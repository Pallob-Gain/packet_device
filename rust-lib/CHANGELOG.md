# Changelog

## 0.1.0

Initial Rust release of the Packet Device protocol and `DevicePacketParser` interface.

- Incremental reception of binary frames and delimited commands, with configurable CRC-16 validation.
- Command, typed-value, array, raw-data, and whole-packet callbacks, plus transport-independent output through `PacketWriter`.
- Explicit little-endian wire codecs for primitive values and application-defined structures.
- Compatibility tests using committed C++ and Node.js packet fixtures, including fragmented input, dispatch, and malformed packets.
- No external crate dependencies or unsafe code. Requires Rust 1.82 or later and `std`; supports desktop and ESP-IDF applications.

See the [compatibility map](https://github.com/Pallob-Gain/packet_device/blob/main/rust-lib/PARITY.md) for retained C++ behavior and the scope of the Rust port.
