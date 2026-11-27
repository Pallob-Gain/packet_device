# Rust ESP32-S3 example (ESP-IDF + std)

This firmware uses [`../../rust-lib`](../../rust-lib) and responds to the same `VNR`, `ULX`, `counts`, and `bio` commands as the existing C++ UART example. Every 2.5 seconds it sends `env` (five sensor floats) and `mdt` (a 900-byte structure). The [Windows Rust client](../Rust_Windows_PC) and the existing Node.js client can communicate with it.

The project targets **ESP32-S3**, with `std` on ESP-IDF, and pins `esp-idf-hal` 0.47.0, `esp-idf-sys` 0.38.1, and ESP-IDF v5.4.4. It uses published crates; the upstream Rust sources now live in the [esp-rs/esp-idf repository](https://github.com/esp-rs/esp-idf).

## Select the physical transport

| Build | ESP32-S3 connection | Windows side |
| --- | --- | --- |
| Default | UART0 TX GPIO43, RX GPIO44, 115200 baud, 8-N-1 | Board's USB-to-UART connector/bridge |
| `--features usb-serial-jtag` | Native USB Serial/JTAG, D- GPIO19 and D+ GPIO20 | Board's native USB connector |

**COM36 is a host port name, not a firmware setting.** Use the Windows client's `--list` option to identify which connector owns that port. Boards with two USB connectors often expose separate ports. The optional USB build uses the fixed USB Serial/JTAG controller; it does not implement TinyUSB/USB-OTG CDC. No UART GPIO jumper wiring is needed when using the board's USB-to-UART bridge. If using an external bridge, connect its RX to GPIO43, TX to GPIO44, and ground to ground, using 3.3 V logic.

The UART implementation processes ESP-IDF receive error events and clears the parser after lost data. The protocol remains transport-independent; platform code is isolated in [`src/transport.rs`](src/transport.rs). Structures are shared with the host via [`../rust_common.rs`](../rust_common.rs).

## Prepare a Windows build environment

Install Rust with the Windows MSVC host toolchain, Visual Studio C++ build tools, Git, Python 3, CMake, and Ninja. Follow the [ESP Rust template prerequisites](https://github.com/esp-rs/esp-idf-template) for the native ESP-IDF build environment.

In PowerShell, outside this directory until the `esp` toolchain is installed:

```powershell
cargo install espup --locked
espup install --targets esp32s3 --std
cargo install ldproxy --locked
cargo install espflash --locked
```

Open a new PowerShell terminal after installation. Current `espup` configures Windows environment variables automatically; follow its installer output if your version requests additional setup. See the [espup instructions](https://github.com/esp-rs/espup#environment-variables-setup).

Use a **short checkout path without spaces**, for example `D:\packet_device`, including both `rust-lib` and `examples`, for predictable Windows tooling. Copy the complete current checkout, including the new Rust files, or use a checkout containing those files; moving only this example breaks its relative dependency. See [Espressif's Windows project requirements](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-start-project.html).

The build output and SDK files are stored under **`E:\Software projects`**:

| Physical storage | Short path used by the build tools |
| --- | --- |
| `E:\Software projects\packet-device-build` | `E:\packet-device-build` |
| `E:\Software projects\esp-idf-tools` | `E:\esp-idf-tools` |

The short paths are directory junctions to the physical storage, so they do not duplicate the files. The junctions have already been created on this machine. For a new installation, create the directories and junctions once:

```powershell
New-Item -ItemType Directory -Path 'E:\Software projects\packet-device-build', 'E:\Software projects\esp-idf-tools' -Force
New-Item -ItemType Junction -Path 'E:\packet-device-build' -Target 'E:\Software projects\packet-device-build'
New-Item -ItemType Junction -Path 'E:\esp-idf-tools' -Target 'E:\Software projects\esp-idf-tools'
```

`esp-idf-sys` checks the resolved physical output path against its 88-character Windows limit, even when a junction is used. Set its supported `ESP_IDF_PATH_ISSUES=warn` policy for this layout, while passing the short paths to the tools. This changes the path check to an advisory; other build errors still fail normally. The supplied Cargo configuration keeps SDK settings tied to this example even with an external target directory.

Legacy path junctions on this machine also point to these same directories. They preserve absolute paths in existing CMake caches and the SDK Python environment; they hold no separate copies of the build or SDK files. Use the descriptive paths above for new build commands.

## Build and flash

Run from this example directory so Cargo loads `.cargo/config.toml` and `rust-toolchain.toml`:

```powershell
Set-Location D:\packet_device\examples\Rust_ESP32
$env:CARGO_TARGET_DIR = 'E:\packet-device-build'
$env:ESP_IDF_TOOLS_INSTALL_DIR = 'custom:E:\esp-idf-tools'
$env:ESP_IDF_PATH_ISSUES = 'warn'
cargo build --release
```

The first build downloads ESP-IDF and its native tools into the selected SDK directory (`E:\Software projects\esp-idf-tools` through the `E:\esp-idf-tools` junction above; `.embuild` by default), then builds Rust's standard library for `xtensa-esp32s3-espidf`. An already-set `IDF_PATH` overrides the configured ESP-IDF version; use a matching SDK or a shell without an unrelated IDF environment.

To flash the default UART build when COM36 is the board's programming port:

```powershell
espflash flash --port COM36 E:/packet-device-build/xtensa-esp32s3-espidf/release/packet-device-esp32
```

For native USB Serial/JTAG data transport instead:

```powershell
cargo build --release --features usb-serial-jtag
espflash flash --port COM36 E:/packet-device-build/xtensa-esp32s3-espidf/release/packet-device-esp32
```

Use the actual programming port when flashing. After a native USB flash, the application's serial port may enumerate under a different COM number. Then start the Windows client from the repository root:

```powershell
cargo run --manifest-path examples/Rust_Windows_PC/Cargo.toml -- --port COM36 --duration 15
```

Do not attach a serial monitor while the client owns the port. The firmware emits binary packets, so terminal output is not a useful packet decoder.

## Runtime details

- The parser holds up to 2048 bytes; reads arrive in chunks of at most 512 bytes.
- Command callbacks reply through the same packet writer. `counts` and `bio` demonstrate typed array callbacks, and `ULX` demonstrates a typed structure callback.
- `sdkconfig.defaults` disables application console output and second-stage bootloader logging to keep protocol bytes intact. ROM startup messages can still appear during reset; the host retries its version request while the board starts.
- UART overflow/parity/framing errors reset receive state. Detected packet/transport errors accumulate in the optional `rx_errors` telemetry value.
- No incomplete-packet timer is added. Ordinary short reads preserve parser state; explicit transport loss resets it.
- The selected connector carries only protocol traffic during normal operation. Use JTAG/debugger inspection for firmware diagnostics, or deliberately configure a separate logging channel.

Both UART and USB Serial/JTAG variants were cross-compiled successfully in release mode for `xtensa-esp32s3-espidf`, using Xtensa Rust 1.97.0.0 and ESP-IDF v5.4.4. The UART firmware was also rebuilt successfully using the E: storage layout above, with the expected path-length advisory. The Windows tests also exercise these actual application handlers through an in-memory connection. Hardware flashing and communication remain separate validation steps.

The local validation artifacts are under the ignored `target/firmware` directory: `packet-device-esp32-uart.elf`, `packet-device-esp32-usb.elf`, `bootloader.bin`, and `partition-table.bin`. These generated files are not included in version control. When using these ELF files with `espflash`, pass the corresponding `--bootloader target/firmware/bootloader.bin` and `--partition-table target/firmware/partition-table.bin` options to use the SDK-built bootloader and partition layout. Without those options, standalone `espflash` uses its defaults; its bundled bootloader can print startup logs independently of this project's SDK configuration.

UART and USB APIs follow the [HAL UART documentation](https://esp-rs.github.io/esp-idf-hal/esp_idf_hal/uart/struct.UartDriver.html) and [USB Serial/JTAG implementation](https://esp-rs.github.io/esp-idf-svc/src/esp_idf_hal/usb_serial.rs.html).
