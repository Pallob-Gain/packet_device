# DevicePacketParser compatibility map

This document maps the public C++ `DevicePacketParser` in [`src/DevicePacketParser.h`](../src/DevicePacketParser.h) and its implementations to the native Rust library. The port preserves valid packet bytes and the selected parser's command/output features while expressing ownership, typed values, and errors through Rust APIs.

The separate legacy `DevicePacket` Arduino stream/command queue and `ByteRingBuffer` are outside the selected scope. Their transport polling, queue processing, FreeRTOS mutex/event-group methods, and auto-flush methods are not claimed as Rust features. The Rust examples provide transport integration around `DevicePacketParser`.

## Public interface coverage

All Rust output helpers are available on the `PacketWriter` obtained with `parser.writer()`. Receive callbacks are passed that writer directly as their first argument.

| C++ entry point | Rust equivalent | Behavior |
| --- | --- | --- |
| `DevicePacketParser(buffer_size)` | `DevicePacketParser::new(capacity)?` | CRLF delimiters, binary output, CRC enabled, seed zero. |
| `DevicePacketParser(buffer_size, delimiters)` | `DevicePacketParser::with_delimiters(capacity, delimiters)?` | Custom delimiter bytes. |
| `DevicePacketParser(buffer_size, use_crlf)` | `DevicePacketParser::with_crlf(capacity, use_crlf)?` | Choose CRLF or no delimiter detection; binary framing remains enabled. |
| Destructor | Rust ownership and automatic drop | Owned buffers and callbacks are released automatically. |
| `setStreamCaptureCallback` | `set_stream_capture_callback` | Attach an output sink. Rust sink returns an I/O result and consumes a complete transmission. |
| `setReceiver` overloads | `set_receiver_*` family | Replace the corresponding handler category; see the handler table below. |
| `onReceive(name, void())` | `on_command(name, callback)` | Exact command-name handler. |
| `onReceive(name, void(String), false)` | `on_value(name, callback)` | `CMD=value` handler. |
| `onReceive(name, void(String), true)` | `on_param(name, callback)` | `CMD:param` handler. |
| `onReceive(name, void(String, String))` | `on_param_value(name, callback)` | `CMD:p=value` handler. |
| `onReceive(name, raw callback)` | `on_raw(name, callback)` | Raw payload, type ID, element size, and count through `RawValue`. |
| `onReceive<T>(name, void(T*))` | `on_receive::<T, _>(name, callback)` | Decode one value through `WireValue`. |
| `onReceive<T>(name, void(T*, uint16_t))` | `on_receive_array::<T, _>(name, callback)` | Decode array elements through `WireValue`. |
| `onPacketReceive` | `on_packet_receive` | Intercept complete bodies before CRC checking and decoding; bypass ordinary handlers. |
| `setVerifyCRC` | `set_verify_crc` | Control outgoing CRC and incoming binary CRC checking together. |
| `setInitialCRC` | `set_initial_crc` | Configure the CRC seed for both directions. |
| `processBytes` | `process_bytes` | Accept arbitrary chunks; dispatch completed packets immediately and return a `ProcessReport`. |
| `getBufferMode` / `setBufferMode` | `get_buffer_mode` / `set_buffer_mode` | Query/select outgoing binary framing versus delimiter-based output. |
| `writeToPort` | `writer().write_to_port` | Write bytes unchanged; no framing, CRC, or delimiter. |
| `restRawOut<T>` | `writer().rest_raw_out::<T>` | Named raw structure/scalar body from explicit encoded bytes. |
| `restOut<T>` | `writer().rest_out::<T>` | Typed binary output or the type's text representation. |
| `restOut<String, true>` | `writer().rest_out_unquoted` | Suppress quotes around string contents in text mode. |
| `restArrayOut<T>` | `writer().rest_array_out::<T>` | Typed array output. |
| `restOutStr` | `writer().rest_out_str` | String output. |
| `restOutFloat` | `writer().rest_out_float` | `f32` output. |
| `restOutInt` | `writer().rest_out_int` | Signed 32-bit `int`, ID 9 on the modern ESP-IDF Xtensa toolchain. |
| `restOutHex` / `restOutBin` | `writer().rest_out_hex` / `rest_out_bin` | Ordinary `u32` output, as implemented in C++. |
| `restOutSuccess` | `writer().rest_out_success` | String under property `payload`. |
| `restOutError` | `writer().rest_out_error` | String under property `error`. |
| `CyclicRedundancyCheck::calculateCRC16` | `crc16` | Polynomial `0x1021`; caller-selected initial CRC. |
| `CyclicRedundancyCheck::verifyCRC16` | `verify_crc16` | Check the two-byte big-endian trailer. |
| Communication/basic constants | Public `codec` constants | All packet markers, header lengths, and type IDs; original misspellings retained as aliases. |
| `getTypeID<T>()` | `get_type_id::<T>()` / `T::TYPE_ID` | Explicit wire type ID supplied by `WireValue`. |

Rust also exposes `PacketWriter` independently, `rest_raw_bytes`, `rest_array_bytes`, `write_command`, `write_text_command`, `reset`, packet/body codecs, and getters for CRC configuration. These make the existing protocol usable without Arduino `Stream` or arbitrary memory casts.

## Callback and dispatch behavior

The `set_receiver_*` methods accept `BTreeMap<String, HandlerAlias>` maps of boxed callbacks and replace their own category. Individual `on_*` registrations replace an existing registration with the same name in that category. Handlers return `Result<()>`; an error is recorded in the current `ProcessReport`.

| C++ callback category | Rust callback alias | Individual registration | Whole-map replacement |
| --- | --- | --- | --- |
| No arguments | `CommandHandler` | `on_command` | `set_receiver_commands` |
| Value string | `TextHandler` | `on_value` | `set_receiver_values` |
| Parameter string | `TextHandler` | `on_param` | `set_receiver_params` |
| Parameter plus value strings | `ParamValueHandler` | `on_param_value` | `set_receiver_param_values` |
| Binary data plus metadata | `RawHandler` | `on_raw` | `set_receiver_raw` |
| Whole completed packet bytes | `PacketHandler` | `on_packet_receive` | One callback; clear with `clear_packet_receiver` |

For example, replace all no-argument command handlers:

```rust,ignore
use std::collections::BTreeMap;
use packet_device::CommandHandler;

let mut handlers: BTreeMap<String, CommandHandler> = BTreeMap::new();
handlers.insert("VNR".into(), Box::new(|tx| {
    tx.rest_out_str("version", "Packet Device")
}));
parser.set_receiver_commands(handlers);
```

The C++ handler selection predicate is retained: a typed handler accepts its matching type ID, or `VOID` with a matching declared size. Typed registrations take priority over raw handlers for the same property, including when the type predicate rejects a packet. Rust then decodes only from available bytes; it does not reproduce out-of-bounds reads, unaligned references, or alias the receive buffer as an application object. Scalar callbacks receive an owned `T`; array callbacks receive an owned `Vec<T>`. A scalar callback receiving an array decodes its first element, matching C++'s scalar pointer behavior. Typed values are not writable pointers into the parser's storage.

Scalar metadata reports the payload byte length as element size and a count of one. Array metadata reports the declared element width and count. Plain text parameter grammar retains three-byte command names and the one-byte parameter used by `CMD:p=value`. Binary command text receives exact-name dispatch only. Unregistered valid packets are still available through the report's `packets` collection.

The packet interceptor replaces normal dispatch, including CRC checking. It receives a binary body with CRC bytes still attached or text bytes without the delimiter; it does not receive the outer length signature. Applications using it take responsibility for decoding and validation. Intercepted bodies increment `ProcessReport::intercepted` and are not added to its decoded `packets` collection.

## Wire compatibility

| Field or behavior | Representation |
| --- | --- |
| Outer signature | Nine bytes `< n - n * n - n >`; length digits are binary nibbles. |
| Announced length | 16-bit body length including the optional CRC. |
| Body prefix | `0x2A` followed by text `0x5E`, scalar `0x5F`, or array `0x60`. |
| Lengths and element count | Big endian. |
| CRC | Non-reflected polynomial `0x1021`, default seed `0x0000`, no final XOR, high byte first. |
| Integer/float payloads | Explicit little endian, matching the selected ESP32 target. |
| String payload | UTF-8 bytes without a trailing NUL. |
| Structure payload | Application-defined field encoding, with any C++ padding represented explicitly. |
| Plain text commands | Delimiter terminated; no automatic CRC verification or removal. |
| Binary trailing bytes | Decoder accepts trailing bytes after the declared contents, as C++ does; CRC covers the whole body. |
| Incoming modes | Framed packets and delimited text are recognized independently of the output-mode setting. |

All type IDs are available:

| ID | Rust representation | Payload width |
| --- | --- | --- |
| 0 | `[u8; N]` or a custom `WireValue` | Explicit size |
| 1 / 2 | `u64` / `i64` | 8 bytes |
| 3 / 4 | `u32` / `i32` | 4 bytes |
| 5 / 6 | `u16` / `i16` | 2 bytes |
| 7 / 8 | `u8` / `i8` | 1 byte |
| 9 / 10 | `Int` / `UInt` wrappers | 4 bytes |
| 11 / 12 | `f32` / `f64` | 4 / 8 bytes |
| 13 / 14 | `Long` / `ULong` wrappers | 4 bytes |
| 15 | `String` | Variable UTF-8 byte length |
| 16 | `bool` | 1 byte |
| 17 | `Null` | 0 bytes |

The wrapper IDs are explicit protocol choices. C++ `int`/`long` may be aliases of fixed-width types checked earlier by `getTypeID`, so those source names do not always select IDs 9/13. For the selected modern ESP-IDF Xtensa ABI, `int32_t` is `long` and `uint32_t` is `unsigned long`; therefore C++ `int` selects ID 9, `unsigned int` selects ID 10, `long` selects ID 4, and `unsigned long` selects ID 3. `rest_out_int` sends ID 9. To match an older GCC 8 Xtensa peer where `int32_t` aliases `int`, use `rest_out` with an ordinary `i32` to send ID 4. See the [official toolchain migration guide](https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/migration-guides/release-5.x/5.0/gcc.html).

The C++ `null_type` is an alias of `uint8_t` and selects ID 7 before its null branch. The `Null` wrapper preserves access to ID 17 as supported by the Node.js codec; ordinary Rust high-level binary output still suppresses its empty payload.

## Retained behavior and deliberate Rust boundaries

- Empty binary values/arrays generate no frame, following the current C++ `size == 0` return. Text-mode empty strings and arrays still have nonempty text bodies.
- Text responses remain JSON-like rather than a general JSON serialization: names and strings are unescaped; booleans are numeric; scalar floats use two decimals; `f32` arrays use five. CRC bytes, when enabled, appear before the delimiter.
- Raw output stays binary even in delimiter-based output mode. Hex/bin helpers keep their ordinary numeric behavior. Success/error property names remain `payload` and `error`.
- The receive parser has no automatic timeout. The removed C++ timeout assignments never supplied an enforced timeout. Rust adds an explicit `reset` to discard partial receive state when the application decides recovery is needed.
- High-level property names are limited to 249 bytes for scalar/raw output and 248 for arrays, preserving the C++ eight-bit header-size argument. Low-level body encoding supports the protocol's 255-byte name field. Names exceeding a supported limit return an error rather than silently truncating.
- The body plus CRC cannot exceed 65,535 bytes. Incoming bodies must be smaller than the configured receive capacity. Binary array elements must be fixed-size values of 1 through 255 bytes. Rust checks lengths before arithmetic, slicing, and encoding.
- Oversized framed bodies are discarded for their announced length rather than being reinterpreted as commands. A text buffer that reaches capacity is cleared and reported, matching C++'s reset point before delimiter/signature matching. Malformed recognized binary bodies and truncated declared contents produce errors. Valid following packets can still be processed after the malformed complete frame is consumed.
- String/command/property decoding requires valid UTF-8. Raw bytes and `Packet::Text` remain available for byte-oriented application handling.
- Output callbacks return `std::io::Result<()>` and must consume the entire supplied slice. One formatted transmission uses one callback invocation instead of the C++ callback's separate signature/header/payload/CRC writes. Packet bytes are unchanged, while callback chunk boundaries are intentionally different.
- Handler ownership and mutable access replace C++ raw pointers and implicit FreeRTOS locks. Applications own scheduling, transport reads, and any cross-thread synchronization. The library uses `std`, owned buffers, and safe Rust; it does not promise lock-free, zero-copy, or `no_std` execution.

## Verification

Run `cargo test --manifest-path rust-lib/Cargo.toml` from the repository root. The protocol and dispatch integration tests cover representative C++/Node.js packet formats, all primitive codecs, fragmented and concatenated input, the corrected array count encoding, both CRC settings and custom seeds, callback selection, response generation, and malformed/oversized input handling.

`node rust-lib/tools/nodejs_oracle.cjs` independently checks the committed reference bytes against the existing local Node.js implementation. The explicit example structures are shared by the Windows and ESP32 programs. Host tests and a successful firmware build do not establish physical-device communication; a connected-board run is a separate validation step.
