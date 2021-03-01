/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Optimisation stage that replaces expressions of type ``sload(x)`` by the value
 * currently stored in storage, if known.
 */

#include <libyul/optimiser/LoadResolver.h>

#include <libyul/backends/evm/EVMDialect.h>
#include <libyul/backends/evm/EVMMetrics.h>
#include <libyul/optimiser/Semantics.h>
#include <libyul/optimiser/CallGraphGenerator.h>
#include <libyul/SideEffects.h>
#include <libyul/AST.h>
#include <libyul/Utilities.h>

#include <libevmasm/Instruction.h>
#include <libevmasm/GasMeter.h>
#include <libsolutil/Keccak256.h>

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/take.hpp>

using namespace std;
using namespace solidity;
using namespace solidity::util;
using namespace solidity::evmasm;
using namespace solidity::yul;

void LoadResolver::run(OptimiserStepContext& _context, Block& _ast)
{
	bool containsMSize = MSizeFinder::containsMSize(_context.dialect, _ast);
	LoadResolver{
		_context.dialect,
		SideEffectsPropagator::sideEffects(_context.dialect, CallGraphGenerator::callGraph(_ast)),
		!containsMSize,
		_context.expectedExecutionsPerDeployment
	}(_ast);
}

void LoadResolver::visit(Expression& _e)
{
	DataFlowAnalyzer::visit(_e);

	auto isKeccak = [&](YulString const& _name) -> bool
	{
		if (auto evmDialect = dynamic_cast<EVMDialect const*>(&m_dialect))
			if (auto builtin = evmDialect->builtin(_name))
				return (
					builtin->instruction &&
					*builtin->instruction == Instruction::KECCAK256
				);

		return false;
	};

	if (FunctionCall const* funCall = std::get_if<FunctionCall>(&_e))
	{
		for (auto location: { StoreLoadLocation::Memory, StoreLoadLocation::Storage })
			if (funCall->functionName.name == m_loadFunctionName[static_cast<unsigned>(location)])
			{
				tryResolve(_e, location, funCall->arguments);
				break;
			}

		if (isKeccak(funCall->functionName.name))
			tryEvaluateKeccak(_e, funCall->arguments);
	}
}

void LoadResolver::tryResolve(
	Expression& _e,
	StoreLoadLocation _location,
	vector<Expression> const& _arguments
)
{
	if (_arguments.empty() || !holds_alternative<Identifier>(_arguments.at(0)))
		return;

	YulString key = std::get<Identifier>(_arguments.at(0)).name;
	if (_location == StoreLoadLocation::Storage)
	{
		if (auto value = util::valueOrNullptr(m_storage, key))
			if (inScope(*value))
				_e = Identifier{locationOf(_e), *value};
	}
	else if (m_optimizeMLoad && _location == StoreLoadLocation::Memory)
		if (auto value = util::valueOrNullptr(m_memory, key))
			if (inScope(*value))
				_e = Identifier{locationOf(_e), *value};
}

void LoadResolver::tryEvaluateKeccak(
	Expression& _e,
	std::vector<Expression> const& _arguments
)
{
	GasMeter gasMeter{
		dynamic_cast<EVMDialect const&>(m_dialect),
		!m_expectedExecutionsPerDeployment,
		m_expectedExecutionsPerDeployment ? *m_expectedExecutionsPerDeployment : 1
	};

	auto costOfKeccak = gasMeter.costs(_e);
	auto costOfLiteral = gasMeter.costs(
		Literal{
			{},
			LiteralKind::Number,
			// a dummy 256-bit number to represent the Keccak256 hash.
			YulString{u256(-1).str()},
			{}
		}
	);

	// We skip if there are no net gas savings.
	// Note that for default `m_runs = 200`, the values are
	// `costOfLiteral = 7200` and `costOfKeccak = 9000` for runtime context.
	// For creation context: `costOfLiteral = 531` and `costOfKeccak = 90`.
	if (costOfLiteral > costOfKeccak)
		return;

	yulAssert(_arguments.size() == 2, "");
	Identifier const* memoryKey = std::get_if<Identifier>(&_arguments.at(0));
	Identifier const* offset = std::get_if<Identifier>(&_arguments.at(1));

	if (!memoryKey || !offset)
		return;

	if (
		auto memoryValue = util::valueOrNullptr(m_memory, memoryKey->name);
		memoryValue &&
		inScope(*memoryValue)
	)
	{
		optional<u256> memoryContent = valueOfIdentifier(*memoryValue);
		optional<u256> memoryOffset = valueOfIdentifier(offset->name);
		if (memoryContent && memoryOffset && *memoryOffset <= 32)
		{
			bytes contentAsBytes = toBigEndian(*memoryContent);
			bytes slicedBytes = contentAsBytes | ranges::views::take(unsigned(*memoryOffset)) | ranges::to<vector>;
			_e = Literal{
				locationOf(_e),
				LiteralKind::Number,
				YulString{u256(keccak256(slicedBytes)).str()},
				m_dialect.defaultType
			};
		}
	}
}
