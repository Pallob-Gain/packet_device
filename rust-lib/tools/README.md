# Protocol reference checks

Run from the repository root:

```powershell
cargo test --manifest-path rust-lib/Cargo.toml
node rust-lib/tools/nodejs_oracle.cjs
python rust-lib/tools/cpp_reference.py
```

The Rust tests read checked-in hexadecimal fixtures, so ordinary `cargo test`
does not require Node.js, Python, or a C++ compiler. The two reference commands
check that those fixtures still match the existing implementations.

`nodejs_oracle.cjs` loads the repository's Node.js codec directly. It requires
Node.js but no `npm install`, serial port, or connected board. It covers command,
scalar, array, raw structure, 900-byte structure, 600-element array, and delimiter
framing examples.

`cpp_reference.py` requires Python 3.10+ and `g++` with GNU C++17 support. It
compiles the original `src/DevicePacketParser.cpp` and
`src/CyclicRedundancyCheck.cpp`, then runs a small host harness. Build artifacts
are created in a temporary directory and removed afterwards. No original C++
source is modified. The harness also checks reception of a short CRC-disabled
command and the suppression of empty binary output.

The C++ harness supplies a minimal Arduino `String` shim and disables FreeRTOS
code by compiling for the host. Its simple text examples check normal formatting;
it does not prove every Arduino floating-point-formatting edge case. It also
does not test FreeRTOS synchronization, serial hardware, UART interrupts, or
ESP32 firmware execution.

Numeric alias IDs depend on the C++ compiler. The shared host fixtures use
explicit `uint8_t`, `int32_t`, and `float`, plus structures with asserted sizes.
They do not infer modern Xtensa `int`/`long` alias behavior from the host compiler.
The Rust suite checks the selected ESP-IDF 5 profile separately: `rest_out_int`
uses protocol ID 9, while a plain Rust `i32` uses fixed-width ID 4.

To regenerate fixtures after an intentional protocol change, append `--write`
to the appropriate reference command, then review the fixture diff and run the
Rust tests. Do not regenerate fixtures solely to hide a failed compatibility
check.
