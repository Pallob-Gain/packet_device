# Rust Windows PC example

This application uses the Rust library in [`../../rust-lib`](../../rust-lib) to communicate with either the existing C++ ESP32 examples or the [Rust ESP32-S3 firmware](../Rust_ESP32). Defaults are **COM36, 115200 baud, 8-N-1**, without flow control.

From the repository root in PowerShell:

```powershell
cargo build --manifest-path examples/Rust_Windows_PC/Cargo.toml
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --help
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --list
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --port COM36 --baud 115200
```

The build requires Rust and the Visual Studio C++ build tools for the Windows MSVC target. `--help` and `--list` do not open a serial connection. Close Arduino Serial Monitor, Node.js, `espflash monitor`, and other applications using the selected port before connecting.

The client retries the plain `VNR\r\n` command while waiting up to five seconds for a version response. Once connected, it sends these packets every second:

| Name | Encoded data | Expected response |
| --- | --- | --- |
| `ULX` | Two little-endian `f32` values: 5.0 and 0.5 | `amplitude: 5` |
| `counts` | Ten `f32` values, 0 through 90 in steps of 10 | `count0` through `count9` |
| `bio` | Five two-byte age/height records | `len: 5`, `ages: [20, 21, 22, 23, 24]` |

It also decodes unsolicited `env` float arrays and the 900-byte `mdt` structure. All three structure definitions live in [`../rust_common.rs`](../rust_common.rs); their fields are encoded explicitly rather than copying Rust struct memory.

For a finite run, add a positive duration in seconds:

```powershell
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --port COM36 --duration 15
```

Duration starts when the port opens. The process returns a failure exit code if no version response arrives, if the port cannot be opened, or if a transport operation fails. Without a duration, stop with Ctrl+C. Bad packets are reported while reception continues.

The parser intentionally has no automatic incomplete-frame timeout. If a connection loses bytes mid-frame, restart this client to clear its receive state. A normal read timeout means only that no serial bytes arrived in that interval.

COM36 may identify a USB-to-UART bridge or the ESP32-S3 native USB port. Match the firmware transport selection to the connector; see the ESP32 README. The client can use either transport because both appear as Windows serial ports.

Host verification without hardware:

```powershell
cargo test --manifest-path examples/Rust_Windows_PC/Cargo.toml
cargo clippy --manifest-path examples/Rust_Windows_PC/Cargo.toml --all-targets -- -D warnings
```

Tests check the shared structure byte layouts and exchange the client's actual demo packets with the ESP32 application's actual handlers in memory, including fragmented reads and the 900-byte telemetry structure.

Serial I/O uses the [serialport crate](https://docs.rs/serialport/4.10.1/serialport/).
