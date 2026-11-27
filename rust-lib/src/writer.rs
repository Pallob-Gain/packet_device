use std::io;

use crate::{codec::*, Error, Result, WireValue};

/// A sink must consume the complete slice or return an error. One call contains
/// a complete transmission, preventing interleaving of its header and body.
pub type StreamCapture = Box<dyn FnMut(&[u8]) -> io::Result<()> + Send>;

/// Transmit configuration and helpers, also passed to receive callbacks.
pub struct PacketWriter {
    callback: Option<StreamCapture>,
    delimiters: Vec<u8>,
    buffer_mode: bool,
    verify_crc: bool,
    initial_crc: u16,
}

impl Default for PacketWriter {
    fn default() -> Self {
        Self::new(b"\r\n")
    }
}

impl PacketWriter {
    pub fn new(delimiters: &[u8]) -> Self {
        Self {
            callback: None,
            delimiters: delimiters.to_vec(),
            buffer_mode: true,
            verify_crc: true,
            initial_crc: INITIAL_CRC,
        }
    }

    pub fn set_stream_capture_callback<F>(&mut self, callback: F)
    where
        F: FnMut(&[u8]) -> io::Result<()> + Send + 'static,
    {
        self.callback = Some(Box::new(callback));
    }

    pub fn clear_stream_capture_callback(&mut self) {
        self.callback = None;
    }

    pub fn get_buffer_mode(&self) -> bool {
        self.buffer_mode
    }

    pub fn set_buffer_mode(&mut self, state: bool) {
        self.buffer_mode = state;
    }

    /// Matches C++: this switch controls both outgoing CRC and receive verification.
    pub fn set_verify_crc(&mut self, state: bool) {
        self.verify_crc = state;
    }

    pub fn get_verify_crc(&self) -> bool {
        self.verify_crc
    }

    pub fn set_initial_crc(&mut self, seed: u16) {
        self.initial_crc = seed;
    }

    pub fn initial_crc(&self) -> u16 {
        self.initial_crc
    }

    /// Write bytes unchanged, without framing, CRC, or a delimiter.
    pub fn write_to_port(&mut self, bytes: &[u8]) -> Result<()> {
        self.callback.as_mut().ok_or(Error::NoTransport)?(bytes)?;
        Ok(())
    }

    fn transmit(&mut self, mut body: Vec<u8>) -> Result<()> {
        let length = body.len() + if self.verify_crc { CRC16_BYTE_LEN } else { 0 };
        if length > u16::MAX as usize {
            return Err(Error::LimitExceeded(
                "packet body including CRC exceeds 65535 bytes",
            ));
        }
        if self.verify_crc {
            let crc = crc16(&body, self.initial_crc);
            body.extend_from_slice(&crc.to_be_bytes());
        }
        let mut bytes =
            Vec::with_capacity(PACKET_SIGNATURE_LEN + body.len() + self.delimiters.len());
        if self.buffer_mode {
            bytes.extend_from_slice(&frame_length(length as u16));
        }
        bytes.extend_from_slice(&body);
        if !self.buffer_mode {
            bytes.extend_from_slice(&self.delimiters);
        }
        self.write_to_port(&bytes)
    }

    /// Send a plain command plus delimiter, without CRC, as `println` in Node.js.
    pub fn write_text_command(&mut self, command: &str) -> Result<()> {
        let mut bytes = command.as_bytes().to_vec();
        bytes.extend_from_slice(&self.delimiters);
        self.write_to_port(&bytes)
    }

    /// Send a binary command-text body, with the selected framing and CRC.
    pub fn write_command(&mut self, command: &str) -> Result<()> {
        self.transmit(encode_command_body(command)?)
    }

    /// Send an explicitly typed payload. `restRawOut` also uses binary contents
    /// in delimiter mode, matching C++. Empty binary payloads are suppressed.
    pub fn rest_raw_bytes(&mut self, property: &str, type_id: u8, data: &[u8]) -> Result<()> {
        if data.is_empty() {
            return Ok(());
        }
        // The existing C++ transmitter has an 8-bit *total* header argument.
        // Reject names outside its usable range rather than silently truncating.
        if property.len() + TRANSFER_DATA_PARAMS_HEADER_LEN > u8::MAX as usize {
            return Err(Error::LimitExceeded(
                "C++ scalar header supports property names up to 249 bytes",
            ));
        }
        self.transmit(encode_value_body(property, type_id, data)?)
    }

    pub fn rest_raw_out<T: WireValue>(&mut self, property: &str, value: &T) -> Result<()> {
        let data = value.encode();
        if let Some(expected) = T::WIRE_SIZE {
            if data.len() != expected {
                return Err(Error::InvalidLength {
                    expected,
                    actual: data.len(),
                });
            }
        }
        self.rest_raw_bytes(property, T::TYPE_ID, &data)
    }

    pub fn rest_out<T: WireValue>(&mut self, property: &str, value: &T) -> Result<()> {
        if self.buffer_mode {
            self.rest_raw_out(property, value)
        } else {
            let text = value.text(false)?;
            self.rest_text_value(property, &text, T::TYPE_ID == DATA_TYPE_STRING)
        }
    }

    fn rest_text_value(&mut self, property: &str, text: &str, quote: bool) -> Result<()> {
        // Deliberately retain Arduino's JSON-like output, including its lack of
        // escaping. This mode is not a general JSON serializer.
        let body = if quote {
            format!("{{\"{property}\":\"{text}\"}}")
        } else {
            format!("{{\"{property}\":{text}}}")
        };
        self.transmit(body.into_bytes())
    }

    /// Equivalent of C++ `restOut<String, true>`: do not quote the string in text mode.
    pub fn rest_out_unquoted(&mut self, property: &str, value: &str) -> Result<()> {
        if self.buffer_mode {
            self.rest_raw_bytes(property, DATA_TYPE_STRING, value.as_bytes())
        } else {
            self.rest_text_value(property, value, false)
        }
    }

    pub fn rest_array_bytes(
        &mut self,
        property: &str,
        type_id: u8,
        element_size: u8,
        count: u16,
        data: &[u8],
    ) -> Result<()> {
        if data.len() != usize::from(element_size) * usize::from(count) {
            return Err(Error::InvalidLength {
                expected: usize::from(element_size) * usize::from(count),
                actual: data.len(),
            });
        }
        if data.is_empty() {
            return Ok(());
        }
        if property.len() + TRANSFER_DATA_ARRAY_HEADER_LEN > u8::MAX as usize {
            return Err(Error::LimitExceeded(
                "C++ array header supports property names up to 248 bytes",
            ));
        }
        self.transmit(encode_array_body(
            property,
            type_id,
            element_size,
            count,
            data,
        )?)
    }

    pub fn rest_array_out<T: WireValue>(&mut self, property: &str, values: &[T]) -> Result<()> {
        if values.len() > u16::MAX as usize {
            return Err(Error::LimitExceeded("array count exceeds 65535"));
        }
        if !self.buffer_mode {
            let values = values
                .iter()
                .map(|v| v.text(true))
                .collect::<Result<Vec<_>>>()?;
            return self.rest_out_unquoted(property, &format!("[{}]", values.join(",")));
        }
        let size = T::WIRE_SIZE.ok_or(Error::InvalidConfig(
            "array elements must have a fixed wire size",
        ))?;
        if size == 0 || size > u8::MAX as usize {
            return Err(Error::LimitExceeded(
                "array element size must be 1..=255 bytes",
            ));
        }
        let length = size
            .checked_mul(values.len())
            .ok_or(Error::LimitExceeded("array byte length"))?;
        if length > u16::MAX as usize {
            return Err(Error::LimitExceeded("array byte length exceeds 65535"));
        }
        let mut data = Vec::with_capacity(length);
        for value in values {
            let encoded = value.encode();
            if encoded.len() != size {
                return Err(Error::InvalidLength {
                    expected: size,
                    actual: encoded.len(),
                });
            }
            data.extend_from_slice(&encoded);
        }
        self.rest_array_bytes(property, T::TYPE_ID, size as u8, values.len() as u16, &data)
    }

    pub fn rest_out_str(&mut self, property: &str, value: &str) -> Result<()> {
        if self.buffer_mode {
            self.rest_raw_bytes(property, DATA_TYPE_STRING, value.as_bytes())
        } else {
            self.rest_text_value(property, value, true)
        }
    }

    pub fn rest_out_float(&mut self, property: &str, value: f32) -> Result<()> {
        self.rest_out(property, &value)
    }

    /// Uses the ESP-IDF 5.x `int` ID. Modern Xtensa toolchains typedef `int32_t`
    /// as `long`, so C++ `getTypeID<int>()` selects ID 9. Use `rest_out` with an
    /// `i32` for the fixed-width ID 4 used by older toolchains.
    pub fn rest_out_int(&mut self, property: &str, value: i32) -> Result<()> {
        self.rest_out(property, &crate::Int(value))
    }

    /// C++'s hex/bin helpers transmit an ordinary unsigned number, without formatting.
    pub fn rest_out_hex(&mut self, property: &str, value: u32) -> Result<()> {
        self.rest_out(property, &value)
    }

    pub fn rest_out_bin(&mut self, property: &str, value: u32) -> Result<()> {
        self.rest_out(property, &value)
    }

    pub fn rest_out_success(&mut self, value: &str) -> Result<()> {
        self.rest_out_str("payload", value)
    }

    pub fn rest_out_error(&mut self, value: &str) -> Result<()> {
        self.rest_out_str("error", value)
    }
}
