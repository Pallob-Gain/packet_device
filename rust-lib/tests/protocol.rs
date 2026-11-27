use packet_device::*;
use std::sync::{Arc, Mutex};

fn reference(name: &str) -> Vec<u8> {
    fixture(include_str!("fixtures/nodejs_frames.txt"), name)
}

fn cpp_reference(name: &str) -> Vec<u8> {
    fixture(include_str!("fixtures/cpp_frames.txt"), name)
}

fn fixture(contents: &str, name: &str) -> Vec<u8> {
    let line = contents
        .lines()
        .find(|line| line.starts_with(&format!("{name} ")))
        .expect("reference fixture exists");
    let hex = line.split_whitespace().nth(1).unwrap();
    (0..hex.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).unwrap())
        .collect()
}

#[test]
fn cpp_and_nodejs_agree_on_shared_binary_reference_cases() {
    for name in [
        "scalar_amplitude",
        "scalar_age",
        "scalar_signed",
        "array_counts",
        "struct_ulx",
        "struct_massive",
        "array_600",
    ] {
        assert_eq!(cpp_reference(name), reference(name), "{name}");
    }
}

#[test]
fn nine_hundred_byte_structure_matches_cpp_and_nodejs() {
    let mut data = [0_u8; 900];
    let name = b"Pallob Kumar Gain";
    data[..name.len()].copy_from_slice(name);
    for i in 0..200 {
        let start = 100 + i * 4;
        data[start..start + 4].copy_from_slice(&(i as f32 / 2.0).to_le_bytes());
    }
    let (mut parser, output) = capture();
    parser.writer().rest_raw_out("mdt", &data).unwrap();
    let encoded = output.lock().unwrap().clone();
    assert_eq!(encoded, cpp_reference("struct_massive"));
    assert_eq!(encoded, reference("struct_massive"));
    let report = parser.process_bytes(&encoded);
    assert!(report.errors.is_empty());
    assert!(
        matches!(&report.packets[0], Packet::Value { property, type_id: 0, data: payload } if property == "mdt" && payload == &data)
    );
}

#[test]
fn all_wire_type_ids_and_cpp_alias_wrappers_are_represented() {
    let ids = [
        <[u8; 1]>::TYPE_ID,
        u64::TYPE_ID,
        i64::TYPE_ID,
        u32::TYPE_ID,
        i32::TYPE_ID,
        u16::TYPE_ID,
        i16::TYPE_ID,
        u8::TYPE_ID,
        i8::TYPE_ID,
        Int::TYPE_ID,
        UInt::TYPE_ID,
        f32::TYPE_ID,
        f64::TYPE_ID,
        Long::TYPE_ID,
        ULong::TYPE_ID,
        String::TYPE_ID,
        bool::TYPE_ID,
        Null::TYPE_ID,
    ];
    assert_eq!(ids, std::array::from_fn::<u8, 18, _>(|i| i as u8));
    assert_eq!(Int(-123).encode(), (-123_i32).to_le_bytes());
    assert_eq!(UInt(123).encode(), 123_u32.to_le_bytes());
    assert_eq!(Long(-456).encode(), (-456_i32).to_le_bytes());
    assert_eq!(ULong(456).encode(), 456_u32.to_le_bytes());
    assert_eq!(Long::decode(&(-456_i32).to_le_bytes()).unwrap(), Long(-456));
    assert_eq!(Null::decode(&[]).unwrap(), Null);
    for id in 0..=17 {
        assert_eq!(DataType::try_from(id).unwrap() as u8, id);
    }
    assert!(DataType::try_from(18).is_err());
}

#[test]
fn writer_matches_cpp_string_and_text_output() {
    let (mut parser, output) = capture();
    parser
        .writer()
        .rest_out_str("version", "Packet Device")
        .unwrap();
    assert_eq!(*output.lock().unwrap(), cpp_reference("string_version"));
    output.lock().unwrap().clear();
    parser.set_buffer_mode(false);
    parser
        .writer()
        .rest_raw_out("ULX", &[0_u8, 0, 160, 64, 0, 0, 0, 63])
        .unwrap();
    assert_eq!(*output.lock().unwrap(), cpp_reference("raw_delimited"));
    output.lock().unwrap().clear();
    parser.set_initial_crc(0xffff);
    parser
        .writer()
        .rest_out_str("version", "Packet Device")
        .unwrap();
    assert_eq!(*output.lock().unwrap(), cpp_reference("text_custom_seed"));
    output.lock().unwrap().clear();
    parser.set_verify_crc(false);
    parser
        .writer()
        .rest_array_out("env", &[1.0_f32, 2.5, -3.0])
        .unwrap();
    assert_eq!(*output.lock().unwrap(), cpp_reference("text_float_array"));
}

#[test]
fn convenience_helpers_keep_selected_esp_idf_profile_and_cpp_semantics() {
    let (mut parser, output) = capture();
    parser.writer().rest_out_int("n", -12).unwrap();
    parser.writer().rest_out_hex("h", 255).unwrap();
    parser.writer().rest_out_bin("b", 5).unwrap();
    parser.writer().rest_out_success("done").unwrap();
    parser.writer().rest_out_error("failed").unwrap();
    let bytes = output.lock().unwrap().clone();
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert!(
        matches!(&report.packets[0], Packet::Value { type_id: 9, data, .. } if data == &(-12_i32).to_le_bytes())
    );
    assert!(
        matches!(&report.packets[1], Packet::Value { type_id: 3, data, .. } if data == &255_u32.to_le_bytes())
    );
    assert!(
        matches!(&report.packets[2], Packet::Value { type_id: 3, data, .. } if data == &5_u32.to_le_bytes())
    );
    assert!(
        matches!(&report.packets[3], Packet::Value { property, type_id: 15, data } if property == "payload" && data == b"done")
    );
    assert!(
        matches!(&report.packets[4], Packet::Value { property, type_id: 15, data } if property == "error" && data == b"failed")
    );
}

fn capture() -> (DevicePacketParser, Arc<Mutex<Vec<u8>>>) {
    let bytes = Arc::new(Mutex::new(Vec::new()));
    let output = bytes.clone();
    let mut parser = DevicePacketParser::new(4096).unwrap();
    parser.set_stream_capture_callback(move |chunk| {
        output.lock().unwrap().extend_from_slice(chunk);
        Ok(())
    });
    (parser, bytes)
}

fn frame(mut body: Vec<u8>, crc: Option<u16>) -> Vec<u8> {
    if let Some(seed) = crc {
        let checksum = crc16(&body, seed);
        body.extend_from_slice(&checksum.to_be_bytes());
    }
    let mut result = frame_length(body.len() as u16).to_vec();
    result.extend_from_slice(&body);
    result
}

#[test]
fn crc_known_vectors_and_big_endian_trailer() {
    assert_eq!(crc16(b"123456789", 0), 0x31c3);
    assert_eq!(crc16(b"123456789", 0xffff), 0x29b1);
    assert_eq!(crc16(&[], 0xabcd), 0xabcd);
    let body = reference("command_vnr");
    assert!(verify_crc16(&body[9..], 0));
    let mut corrupt = body[9..].to_vec();
    corrupt[4] ^= 1;
    assert!(!verify_crc16(&corrupt, 0));
    assert!(!verify_crc16(&[], 0));
    assert!(!verify_crc16(&[0, 0], 0));
}

#[test]
fn framing_uses_raw_nibbles_and_rejects_bad_signatures() {
    assert_eq!(
        frame_length(0x1234),
        [b'<', 1, b'-', 2, b'*', 3, b'-', 4, b'>']
    );
    assert_eq!(parse_frame_length(&frame_length(0x1234)), Some(0x1234));
    assert_eq!(parse_frame_length(b"<1-2*3-4>"), None);
    assert_eq!(parse_frame_length(&[b'<', 1]), None);
    let mut bad = frame_length(123);
    bad[4] = b'!';
    assert_eq!(parse_frame_length(&bad), None);
}

#[test]
fn writer_matches_nodejs_command_scalar_array_and_structure_frames() {
    let (mut parser, output) = capture();
    parser.writer().write_command("VNR").unwrap();
    assert_eq!(*output.lock().unwrap(), reference("command_vnr"));
    output.lock().unwrap().clear();
    parser.writer().rest_out("amplitude", &5.0_f32).unwrap();
    assert_eq!(*output.lock().unwrap(), reference("scalar_amplitude"));
    output.lock().unwrap().clear();
    parser.writer().rest_out("age", &20_u8).unwrap();
    assert_eq!(*output.lock().unwrap(), reference("scalar_age"));
    output.lock().unwrap().clear();
    parser.writer().rest_out("signed", &-123456_i32).unwrap();
    assert_eq!(*output.lock().unwrap(), reference("scalar_signed"));
    output.lock().unwrap().clear();
    parser
        .writer()
        .rest_array_out("counts", &[1.0_f32, 2.5, -3.0])
        .unwrap();
    assert_eq!(*output.lock().unwrap(), reference("array_counts"));
    output.lock().unwrap().clear();
    let lock = [0, 0, 160, 64, 0, 0, 0, 63];
    parser.writer().rest_raw_out("ULX", &lock).unwrap();
    assert_eq!(*output.lock().unwrap(), reference("struct_ulx"));
    output.lock().unwrap().clear();
    parser
        .writer()
        .rest_array_out("bio", &[[20_u8, 150], [21, 152]])
        .unwrap();
    assert_eq!(*output.lock().unwrap(), reference("array_bio"));
}

#[test]
fn array_count_above_511_is_encoded_and_received_correctly() {
    let data: Vec<u8> = (0..600).map(|i| i as u8).collect();
    let (mut parser, output) = capture();
    parser.writer().rest_array_out("large", &data).unwrap();
    let encoded = output.lock().unwrap().clone();
    assert_eq!(encoded, reference("array_600"));
    let report = parser.process_bytes(&encoded);
    assert!(report.errors.is_empty());
    match &report.packets[0] {
        Packet::Array {
            count,
            element_size,
            data: decoded,
            ..
        } => {
            assert_eq!(*count, 600);
            assert_eq!(*element_size, 1);
            assert_eq!(*decoded, data);
        }
        other => panic!("expected array, got {other:?}"),
    }
}

#[test]
fn every_split_boundary_preserves_frames_including_embedded_delimiters() {
    let cases = [
        reference("struct_ulx"),
        reference("array_counts"),
        frame(
            encode_value_body("bytes", 0, b"\r\n<\x01-\x02*\x03-\x04>").unwrap(),
            Some(0),
        ),
    ];
    for packet in cases {
        for split in 0..=packet.len() {
            let mut parser = DevicePacketParser::new(4096).unwrap();
            let first = parser.process_bytes(&packet[..split]);
            let second = parser.process_bytes(&packet[split..]);
            assert!(first.errors.is_empty(), "split={split}");
            assert!(second.errors.is_empty(), "split={split}");
            assert_eq!(
                first.packets.len() + second.packets.len(),
                1,
                "split={split}"
            );
        }
        let mut parser = DevicePacketParser::new(4096).unwrap();
        let mut packets = 0;
        for byte in packet {
            let report = parser.process_bytes(&[byte]);
            assert!(report.errors.is_empty());
            packets += report.packets.len();
        }
        assert_eq!(packets, 1);
    }
}

#[test]
fn concatenated_frames_and_text_are_all_processed() {
    let mut bytes = b"VNR\r\n".to_vec();
    bytes.extend(reference("scalar_age"));
    bytes.extend(reference("array_counts"));
    bytes.extend(reference("delimited_counts"));
    bytes.extend_from_slice(b"RUN\r\n");
    let mut parser = DevicePacketParser::new(4096).unwrap();
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(report.packets.len(), 5);
    assert!(matches!(&report.packets[0], Packet::Text(data) if data == b"VNR"));
    assert!(matches!(&report.packets[3], Packet::Array { property, .. } if property == "counts"));
    assert!(matches!(&report.packets[4], Packet::Text(data) if data == b"RUN"));
}

#[test]
fn crc_failure_does_not_poison_following_frame() {
    let mut bytes = reference("scalar_age");
    *bytes.last_mut().unwrap() ^= 1;
    bytes.extend(reference("array_counts"));
    let mut parser = DevicePacketParser::new(4096).unwrap();
    let report = parser.process_bytes(&bytes);
    assert_eq!(report.errors.len(), 1);
    assert_eq!(report.packets.len(), 1);
    assert!(matches!(&report.packets[0], Packet::Array { property, .. } if property == "counts"));
}

#[test]
fn custom_seed_round_trip_and_wrong_seed_rejection() {
    let (mut sender, output) = capture();
    sender.set_initial_crc(0xffff);
    sender.writer().rest_out("n", &-3_i16).unwrap();
    let bytes = output.lock().unwrap().clone();
    let mut receiver = DevicePacketParser::new(4096).unwrap();
    assert_eq!(receiver.process_bytes(&bytes).errors.len(), 1);
    receiver.set_initial_crc(0xffff);
    let report = receiver.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(report.packets.len(), 1);
}

#[test]
fn crc_disabled_accepts_short_binary_command_and_value() {
    let mut parser = DevicePacketParser::new(4096).unwrap();
    parser.set_verify_crc(false);
    let called = Arc::new(Mutex::new(0));
    let called_cb = called.clone();
    parser.on_command("X", move |_| {
        *called_cb.lock().unwrap() += 1;
        Ok(())
    });
    let command = frame(encode_command_body("X").unwrap(), None);
    let report = parser.process_bytes(&command);
    assert!(report.errors.is_empty());
    assert_eq!(*called.lock().unwrap(), 1);
    let minimal_value = frame(encode_value_body("", 7, &[5]).unwrap(), None);
    let report = parser.process_bytes(&minimal_value);
    assert!(report.errors.is_empty());
    assert_eq!(report.packets.len(), 1);
}

#[test]
fn custom_delimiters_and_no_delimiter_mode() {
    let mut parser = DevicePacketParser::with_delimiters(128, b"END").unwrap();
    assert!(parser.process_bytes(b"VNR\r\n").packets.is_empty());
    let report = parser.process_bytes(b"END");
    assert!(matches!(&report.packets[0], Packet::Text(data) if data == b"VNR\r\n"));
    let mut parser = DevicePacketParser::with_delimiters(128, &[]).unwrap();
    assert!(parser.process_bytes(b"VNR\r\n").packets.is_empty());
    assert_eq!(
        parser
            .process_bytes(&reference("command_vnr"))
            .packets
            .len(),
        1
    );
}

#[test]
fn text_output_retains_crc_before_delimiter_and_custom_seed() {
    let (mut parser, output) = capture();
    parser.set_buffer_mode(false);
    parser.set_initial_crc(0xffff);
    parser
        .writer()
        .rest_out_str("version", "Packet Device")
        .unwrap();
    let json = br#"{"version":"Packet Device"}"#;
    let mut expected = json.to_vec();
    expected.extend_from_slice(&crc16(json, 0xffff).to_be_bytes());
    expected.extend_from_slice(b"\r\n");
    assert_eq!(*output.lock().unwrap(), expected);
    output.lock().unwrap().clear();
    parser.set_verify_crc(false);
    parser
        .writer()
        .rest_array_out("env", &[1.25_f32, -2.5])
        .unwrap();
    assert_eq!(*output.lock().unwrap(), b"{\"env\":[1.25000,-2.50000]}\r\n");
}

#[test]
fn empty_binary_values_keep_existing_cpp_suppression_behavior() {
    let (mut parser, output) = capture();
    parser.writer().rest_out_str("name", "").unwrap();
    parser.writer().rest_array_out::<u8>("data", &[]).unwrap();
    assert!(output.lock().unwrap().is_empty());
}

#[test]
fn transport_errors_are_returned_to_the_caller() {
    let mut parser = DevicePacketParser::new(128).unwrap();
    assert!(parser.writer().rest_out("x", &1_u8).is_err());
    parser.set_stream_capture_callback(|_| {
        Err(std::io::Error::new(
            std::io::ErrorKind::BrokenPipe,
            "disconnected",
        ))
    });
    assert!(parser.writer().rest_out("x", &1_u8).is_err());
}

#[test]
fn reset_discards_an_incomplete_frame() {
    let packet = reference("struct_ulx");
    let mut parser = DevicePacketParser::new(128).unwrap();
    assert!(parser.process_bytes(&packet[..12]).packets.is_empty());
    parser.reset();
    let report = parser.process_bytes(b"VNR\r\n");
    assert!(report.errors.is_empty());
    assert!(matches!(&report.packets[0], Packet::Text(data) if data == b"VNR"));
}

#[test]
fn values_use_fixed_width_little_endian_representation() {
    macro_rules! check {
        ($value:expr, $ty:ty) => {{
            let value: $ty = $value;
            let bytes = value.encode();
            assert_eq!(bytes, value.to_le_bytes());
            assert_eq!(<$ty>::decode(&bytes).unwrap(), value);
            assert!(<$ty>::decode(&bytes[..bytes.len() - 1]).is_err());
        }};
    }
    check!(0x123456789abcdef0, u64);
    check!(-12345678901234, i64);
    check!(0x12345678, u32);
    check!(-123456, i32);
    check!(0x1234, u16);
    check!(-1234, i16);
    check!(201, u8);
    check!(-41, i8);
    check!(1.25, f32);
    check!(-2.5, f64);
    assert_eq!(true.encode(), [1]);
    assert_eq!(false.encode(), [0]);
    assert!(bool::decode(&[1]).unwrap());
    assert_eq!(String::decode(b"hello").unwrap(), "hello");
    assert!(String::decode(&[0xff]).is_err());
    assert_eq!(<[u8; 3]>::decode(&[1, 2, 3]).unwrap(), [1, 2, 3]);
}

#[test]
fn decoder_rejects_truncated_headers_and_inconsistent_array_lengths() {
    for body in [
        &[0x2a, 0x5e][..],
        &[0x2a, 0x5f, 7][..],
        &[0x2a, 0x60, 11][..],
    ] {
        assert!(decode_packet(body, false, 0).is_err());
    }
    let bad_array = [0x2a, 0x60, 11, 4, 1, 0, 2, b'x', 0, 0, 0, 0];
    assert!(decode_packet(&bad_array, false, 0).is_err());
    assert!(encode_value_body(&"p".repeat(256), 7, &[1]).is_err());
    assert!(encode_array_body("x", 11, 4, 2, &[0; 4]).is_err());
}
