contract C {
	uint[][] b;
	function f() public {
		b.push().push() = b.push().push();
		uint length = b.length;
		assert(length >= 2);
		uint length1 = b[length - 1].length;
		uint length2 = b[length - 2].length;
		assert(length1 == 1);
		assert(length2 == 1);
		assert(b[length - 1][length1 - 1] == 0);
	}
}
// ====
// SMTEngine: all