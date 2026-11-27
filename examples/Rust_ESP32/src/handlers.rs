//! Application handlers can be tested on the PC without the ESP-IDF drivers.

use crate::common::{Bio, LockInInfo, MassiveData};
use packet_device::{DevicePacketParser, Result};

pub fn register_handlers(parser: &mut DevicePacketParser) {
    parser.on_command("VNR", |tx| tx.rest_out_str("version", "Packet Device"));
    parser.on_receive::<LockInInfo, _>("ULX", |tx, info| tx.rest_out("amplitude", &info.amplitude));
    parser.on_receive_array::<f32, _>("counts", |tx, counts| {
        for (index, value) in counts.iter().enumerate() {
            tx.rest_out(&format!("count{index}"), value)?;
        }
        Ok(())
    });
    parser.on_receive_array::<Bio, _>("bio", |tx, bios| {
        // Keep the C++ example's one-byte length response for its small demo array.
        let length = u8::try_from(bios.len())
            .map_err(|_| packet_device::Error::LimitExceeded("demo Bio count exceeds 255"))?;
        tx.rest_out("len", &length)?;
        let ages: Vec<u8> = bios.iter().map(|bio| bio.age).collect();
        tx.rest_array_out("ages", &ages)
    });
}

pub fn telemetry(
    parser: &mut DevicePacketParser,
    massive: &MassiveData,
    errors: u32,
) -> Result<()> {
    parser
        .writer()
        .rest_array_out("env", &[10.0_f32, 20.0, 30.0, 40.0, 50.0])?;
    parser.writer().rest_raw_out("mdt", massive)?;
    if errors != 0 {
        parser.writer().rest_out("rx_errors", &errors)?;
    }
    Ok(())
}
