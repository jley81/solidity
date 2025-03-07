contract c {
    bytes data;

    function test() public returns (bytes memory) {
        for (uint256 i = 0; i < 34; i++) data.push(0x03);
        data.pop();
        return data;
    }
}

// ====
// compileViaYul: also
// ----
// test() -> 0x20, 33, 0x303030303030303030303030303030303030303030303030303030303030303, 0x0300000000000000000000000000000000000000000000000000000000000000
// gas irOptimized: 160904
// gas legacy: 243287
// gas legacyOptimized: 240361
