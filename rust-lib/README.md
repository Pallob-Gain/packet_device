# Packet Device for Rust

`packet-device` is a native Rust implementation of this repository's C++ `DevicePacketParser` interface and wire protocol. The same library runs on Windows and ESP32-S3 with ESP-IDF and Rust `std`. It has **no external crate dependencies**. The examples supply their own serial or ESP-IDF transport dependencies.

The library receives byte slices, reconstructs packets across arbitrary read boundaries, checks CRCs, and dispatches command or typed-data handlers. A user-supplied callback writes outgoing bytes. UART, serial ports, USB, sockets, and other ordered byte transports can use the same parser.

The selected porting scope is `DevicePacketParser`, its protocol constants, CRC functions, value conversion, and output helpers. The older Arduino `DevicePacket` stream/command-queue class and `ByteRingBuffer` are not part of this Rust library. See [the coverage and compatibility map](PARITY.md) for the public API correspondence and retained C++ behavior.

## Add the library

Use Rust 1.82 or later for desktop builds. In another Cargo project, point the dependency to this directory:

```toml
[dependencies]
packet-device = { path = "../packet_device/rust-lib" }
```

Adjust the path for your project. The package name is `packet-device`; the Rust import name is `packet_device`.

This library uses `std` and allocation. It is not a `no_std` or allocation-free crate. Its ESP32 example uses ESP-IDF's `std` environment.

## Receive a command and send a response

```rust
use packet_device::{DevicePacketParser, Result};

fn main() -> Result<()> {
    let mut device = DevicePacketParser::new(2048)?;

    // Demonstration sink: display a complete outgoing transmission.
    // A real transport should write ALL bytes or return an io::Error.
    device.set_stream_capture_callback(|bytes| {
        println!("TX: {bytes:02x?}");
        Ok(())
    });

    device.on_command("VNR", |tx| {
        tx.rest_out_str("version", "Packet Device")
    });

    // A transport may split a command or packet at any byte boundary.
    let first = device.process_bytes(b"VN");
    assert!(first.packets.is_empty());
    let second = device.process_bytes(b"R\r\n");
    for error in second.errors {
        eprintln!("Receive or handler error: {error}");
    }

    device.writer().rest_out("amplitude", &5.0_f32)?;
    device.writer().rest_array_out("env", &[10.0_f32, 20.0, 30.0])?;
    Ok(())
}
```

`process_bytes` dispatches completed packets immediately and returns a `ProcessReport`. Its `packets` collection also exposes decoded packets to applications that prefer a polling loop; `errors` contains parsing and callback failures. An error in one complete packet does not prevent processing subsequent packets in the supplied slice. A call containing only a partial packet produces no decoded packet until more bytes arrive.

Receive handlers are passed a separate `&mut PacketWriter`. This lets a handler respond immediately without borrowing the entire parser again. Registered closures are `Send + 'static` and can capture owned state. The library does not create a receive thread or read a serial port: feed bytes from your own task or event loop. Each parser is used through `&mut self`; applications that share one across threads must coordinate access themselves.

The output callback has the form `FnMut(&[u8]) -> std::io::Result<()>`. A formatted transmission is delivered in one callback invocation. For a standard I/O writer, call `write_all`, not a single possibly partial `write`. Without a callback, nonempty output returns `Error::NoTransport`; transport failures become `Error::Io`.

## Commands and binary callbacks

The text command forms match the C++ parser:

| Input before the delimiter | Registration | Callback arguments after `tx` |
| --- | --- | --- |
| `VNR` | `on_command("VNR", ...)` | None |
| `SET=12` | `on_value("SET", ...)` | Value text |
| `GET:temperature` | `on_param("GET", ...)` | Parameter text |
| `PUT:x=2.5` | `on_param_value("PUT", ...)` | Parameter and value text |

The parameterized forms use three-byte command names. Assignment with both a parameter and value uses one byte for the parameter (`PUT:x=2.5`), just like C++. Binary command-text packets use an exact command-name lookup; their content is not reparsed using these parameter rules.

Use `on_receive::<T, _>(name, handler)` for a decoded scalar or structure, `on_receive_array::<T, _>(name, handler)` for a decoded array, and `on_raw(name, handler)` for bytes plus the received type ID, element size, and count. Registering a typed handler takes precedence over the raw handler for that property, as in C++.

`on_packet_receive` is a whole-packet interceptor. It receives the body after framing or delimiter removal, including CRC bytes when present, and replaces normal CRC verification, decoding, and command dispatch. Use it only when the application intends to handle those steps. Intercepted packets are counted in `ProcessReport::intercepted` and are not added to `packets`. `clear_packet_receiver()` restores normal decoding.

## Define a structure's wire layout explicitly

The ESP32 C++ examples send native little-endian values. Rust's default struct layout is not a packet format. Implement `WireValue` with a declared size and explicit field encoding:

```rust
use packet_device::{Error, Result, WireValue, DATA_TYPE_VOID};

#[derive(Debug, PartialEq)]
struct LockInInfo {
    amplitude: f32,
    phase: f32,
}

impl WireValue for LockInInfo {
    const TYPE_ID: u8 = DATA_TYPE_VOID;
    const WIRE_SIZE: Option<usize> = Some(8);

    fn encode(&self) -> Vec<u8> {
        [self.amplitude.to_le_bytes(), self.phase.to_le_bytes()].concat()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        if bytes.len() != 8 {
            return Err(Error::InvalidLength {
                expected: 8,
                actual: bytes.len(),
            });
        }
        Ok(Self {
            amplitude: f32::decode(&bytes[..4])?,
            phase: f32::decode(&bytes[4..])?,
        })
    }
}
```

Then register and transmit it:

```rust,ignore
device.on_receive::<LockInInfo, _>("ULX", |tx, value| {
    tx.rest_out("amplitude", &value.amplitude)
});
device.writer().rest_raw_out("ULX", &LockInInfo {
    amplitude: 5.0,
    phase: 0.5,
})?;
```

If a C++ structure contains padding, encode the agreed padding bytes at the same offsets. For example, a `uint8_t` followed by an aligned `float` may need three padding bytes between the fields. `#[repr(C)]` alone does not initialize padding or make arbitrary incoming bytes safe to cast. The library contains no `unsafe` code and does not alias received bytes as a Rust object.

The shared [example codecs](../examples/rust_common.rs) define `LockInInfo` (8 bytes), `Bio` (2 bytes), and `MassiveData` (900 bytes). A large structure can be sent as one raw value; array elements must fit the protocol's one-byte element-size field.

Primitive `WireValue` implementations cover signed and unsigned 8/16/32/64-bit integers, `f32`, `f64`, `bool`, UTF-8 `String`, and raw `[u8; N]` values. Numeric payloads use little endian, floats use their IEEE byte representations, and booleans transmit one byte (`0` or `1`). Raw byte arrays use type ID `0`.

`Int`, `UInt`, `Long`, and `ULong` wrappers allow explicit use of protocol IDs 9, 10, 13, and 14 with the ESP32's 32-bit widths. On the modern Xtensa toolchain used by ESP-IDF 5, `int32_t` is `long`, so C++ `int` emits ID 9 while `int32_t` and `long` emit ID 4. `rest_out_int` follows the modern toolchain and sends ID 9; `rest_out(property, &value_i32)` sends fixed-width ID 4. Older GCC 8 Xtensa peers alias `int32_t` to `int` and therefore send ID 4 for both. These compiler aliases are documented in the [ESP-IDF GCC migration guide](https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/migration-guides/release-5.x/5.0/gcc.html). Match the actual peer's ID when choosing a primitive or wrapper. `Null` represents ID 17 with no payload bytes; ordinary binary output suppresses an empty payload to match C++.

## Framing and configuration

Defaults are CRLF delimiters, binary output, CRC enabled, and initial CRC `0x0000`:

```rust,ignore
let mut device = DevicePacketParser::new(2048)?;
let custom = DevicePacketParser::with_delimiters(2048, b"\n")?;
let binary_only = DevicePacketParser::with_delimiters(2048, &[])?;

device.set_buffer_mode(false); // Delimiter-based output.
device.set_initial_crc(0x0000);
device.set_verify_crc(false);  // Disable outgoing CRC and incoming verification.
```

Both incoming framed binary packets and delimiter-terminated commands are recognized regardless of the output mode. An empty delimiter disables delimiter-based reception, leaving binary framing available. `with_crlf(capacity, false)` is an equivalent convenience constructor. All peers must agree about CRC enablement and seed for binary packets. Configuration and output methods are also available directly on the parser through its `PacketWriter` access implementation; examples use `writer()` to make the transmission role explicit.

Binary framing starts with nine bytes: `< n - n * n - n >`, where each `n` is a binary nibble from the 16-bit body length, most significant nibble first. The announced length includes the CRC trailer when enabled. The length bytes are not printable hex characters. Packet body length/count fields and CRC bytes use big endian; payload numeric values use little endian.

The CRC is non-reflected, polynomial `0x1021`, seed `0x0000` by default, and no final XOR. ASCII `123456789` gives `0x31C3`. Changing the seed to `0xFFFF` gives `0x29B1`; change both peers together. CRC checks detect accidental corruption and do not provide encryption or authentication.

For a PC-style plain command, use `writer().write_text_command("VNR")`: it sends the command and delimiter without a CRC. `write_command("VNR")` sends the binary command-text packet. `write_to_port(bytes)` sends bytes unchanged, without adding framing, CRC, or a delimiter.

Text output deliberately retains C++'s JSON-like formatting. Strings and property names are not escaped, numeric booleans are `1`/`0`, scalar floats use two decimal places, and `f32` array elements use five. When enabled, two raw CRC bytes precede the delimiter, so the complete output is not a standalone JSON line. The `rest_out_hex` and `rest_out_bin` helpers send ordinary unsigned numeric values, matching C++. `rest_raw_out` continues to emit a binary value body even when delimiter-based output is selected.

## Limits and error handling

- Receive capacity must be 1 through 65,535. Choose at least 10 bytes to recognize the nine-byte signature, and more than the largest expected body including CRC. Incoming announced length must be strictly less than the configured capacity, matching C++. Outgoing body plus CRC must fit in 65,535 bytes.
- High-level scalar/raw writers accept property names up to 249 bytes; array writers accept up to 248. These limits preserve the existing C++ transmitter's eight-bit total-header size. The standalone body codec supports the full one-byte property field of 255 bytes.
- Binary arrays have at most 65,535 elements, each 1 through 255 bytes, and must also meet the total packet-size limit. Typed binary arrays require a fixed `WIRE_SIZE`.
- Empty binary strings and arrays produce no transmission, matching the current C++ output helper. Empty text-mode values still produce text output.
- Invalid lengths, CRC failures, unsupported text conversion, and invalid UTF-8 commands/property names/strings are reported. `Packet::Text` and raw payloads retain their bytes. Check every `ProcessReport::errors` collection and every output `Result`.
- Oversized framed input is discarded as a whole body, so embedded commands are not executed. No receive timeout runs automatically. Call `reset()` when a transport reconnects or reports lost bytes, or when an application-managed deadline expires; it clears an incomplete receive state.

Low-level `codec` functions are public for applications that need direct framing or decoding. `encode_command_body`, `encode_value_body`, and `encode_array_body` return bodies **without** CRC or the outer signature. `decode_packet` expects the body, including a CRC if verification is enabled. Avoid adding framing twice.

## Run the examples and checks

Run these commands from the repository root:

```powershell
cargo test --manifest-path rust-lib/Cargo.toml
cargo fmt --manifest-path rust-lib/Cargo.toml -- --check
cargo clippy --manifest-path rust-lib/Cargo.toml --all-targets -- -D warnings
node rust-lib/tools/nodejs_oracle.cjs
python rust-lib/tools/cpp_reference.py
```

The reference commands verify committed packet fixtures against the existing local Node.js codec and the unchanged C++ parser. The C++ check requires Python and GNU C++17 and uses a temporary Arduino shim; see the [reference-check instructions](tools/README.md). Rust tests use the fixtures without those extra tools to check wire interoperability as well as fragmented input, CRC settings, callbacks, and malformed data. These are host checks; they do not by themselves verify a physical serial link or flashed ESP32 board.

The [Windows PC example](../examples/Rust_Windows_PC/README.md) uses `COM36` at 115,200 baud by default and sends the same demonstration commands and structures as the existing Node.js example:

```powershell
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --list
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --port COM36 --baud 115200 --duration 10
```

The [ESP32-S3 example](../examples/Rust_ESP32/README.md) uses ESP-IDF `std`, provides UART0 and optional native USB Serial/JTAG transports, and responds to `VNR`, `ULX`, `counts`, and `bio`. It sends periodic `env` and `mdt` telemetry. Follow its toolchain, board, connector, build, and flash instructions before running the Windows peer. The two examples depend on this same library via Cargo path dependencies.

License: [MIT](LICENSE).
