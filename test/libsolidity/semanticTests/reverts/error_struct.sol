contract C {
	error test();
	function f() public {
		revert test();
	}
}
// ====
// compileViaYul: also
// ----
// f() -> FAILURE, hex"f8a8fd6d"
