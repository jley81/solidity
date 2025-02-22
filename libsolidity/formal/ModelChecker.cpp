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

#include <libsolidity/formal/ModelChecker.h>
#ifdef HAVE_Z3
#include <libsmtutil/Z3Interface.h>
#endif

#include <range/v3/algorithm/any_of.hpp>

using namespace std;
using namespace solidity;
using namespace solidity::util;
using namespace solidity::langutil;
using namespace solidity::frontend;

ModelChecker::ModelChecker(
	ErrorReporter& _errorReporter,
	map<h256, string> const& _smtlib2Responses,
	ModelCheckerSettings _settings,
	ReadCallback::Callback const& _smtCallback,
	smtutil::SMTSolverChoice _enabledSolvers
):
	m_errorReporter(_errorReporter),
	m_settings(_settings),
	m_context(),
	m_bmc(m_context, _errorReporter, _smtlib2Responses, _smtCallback, _enabledSolvers, m_settings),
	m_chc(m_context, _errorReporter, _smtlib2Responses, _smtCallback, _enabledSolvers, m_settings)
{
}

// TODO This should be removed for 0.9.0.
void ModelChecker::enableAllEnginesIfPragmaPresent(vector<shared_ptr<SourceUnit>> const& _sources)
{
	bool hasPragma = ranges::any_of(_sources, [](auto _source) {
		return _source && _source->annotation().experimentalFeatures.count(ExperimentalFeature::SMTChecker);
	});
	if (hasPragma)
		m_settings.engine = ModelCheckerEngine::All();
}

void ModelChecker::analyze(SourceUnit const& _source)
{
	// TODO This should be removed for 0.9.0.
	if (_source.annotation().experimentalFeatures.count(ExperimentalFeature::SMTChecker))
	{
		PragmaDirective const* smtPragma = nullptr;
		for (auto node: _source.nodes())
			if (auto pragma = dynamic_pointer_cast<PragmaDirective>(node))
				if (
					pragma->literals().size() >= 2 &&
					pragma->literals().at(1) == "SMTChecker"
				)
				{
					smtPragma = pragma.get();
					break;
				}
		solAssert(smtPragma, "");
		m_errorReporter.warning(
			5523_error,
			smtPragma->location(),
			"The SMTChecker pragma has been deprecated and will be removed in the future. "
			"Please use the \"model checker engine\" compiler setting to activate the SMTChecker instead. "
			"If the pragma is enabled, all engines will be used."
		);
	}

	if (m_settings.engine.none())
		return;

	if (m_settings.engine.chc)
		m_chc.analyze(_source);

	auto solvedTargets = m_chc.safeTargets();
	for (auto const& target: m_chc.unsafeTargets())
		solvedTargets[target.first] += target.second;

	if (m_settings.engine.bmc)
		m_bmc.analyze(_source, solvedTargets);
}

vector<string> ModelChecker::unhandledQueries()
{
	return m_bmc.unhandledQueries() + m_chc.unhandledQueries();
}

solidity::smtutil::SMTSolverChoice ModelChecker::availableSolvers()
{
	smtutil::SMTSolverChoice available = smtutil::SMTSolverChoice::None();
#ifdef HAVE_Z3
	available.z3 = solidity::smtutil::Z3Interface::available();
#endif
#ifdef HAVE_CVC4
	available.cvc4 = true;
#endif
	return available;
}
