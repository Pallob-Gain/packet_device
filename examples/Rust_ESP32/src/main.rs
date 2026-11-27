use std::time::{Duration, Instant};

use packet_device::DevicePacketParser;

#[path = "../../rust_common.rs"]
#[allow(dead_code)]
mod common;
mod handlers;
mod transport;

use common::MassiveData;
use handlers::{register_handlers, telemetry};

fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    esp_idf_sys::link_patches();
    let port = transport::Transport::new()?;
    let output = port.clone();
    let mut parser = DevicePacketParser::new(2048)?;
    parser.set_stream_capture_callback(move |bytes| output.write_all(bytes));
    register_handlers(&mut parser);

    let massive = MassiveData::default();
    let mut next_telemetry = Instant::now() + Duration::from_millis(2500);
    let mut bytes = [0_u8; 512];
    let mut errors = 0_u32;
    loop {
        match port.read(&mut bytes) {
            Ok(0) => {}
            Ok(length) => {
                let report = parser.process_bytes(&bytes[..length]);
                errors = errors.saturating_add(report.errors.len() as u32);
            }
            Err(_) => {
                // Transport drops (including UART overflow) invalidate an incomplete frame.
                parser.reset();
                errors = errors.saturating_add(1);
                std::thread::sleep(Duration::from_millis(20));
            }
        }
        if Instant::now() >= next_telemetry {
            if telemetry(&mut parser, &massive, errors).is_err() {
                errors = errors.saturating_add(1);
            }
            next_telemetry = Instant::now() + Duration::from_millis(2500);
        }
    }
}
