use std::env;
use std::io::{self, Read, Write};
use std::time::{Duration, Instant};

use packet_device::{DevicePacketParser, Packet, PacketWriter, Result, WireValue};

#[path = "../../rust_common.rs"]
mod common;
use common::{Bio, LockInInfo, MassiveData};

#[cfg(test)]
#[path = "../../Rust_ESP32/src/handlers.rs"]
mod device_handlers;

type AppResult<T> = std::result::Result<T, Box<dyn std::error::Error>>;

struct Options {
    port: String,
    baud: u32,
    duration: Option<Duration>,
    list: bool,
}

fn options() -> AppResult<Option<Options>> {
    let mut options = Options {
        port: "COM36".into(),
        baud: 115_200,
        duration: None,
        list: false,
    };
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--help" | "-h" => {
                println!("packet-device-windows [--port COM36] [--baud 115200] [--duration SECONDS] [--list]\n\
                    --list lists ports without opening them. Without --duration, run until Ctrl+C.");
                return Ok(None);
            }
            "--list" => options.list = true,
            "--port" => options.port = args.next().ok_or("--port requires a name")?,
            "--baud" => {
                options.baud = args.next().ok_or("--baud requires a number")?.parse()?;
                if options.baud == 0 {
                    return Err("--baud must be positive".into());
                }
            }
            "--duration" => {
                let seconds: u64 = args.next().ok_or("--duration requires seconds")?.parse()?;
                if seconds == 0 {
                    return Err("--duration must be positive".into());
                }
                options.duration = Some(Duration::from_secs(seconds));
            }
            _ => return Err(format!("Unknown argument: {arg}; use --help").into()),
        }
    }
    Ok(Some(options))
}

fn send_demo(tx: &mut PacketWriter) -> Result<()> {
    tx.rest_raw_out(
        "ULX",
        &LockInInfo {
            amplitude: 5.0,
            phase: 0.5,
        },
    )?;
    tx.rest_array_out(
        "counts",
        &[
            0.0_f32, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0,
        ],
    )?;
    let bios: Vec<Bio> = (0..5)
        .map(|i| Bio {
            age: 20 + i,
            height: 150 + i * 2,
        })
        .collect();
    tx.rest_array_out("bio", &bios)
}

/// Return true for a successfully decoded version response.
fn display_packet(packet: Packet) -> Result<bool> {
    match packet {
        Packet::Value {
            property,
            type_id,
            data,
        } => {
            if property == "mdt" && type_id == MassiveData::TYPE_ID {
                let value = MassiveData::decode(&data)?;
                println!(
                    "mdt: name={:?}, {} floats, first={}",
                    value.name_text(),
                    value.other_data.len(),
                    value.other_data[0]
                );
            } else if type_id == String::TYPE_ID {
                let value = String::decode(&data)?;
                println!("{property}: {value}");
                return Ok(property == "version");
            } else if type_id == f32::TYPE_ID {
                println!("{property}: {}", f32::decode(&data)?);
            } else if type_id == u8::TYPE_ID {
                println!("{property}: {}", u8::decode(&data)?);
            } else if type_id == u32::TYPE_ID {
                println!("{property}: {}", u32::decode(&data)?);
            } else {
                println!("{property}: type={type_id}, {} bytes", data.len());
            }
        }
        Packet::Array {
            property,
            type_id,
            element_size,
            count,
            data,
        } => {
            if type_id == f32::TYPE_ID && element_size == 4 {
                let values: Result<Vec<f32>> = data.chunks_exact(4).map(f32::decode).collect();
                println!("{property}: {:?}", values?);
            } else if type_id == u8::TYPE_ID && element_size == 1 {
                println!("{property}: {data:?}");
            } else {
                println!("{property}: {count} elements, type={type_id}, width={element_size}");
            }
        }
        Packet::Command(command) => println!("Command: {command}"),
        Packet::Text(bytes) => println!("Text: {}", String::from_utf8_lossy(&bytes)),
    }
    Ok(false)
}

fn run(options: Options) -> AppResult<()> {
    if options.list {
        let ports = serialport::available_ports()?;
        if ports.is_empty() {
            println!("No serial ports found.");
        }
        for port in ports {
            println!("{}: {:?}", port.port_name, port.port_type);
        }
        return Ok(());
    }

    let mut input = serialport::new(&options.port, options.baud)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(Duration::from_millis(100))
        .open()?;
    let mut output = input.try_clone()?;
    let mut parser = DevicePacketParser::new(4096)?;
    parser.set_stream_capture_callback(move |bytes| output.write_all(bytes));
    println!(
        "Connected to {} at {} baud. Waiting for version...",
        options.port, options.baud
    );

    let start = Instant::now();
    let mut next_version = start;
    let mut next_demo = start;
    let mut version_received = false;
    let mut buffer = [0_u8; 1024];
    loop {
        let now = Instant::now();
        if options
            .duration
            .is_some_and(|duration| now.duration_since(start) >= duration)
        {
            if !version_received {
                return Err(
                    "Finished without a version response; check the selected port and firmware"
                        .into(),
                );
            }
            break;
        }
        if !version_received && now >= next_version {
            parser.writer().write_text_command("VNR")?;
            next_version = now + Duration::from_millis(500);
        }
        if !version_received && now.duration_since(start) >= Duration::from_secs(5) {
            return Err(
                "No version response after 5 seconds; check port, baud rate and UART/USB mode"
                    .into(),
            );
        }
        if version_received && now >= next_demo {
            send_demo(parser.writer())?;
            next_demo = now + Duration::from_secs(1);
        }

        match input.read(&mut buffer) {
            Ok(0) => {}
            Ok(length) => {
                let report = parser.process_bytes(&buffer[..length]);
                for error in report.errors {
                    eprintln!("Packet error: {error}");
                }
                for packet in report.packets {
                    match display_packet(packet) {
                        Ok(is_version) => version_received |= is_version,
                        Err(error) => eprintln!("Decode error: {error}"),
                    }
                }
            }
            Err(error)
                if matches!(
                    error.kind(),
                    io::ErrorKind::TimedOut
                        | io::ErrorKind::WouldBlock
                        | io::ErrorKind::Interrupted
                ) => {}
            Err(error) => return Err(error.into()),
        }
    }
    println!("Finished; serial port closed.");
    Ok(())
}

fn main() {
    let result = options().and_then(|options| options.map(run).unwrap_or(Ok(())));
    if let Err(error) = result {
        eprintln!("Error: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;
    use std::sync::{Arc, Mutex};

    #[test]
    fn pc_demo_exchanges_commands_with_actual_firmware_handlers() {
        let requests = Arc::new(Mutex::new(Vec::<u8>::new()));
        let request_capture = requests.clone();
        let mut tx = PacketWriter::default();
        tx.set_stream_capture_callback(move |bytes| {
            request_capture.lock().unwrap().extend_from_slice(bytes);
            Ok(())
        });
        tx.write_text_command("VNR").unwrap();
        send_demo(&mut tx).unwrap();

        let replies = Arc::new(Mutex::new(Vec::<u8>::new()));
        let reply_capture = replies.clone();
        let mut device = DevicePacketParser::new(2048).unwrap();
        device.set_stream_capture_callback(move |bytes| {
            reply_capture.lock().unwrap().extend_from_slice(bytes);
            Ok(())
        });
        device_handlers::register_handlers(&mut device);
        for chunk in requests.lock().unwrap().chunks(7) {
            assert!(device.process_bytes(chunk).errors.is_empty());
        }
        device_handlers::telemetry(&mut device, &MassiveData::default(), 0).unwrap();

        let mut pc = DevicePacketParser::new(4096).unwrap();
        let mut values = BTreeMap::new();
        let mut arrays = BTreeMap::new();
        let mut version_found = false;
        for chunk in replies.lock().unwrap().chunks(17) {
            let report = pc.process_bytes(chunk);
            assert!(report.errors.is_empty());
            for packet in report.packets {
                match &packet {
                    Packet::Value { property, data, .. } => {
                        values.insert(property.clone(), data.clone());
                    }
                    Packet::Array { property, data, .. } => {
                        arrays.insert(property.clone(), data.clone());
                    }
                    _ => panic!("unexpected demo response"),
                }
                version_found |= display_packet(packet).unwrap();
            }
        }
        assert!(version_found);
        assert_eq!(String::decode(&values["version"]).unwrap(), "Packet Device");
        assert_eq!(f32::decode(&values["amplitude"]).unwrap(), 5.0);
        for index in 0..10 {
            assert_eq!(
                f32::decode(&values[&format!("count{index}")]).unwrap(),
                index as f32 * 10.0
            );
        }
        assert_eq!(values["len"], [5]);
        assert_eq!(arrays["ages"], [20, 21, 22, 23, 24]);
        assert_eq!(arrays["env"].len(), 20);
        assert_eq!(
            MassiveData::decode(&values["mdt"]).unwrap(),
            MassiveData::default()
        );
    }
}
