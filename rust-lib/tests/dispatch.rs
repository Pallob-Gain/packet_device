use packet_device::*;
use std::collections::BTreeMap;
use std::sync::{Arc, Mutex};

fn frame(mut body: Vec<u8>) -> Vec<u8> {
    body.extend_from_slice(&crc16(&body, 0).to_be_bytes());
    let mut bytes = frame_length(body.len() as u16).to_vec();
    bytes.extend(body);
    bytes
}

#[test]
fn text_grammar_dispatches_each_cpp_callback_category() {
    let events = Arc::new(Mutex::new(Vec::<String>::new()));
    let mut parser = DevicePacketParser::new(128).unwrap();
    let cb = events.clone();
    parser.on_command("VNR", move |_| {
        cb.lock().unwrap().push("command".into());
        Ok(())
    });
    let cb = events.clone();
    parser.on_value("SET", move |_, value| {
        cb.lock().unwrap().push(format!("value:{value}"));
        Ok(())
    });
    let cb = events.clone();
    parser.on_param("GET", move |_, param| {
        cb.lock().unwrap().push(format!("param:{param}"));
        Ok(())
    });
    let cb = events.clone();
    parser.on_param_value("PUT", move |_, param, value| {
        cb.lock().unwrap().push(format!("pair:{param}:{value}"));
        Ok(())
    });
    let report = parser.process_bytes(b"VNR\r\nSET=12\r\nGET:temperature\r\nPUT:x=2.5\r\n");
    assert!(report.errors.is_empty());
    assert_eq!(
        *events.lock().unwrap(),
        ["command", "value:12", "param:temperature", "pair:x:2.5"]
    );
}

#[test]
fn binary_text_command_is_exact_name_dispatch_not_parameter_grammar() {
    let exact = Arc::new(Mutex::new(0));
    let mut parser = DevicePacketParser::new(128).unwrap();
    let cb = exact.clone();
    parser.on_command("SET=12", move |_| {
        *cb.lock().unwrap() += 1;
        Ok(())
    });
    parser.on_value("SET", |_, _| {
        panic!("binary command must not use text value grammar")
    });
    let packet = frame(encode_command_body("SET=12").unwrap());
    let report = parser.process_bytes(&packet);
    assert!(report.errors.is_empty());
    assert_eq!(*exact.lock().unwrap(), 1);
}

#[test]
fn callback_can_send_response_immediately() {
    let output = Arc::new(Mutex::new(Vec::new()));
    let out = output.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.set_stream_capture_callback(move |bytes| {
        out.lock().unwrap().extend_from_slice(bytes);
        Ok(())
    });
    parser.on_command("VNR", |writer| {
        writer.rest_out_str("version", "Packet Device")
    });
    let report = parser.process_bytes(b"VNR\r\n");
    assert!(report.errors.is_empty());
    let response = output.lock().unwrap().clone();
    let mut peer = DevicePacketParser::new(128).unwrap();
    let report = peer.process_bytes(&response);
    assert!(report.errors.is_empty());
    assert!(
        matches!(&report.packets[0], Packet::Value { property, type_id: 15, data } if property == "version" && data == b"Packet Device")
    );
}

#[test]
fn raw_callbacks_receive_scalar_and_array_metadata() {
    let received = Arc::new(Mutex::new(Vec::new()));
    let cb = received.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.on_raw("x", move |_, value| {
        cb.lock().unwrap().push((
            value.type_id,
            value.element_size,
            value.count,
            value.data.to_vec(),
        ));
        Ok(())
    });
    let mut bytes = frame(encode_value_body("x", 5, &[0x34, 0x12]).unwrap());
    bytes.extend(frame(encode_array_body("x", 7, 1, 3, &[1, 2, 3]).unwrap()));
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(
        *received.lock().unwrap(),
        [(5, 2, 1, vec![0x34, 0x12]), (7, 1, 3, vec![1, 2, 3])]
    );
}

#[test]
fn typed_callback_precedes_raw_and_accepts_matching_void_width() {
    let values = Arc::new(Mutex::new(Vec::new()));
    let cb = values.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.on_raw("x", |_, _| panic!("typed handler should take precedence"));
    parser.on_receive::<f32, _>("x", move |_, value| {
        cb.lock().unwrap().push(value);
        Ok(())
    });
    let mut bytes = frame(encode_value_body("x", 11, &1.25_f32.to_le_bytes()).unwrap());
    bytes.extend(frame(
        encode_value_body("x", 0, &2.5_f32.to_le_bytes()).unwrap(),
    ));
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(*values.lock().unwrap(), [1.25, 2.5]);
}

#[derive(Debug, PartialEq)]
struct Bio {
    age: u8,
    height: u8,
}

impl WireValue for Bio {
    const TYPE_ID: u8 = 0;
    const WIRE_SIZE: Option<usize> = Some(2);
    fn encode(&self) -> Vec<u8> {
        vec![self.age, self.height]
    }
    fn decode(bytes: &[u8]) -> Result<Self> {
        if bytes.len() != 2 {
            return Err(Error::InvalidLength {
                expected: 2,
                actual: bytes.len(),
            });
        }
        Ok(Self {
            age: bytes[0],
            height: bytes[1],
        })
    }
}

#[test]
fn custom_structure_array_callback_uses_explicit_codec() {
    let received = Arc::new(Mutex::new(Vec::new()));
    let cb = received.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.on_receive_array::<Bio, _>("bio", move |_, values| {
        *cb.lock().unwrap() = values;
        Ok(())
    });
    let bytes = frame(encode_array_body("bio", 0, 2, 2, &[20, 150, 21, 152]).unwrap());
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(
        *received.lock().unwrap(),
        [
            Bio {
                age: 20,
                height: 150
            },
            Bio {
                age: 21,
                height: 152
            }
        ]
    );
}

#[test]
fn whole_packet_interceptor_bypasses_crc_and_command_dispatch() {
    let captured = Arc::new(Mutex::new(Vec::<Vec<u8>>::new()));
    let cb = captured.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.on_command("VNR", |_| panic!("interceptor replaces normal dispatch"));
    parser.on_packet_receive(move |_, bytes| {
        cb.lock().unwrap().push(bytes.to_vec());
        Ok(())
    });
    let mut binary = frame(encode_command_body("VNR").unwrap());
    *binary.last_mut().unwrap() ^= 1;
    let body = binary[9..].to_vec();
    binary.extend_from_slice(b"VNR\r\n");
    let report = parser.process_bytes(&binary);
    assert!(report.errors.is_empty());
    assert_eq!(report.intercepted, 2);
    assert_eq!(*captured.lock().unwrap(), [body, b"VNR".to_vec()]);
}

#[test]
fn handler_error_is_reported_and_next_command_is_processed() {
    let count = Arc::new(Mutex::new(0));
    let cb = count.clone();
    let mut parser = DevicePacketParser::new(128).unwrap();
    parser.on_command("BAD", |_| {
        Err(Error::InvalidPacket("handler rejected command"))
    });
    parser.on_command("OK", move |_| {
        *cb.lock().unwrap() += 1;
        Ok(())
    });
    let report = parser.process_bytes(b"BAD\r\nOK\r\n");
    assert_eq!(report.errors.len(), 1);
    assert_eq!(*count.lock().unwrap(), 1);
}

#[test]
fn oversized_frame_body_is_discarded_without_executing_embedded_command() {
    let count = Arc::new(Mutex::new(0));
    let cb = count.clone();
    let mut parser = DevicePacketParser::new(32).unwrap();
    parser.on_command("VNR", move |_| {
        *cb.lock().unwrap() += 1;
        Ok(())
    });
    let mut bytes = frame_length(40).to_vec();
    bytes.extend_from_slice(b"VNR\r\n");
    bytes.extend_from_slice(&[b'.'; 35]);
    bytes.extend_from_slice(b"VNR\r\n");
    let report = parser.process_bytes(&bytes);
    assert_eq!(report.errors.len(), 1);
    assert_eq!(*count.lock().unwrap(), 1);
}

#[test]
fn receiver_maps_replace_handlers_in_each_category() {
    let events = Arc::new(Mutex::new(Vec::<String>::new()));
    let mut parser = DevicePacketParser::new(256).unwrap();
    parser.on_command("OLD", |_| panic!("old map must be replaced"));
    parser.on_value("SET", |_, _| panic!("old value handler must be replaced"));
    parser.on_param("GET", |_, _| {
        panic!("old parameter handler must be replaced")
    });
    parser.on_param_value("PUT", |_, _, _| panic!("old pair handler must be replaced"));
    parser.on_raw("raw", |_, _| panic!("old raw handler must be replaced"));

    let mut commands = BTreeMap::<String, CommandHandler>::new();
    let cb = events.clone();
    commands.insert(
        "NEW".into(),
        Box::new(move |_| {
            cb.lock().unwrap().push("command".into());
            Ok(())
        }),
    );
    parser.set_receiver_commands(commands);
    let mut values = BTreeMap::<String, TextHandler>::new();
    let cb = events.clone();
    values.insert(
        "SET".into(),
        Box::new(move |_, value| {
            cb.lock().unwrap().push(format!("value:{value}"));
            Ok(())
        }),
    );
    parser.set_receiver_values(values);
    let mut params = BTreeMap::<String, TextHandler>::new();
    let cb = events.clone();
    params.insert(
        "GET".into(),
        Box::new(move |_, param| {
            cb.lock().unwrap().push(format!("param:{param}"));
            Ok(())
        }),
    );
    parser.set_receiver_params(params);
    let mut pairs = BTreeMap::<String, ParamValueHandler>::new();
    let cb = events.clone();
    pairs.insert(
        "PUT".into(),
        Box::new(move |_, param, value| {
            cb.lock().unwrap().push(format!("pair:{param}:{value}"));
            Ok(())
        }),
    );
    parser.set_receiver_param_values(pairs);
    let mut raw = BTreeMap::<String, RawHandler>::new();
    let cb = events.clone();
    raw.insert(
        "raw".into(),
        Box::new(move |_, data| {
            cb.lock().unwrap().push(format!("raw:{}", data.data[0]));
            Ok(())
        }),
    );
    parser.set_receiver_raw(raw);

    let mut bytes = b"OLD\r\nNEW\r\nSET=1\r\nGET:x\r\nPUT:y=2\r\n".to_vec();
    bytes.extend(frame(encode_value_body("raw", 7, &[5]).unwrap()));
    let report = parser.process_bytes(&bytes);
    assert!(report.errors.is_empty());
    assert_eq!(
        *events.lock().unwrap(),
        ["command", "value:1", "param:x", "pair:y:2", "raw:5"]
    );
}
