//! Packet Device wire constants, framing, CRC, and packet bodies.
//!
//! Length fields and the CRC trailer are big endian. Numeric payloads are
//! encoded separately by [`crate::value::WireValue`] in ESP32 byte order.

use crate::{Error, Result};

pub const MAX_COMMAND_QUEUE_LEN: usize = 5;
pub const MAX_COMMAND_DEFAULT_LEN: usize = 128;
pub const PACKET_SIGNETURE_DATA_LEN: usize = 4;
pub const PACKET_SIGNETURE_LEN: usize = 9;
pub const PACKET_SIGNATURE_DATA_LEN: usize = PACKET_SIGNETURE_DATA_LEN;
pub const PACKET_SIGNATURE_LEN: usize = PACKET_SIGNETURE_LEN;
pub const TRANSFER_DATA_TEXT_HEADER_LEN: usize = 4;
pub const TRANSFER_DATA_PARAMS_HEADER_LEN: usize = 6;
pub const TRANSFER_DATA_ARRAY_HEADER_LEN: usize = 7;
pub const CRC16_BYTE_LEN: usize = 2;
pub const INITIAL_CRC: u16 = 0x0000;
pub const CRC16_POLYNOMIAL: u16 = 0x1021;
pub const TRANSFER_DATA_BUFFER_SIG: u8 = 0x2a;
pub const BUFFER_TEXT_RESPNOSE: u8 = 0x5e;
pub const BUFFER_PARAM_RESPNOSE: u8 = 0x5f;
pub const BUFFER_ARRY_RESPNOSE: u8 = 0x60;
pub const BUFFER_TEXT_RESPONSE: u8 = BUFFER_TEXT_RESPNOSE;
pub const BUFFER_PARAM_RESPONSE: u8 = BUFFER_PARAM_RESPNOSE;
pub const BUFFER_ARRAY_RESPONSE: u8 = BUFFER_ARRY_RESPNOSE;

pub const DATA_TYPE_VOID: u8 = 0;
pub const DATA_TYPE_UINT64_T: u8 = 1;
pub const DATA_TYPE_INT64_T: u8 = 2;
pub const DATA_TYPE_UINT32_T: u8 = 3;
pub const DATA_TYPE_INT32_T: u8 = 4;
pub const DATA_TYPE_UINT16_T: u8 = 5;
pub const DATA_TYPE_INT16_T: u8 = 6;
pub const DATA_TYPE_UINT8_T: u8 = 7;
pub const DATA_TYPE_INT8_T: u8 = 8;
pub const DATA_TYPE_INT: u8 = 9;
pub const DATA_TYPE_UINT: u8 = 10;
pub const DATA_TYPE_FLOAT: u8 = 11;
pub const DATA_TYPE_DOUBLE: u8 = 12;
pub const DATA_TYPE_LONG: u8 = 13;
pub const DATA_TYPE_ULONG: u8 = 14;
pub const DATA_TYPE_STRING: u8 = 15;
pub const DATA_TYPE_BOOL: u8 = 16;
pub const DATA_TYPE_NULL: u8 = 17;

/// Numeric identifiers defined by the C++ communication flags.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DataType {
    Void = DATA_TYPE_VOID,
    U64 = DATA_TYPE_UINT64_T,
    I64 = DATA_TYPE_INT64_T,
    U32 = DATA_TYPE_UINT32_T,
    I32 = DATA_TYPE_INT32_T,
    U16 = DATA_TYPE_UINT16_T,
    I16 = DATA_TYPE_INT16_T,
    U8 = DATA_TYPE_UINT8_T,
    I8 = DATA_TYPE_INT8_T,
    Int = DATA_TYPE_INT,
    UInt = DATA_TYPE_UINT,
    Float = DATA_TYPE_FLOAT,
    Double = DATA_TYPE_DOUBLE,
    Long = DATA_TYPE_LONG,
    ULong = DATA_TYPE_ULONG,
    String = DATA_TYPE_STRING,
    Bool = DATA_TYPE_BOOL,
    Null = DATA_TYPE_NULL,
}

impl TryFrom<u8> for DataType {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self> {
        Ok(match value {
            DATA_TYPE_VOID => Self::Void,
            DATA_TYPE_UINT64_T => Self::U64,
            DATA_TYPE_INT64_T => Self::I64,
            DATA_TYPE_UINT32_T => Self::U32,
            DATA_TYPE_INT32_T => Self::I32,
            DATA_TYPE_UINT16_T => Self::U16,
            DATA_TYPE_INT16_T => Self::I16,
            DATA_TYPE_UINT8_T => Self::U8,
            DATA_TYPE_INT8_T => Self::I8,
            DATA_TYPE_INT => Self::Int,
            DATA_TYPE_UINT => Self::UInt,
            DATA_TYPE_FLOAT => Self::Float,
            DATA_TYPE_DOUBLE => Self::Double,
            DATA_TYPE_LONG => Self::Long,
            DATA_TYPE_ULONG => Self::ULong,
            DATA_TYPE_STRING => Self::String,
            DATA_TYPE_BOOL => Self::Bool,
            DATA_TYPE_NULL => Self::Null,
            _ => return Err(Error::InvalidPacket("unknown data type")),
        })
    }
}

/// A decoded body. Binary variants contain only their declared payload bytes.
///
/// `Text` represents unstructured, delimited input. Its bytes are left intact:
/// the C++ parser does not verify or remove CRC bytes from plain text commands.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Packet {
    Command(String),
    Value {
        property: String,
        type_id: u8,
        data: Vec<u8>,
    },
    Array {
        property: String,
        type_id: u8,
        element_size: u8,
        count: u16,
        data: Vec<u8>,
    },
    Text(Vec<u8>),
}

/// Calculate the unreflected CRC-16 with polynomial `0x1021` and no final XOR.
/// Pass [`INITIAL_CRC`] for the Packet Device default, or a previous CRC to
/// continue calculating across successive byte slices.
pub fn crc16(data: &[u8], initial: u16) -> u16 {
    let mut crc = initial;
    for &byte in data {
        crc ^= u16::from(byte) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ CRC16_POLYNOMIAL
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Verify a body followed by its two-byte, big-endian CRC trailer.
/// Like the C++ helper, a trailer without any body bytes is rejected.
pub fn verify_crc16(data: &[u8], initial: u16) -> bool {
    if data.len() <= CRC16_BYTE_LEN {
        return false;
    }
    let end = data.len() - CRC16_BYTE_LEN;
    crc16(&data[..end], initial) == u16::from_be_bytes([data[end], data[end + 1]])
}

/// Encode the nine-byte signature. The four length digits are binary nibbles,
/// not printable hexadecimal characters. Length includes a CRC when present.
pub fn frame_length(length: u16) -> [u8; PACKET_SIGNETURE_LEN] {
    [
        b'<',
        ((length >> 12) & 0x0f) as u8,
        b'-',
        ((length >> 8) & 0x0f) as u8,
        b'*',
        ((length >> 4) & 0x0f) as u8,
        b'-',
        (length & 0x0f) as u8,
        b'>',
    ]
}

/// Decode a signature at the start of a byte slice. Zero is not a valid
/// announced packet length, matching the C++ parser's sentinel value.
pub fn parse_frame_length(bytes: &[u8]) -> Option<usize> {
    let signature = bytes.get(..PACKET_SIGNETURE_LEN)?;
    if signature[0] != b'<'
        || signature[2] != b'-'
        || signature[4] != b'*'
        || signature[6] != b'-'
        || signature[8] != b'>'
    {
        return None;
    }
    let mut length = 0usize;
    for index in [1, 3, 5, 7] {
        let nibble = signature[index];
        if nibble > 0x0f {
            return None;
        }
        length = (length << 4) | usize::from(nibble);
    }
    (length != 0).then_some(length)
}

fn utf8(bytes: &[u8]) -> Result<String> {
    std::str::from_utf8(bytes)
        .map(str::to_owned)
        .map_err(|_| Error::InvalidUtf8)
}

/// Decode one packet body after removing its outer nine-byte signature or
/// delimiter. Known binary packet kinds honor the configured CRC setting.
/// Trailing binary bytes beyond the declared content are accepted, as in C++.
pub fn decode_packet(body: &[u8], verify_crc: bool, initial_crc: u16) -> Result<Packet> {
    let header_size = match body.get(..2) {
        Some([TRANSFER_DATA_BUFFER_SIG, BUFFER_TEXT_RESPNOSE]) => TRANSFER_DATA_TEXT_HEADER_LEN,
        Some([TRANSFER_DATA_BUFFER_SIG, BUFFER_PARAM_RESPNOSE]) => TRANSFER_DATA_PARAMS_HEADER_LEN,
        Some([TRANSFER_DATA_BUFFER_SIG, BUFFER_ARRY_RESPNOSE]) => TRANSFER_DATA_ARRAY_HEADER_LEN,
        _ => return Ok(Packet::Text(body.to_vec())),
    };
    let crc_size = if verify_crc { CRC16_BYTE_LEN } else { 0 };
    if body.len() < header_size + crc_size {
        return Err(Error::InvalidPacket("truncated packet header"));
    }
    if verify_crc && !verify_crc16(body, initial_crc) {
        return Err(Error::InvalidCrc);
    }
    let data = &body[..body.len() - crc_size];
    match body[1] {
        BUFFER_TEXT_RESPNOSE => {
            let length = usize::from(u16::from_be_bytes([data[2], data[3]]));
            let command = data
                .get(header_size..header_size + length)
                .ok_or(Error::InvalidPacket("truncated command"))?;
            Ok(Packet::Command(utf8(command)?))
        }
        BUFFER_PARAM_RESPNOSE => {
            let property_end = header_size + usize::from(data[3]);
            let length = usize::from(u16::from_be_bytes([data[4], data[5]]));
            let property = data
                .get(header_size..property_end)
                .ok_or(Error::InvalidPacket("truncated property name"))?;
            let value = data
                .get(property_end..property_end + length)
                .ok_or(Error::InvalidPacket("truncated value"))?;
            Ok(Packet::Value {
                property: utf8(property)?,
                type_id: data[2],
                data: value.to_vec(),
            })
        }
        BUFFER_ARRY_RESPNOSE => {
            let element_size = data[3];
            let property_end = header_size + usize::from(data[4]);
            let count = u16::from_be_bytes([data[5], data[6]]);
            let length = usize::from(element_size) * usize::from(count);
            let property = data
                .get(header_size..property_end)
                .ok_or(Error::InvalidPacket("truncated property name"))?;
            let values = data
                .get(property_end..property_end + length)
                .ok_or(Error::InvalidPacket("truncated array"))?;
            Ok(Packet::Array {
                property: utf8(property)?,
                type_id: data[2],
                element_size,
                count,
                data: values.to_vec(),
            })
        }
        _ => unreachable!("packet kind was checked above"),
    }
}

fn checked_property_len(property: &str) -> Result<u8> {
    u8::try_from(property.len())
        .map_err(|_| Error::LimitExceeded("property name exceeds 255 bytes"))
}

fn checked_body_len(length: usize) -> Result<()> {
    if length > usize::from(u16::MAX) {
        Err(Error::LimitExceeded("packet body exceeds 65535 bytes"))
    } else {
        Ok(())
    }
}

/// Encode a binary command body, without an outer signature or CRC.
pub fn encode_command_body(command: &str) -> Result<Vec<u8>> {
    let length = u16::try_from(command.len())
        .map_err(|_| Error::LimitExceeded("command exceeds 65535 bytes"))?;
    let body_len = TRANSFER_DATA_TEXT_HEADER_LEN + command.len();
    checked_body_len(body_len)?;
    let mut body = Vec::with_capacity(body_len);
    body.extend_from_slice(&[TRANSFER_DATA_BUFFER_SIG, BUFFER_TEXT_RESPNOSE]);
    body.extend_from_slice(&length.to_be_bytes());
    body.extend_from_slice(command.as_bytes());
    Ok(body)
}

/// Encode a named scalar, string, or structure body, without framing or CRC.
pub fn encode_value_body(property: &str, type_id: u8, data: &[u8]) -> Result<Vec<u8>> {
    let property_len = checked_property_len(property)?;
    let length =
        u16::try_from(data.len()).map_err(|_| Error::LimitExceeded("value exceeds 65535 bytes"))?;
    let body_len = TRANSFER_DATA_PARAMS_HEADER_LEN + property.len() + data.len();
    checked_body_len(body_len)?;
    let mut body = Vec::with_capacity(body_len);
    body.extend_from_slice(&[
        TRANSFER_DATA_BUFFER_SIG,
        BUFFER_PARAM_RESPNOSE,
        type_id,
        property_len,
    ]);
    body.extend_from_slice(&length.to_be_bytes());
    body.extend_from_slice(property.as_bytes());
    body.extend_from_slice(data);
    Ok(body)
}

/// Encode an array body. The supplied data must contain exactly `count`
/// elements of `element_size` bytes each. Framing and CRC are not included.
pub fn encode_array_body(
    property: &str,
    type_id: u8,
    element_size: u8,
    count: u16,
    data: &[u8],
) -> Result<Vec<u8>> {
    let property_len = checked_property_len(property)?;
    let expected = usize::from(element_size) * usize::from(count);
    if data.len() != expected {
        return Err(Error::InvalidLength {
            expected,
            actual: data.len(),
        });
    }
    let body_len = TRANSFER_DATA_ARRAY_HEADER_LEN + property.len() + data.len();
    checked_body_len(body_len)?;
    let mut body = Vec::with_capacity(body_len);
    body.extend_from_slice(&[
        TRANSFER_DATA_BUFFER_SIG,
        BUFFER_ARRY_RESPNOSE,
        type_id,
        element_size,
        property_len,
    ]);
    body.extend_from_slice(&count.to_be_bytes());
    body.extend_from_slice(property.as_bytes());
    body.extend_from_slice(data);
    Ok(body)
}
