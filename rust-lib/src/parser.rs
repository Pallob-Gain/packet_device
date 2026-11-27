use std::collections::BTreeMap;
use std::io;
use std::ops::{Deref, DerefMut};

use crate::{codec::*, Error, PacketWriter, Result, WireValue};

/// Metadata delivered by the C++ raw callback. For a scalar, element_size is
/// its payload length and count is one. For an array, both come from its header.
#[derive(Clone, Copy, Debug)]
pub struct RawValue<'a> {
    pub data: &'a [u8],
    pub type_id: u8,
    pub element_size: usize,
    pub count: usize,
}

pub type CommandHandler = Box<dyn FnMut(&mut PacketWriter) -> Result<()> + Send>;
pub type TextHandler = Box<dyn FnMut(&mut PacketWriter, &str) -> Result<()> + Send>;
pub type ParamValueHandler = Box<dyn FnMut(&mut PacketWriter, &str, &str) -> Result<()> + Send>;
pub type RawHandler = Box<dyn FnMut(&mut PacketWriter, RawValue<'_>) -> Result<()> + Send>;
pub type PacketHandler = Box<dyn FnMut(&mut PacketWriter, &[u8]) -> Result<()> + Send>;

/// Results from one input chunk. An invalid frame does not prevent later
/// complete frames in the same chunk from being handled.
#[derive(Debug, Default)]
pub struct ProcessReport {
    pub packets: Vec<Packet>,
    pub errors: Vec<Error>,
    pub intercepted: usize,
}

/// Incremental packet framing and immediate callback dispatch.
///
/// A parser has one receive owner (`&mut self`). Use a mutex or a single I/O task
/// to share it across threads. Callbacks receive its separate transmitter and
/// can send replies without acquiring the parser a second time.
pub struct DevicePacketParser {
    capacity: usize,
    buffer: Vec<u8>,
    expected: Option<usize>,
    discard_remaining: usize,
    delimiters: Vec<u8>,
    writer: PacketWriter,
    commands: BTreeMap<String, CommandHandler>,
    values: BTreeMap<String, TextHandler>,
    params: BTreeMap<String, TextHandler>,
    param_values: BTreeMap<String, ParamValueHandler>,
    raw: BTreeMap<String, RawHandler>,
    typed: BTreeMap<String, RawHandler>,
    packet_receiver: Option<PacketHandler>,
}

impl DevicePacketParser {
    pub fn new(buffer_size: usize) -> Result<Self> {
        Self::with_delimiters(buffer_size, b"\r\n")
    }

    /// Pass an empty delimiter to accept only length-framed packets.
    pub fn with_delimiters(buffer_size: usize, delimiters: &[u8]) -> Result<Self> {
        if buffer_size == 0 || buffer_size > u16::MAX as usize {
            return Err(Error::InvalidConfig("receive capacity must be 1..=65535"));
        }
        Ok(Self {
            capacity: buffer_size,
            buffer: Vec::with_capacity(buffer_size),
            expected: None,
            discard_remaining: 0,
            delimiters: delimiters.to_vec(),
            writer: PacketWriter::new(delimiters),
            commands: BTreeMap::new(),
            values: BTreeMap::new(),
            params: BTreeMap::new(),
            param_values: BTreeMap::new(),
            raw: BTreeMap::new(),
            typed: BTreeMap::new(),
            packet_receiver: None,
        })
    }

    pub fn with_crlf(buffer_size: usize, use_crlf: bool) -> Result<Self> {
        Self::with_delimiters(buffer_size, if use_crlf { b"\r\n" } else { b"" })
    }

    pub fn writer(&mut self) -> &mut PacketWriter {
        &mut self.writer
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    /// Clear an incomplete frame after transport byte loss or an application
    /// timeout. The C++ parser has no active timeout; none is imposed here.
    pub fn reset(&mut self) {
        self.buffer.clear();
        self.expected = None;
        self.discard_remaining = 0;
    }

    pub fn set_stream_capture_callback<F>(&mut self, callback: F)
    where
        F: FnMut(&[u8]) -> io::Result<()> + Send + 'static,
    {
        self.writer.set_stream_capture_callback(callback);
    }

    pub fn set_verify_crc(&mut self, state: bool) {
        self.writer.set_verify_crc(state);
    }

    pub fn set_initial_crc(&mut self, seed: u16) {
        self.writer.set_initial_crc(seed);
    }

    pub fn set_buffer_mode(&mut self, state: bool) {
        self.writer.set_buffer_mode(state);
    }

    pub fn get_buffer_mode(&self) -> bool {
        self.writer.get_buffer_mode()
    }

    pub fn on_command<F>(&mut self, name: impl Into<String>, handler: F)
    where
        F: FnMut(&mut PacketWriter) -> Result<()> + Send + 'static,
    {
        self.commands.insert(name.into(), Box::new(handler));
    }

    /// Register `CMD=value` (three-byte command name).
    pub fn on_value<F>(&mut self, name: impl Into<String>, handler: F)
    where
        F: FnMut(&mut PacketWriter, &str) -> Result<()> + Send + 'static,
    {
        self.values.insert(name.into(), Box::new(handler));
    }

    /// Register `CMD:parameter` (three-byte command name).
    pub fn on_param<F>(&mut self, name: impl Into<String>, handler: F)
    where
        F: FnMut(&mut PacketWriter, &str) -> Result<()> + Send + 'static,
    {
        self.params.insert(name.into(), Box::new(handler));
    }

    /// Register `CMD:p=value`. C++ supports a one-byte parameter in this form.
    pub fn on_param_value<F>(&mut self, name: impl Into<String>, handler: F)
    where
        F: FnMut(&mut PacketWriter, &str, &str) -> Result<()> + Send + 'static,
    {
        self.param_values.insert(name.into(), Box::new(handler));
    }

    pub fn on_raw<F>(&mut self, name: impl Into<String>, handler: F)
    where
        F: FnMut(&mut PacketWriter, RawValue<'_>) -> Result<()> + Send + 'static,
    {
        self.raw.insert(name.into(), Box::new(handler));
    }

    pub fn on_receive<T, F>(&mut self, name: impl Into<String>, mut handler: F)
    where
        T: WireValue + 'static,
        F: FnMut(&mut PacketWriter, T) -> Result<()> + Send + 'static,
    {
        self.typed.insert(
            name.into(),
            Box::new(move |tx, raw| {
                if !typed_matches::<T>(raw) {
                    return Ok(());
                }
                // The C++ scalar callback also accepts arrays and sees the first
                // element. Decode that element without casting an unaligned pointer.
                let size = T::WIRE_SIZE.unwrap_or(raw.data.len());
                let bytes = raw.data.get(..size).ok_or(Error::InvalidLength {
                    expected: size,
                    actual: raw.data.len(),
                })?;
                handler(tx, T::decode(bytes)?)
            }),
        );
    }

    pub fn on_receive_array<T, F>(&mut self, name: impl Into<String>, mut handler: F)
    where
        T: WireValue + 'static,
        F: FnMut(&mut PacketWriter, Vec<T>) -> Result<()> + Send + 'static,
    {
        self.typed.insert(
            name.into(),
            Box::new(move |tx, raw| {
                if !typed_matches::<T>(raw) {
                    return Ok(());
                }
                let size = T::WIRE_SIZE.unwrap_or(raw.element_size);
                let expected = size
                    .checked_mul(raw.count)
                    .ok_or(Error::LimitExceeded("typed array byte length"))?;
                if raw.data.len() < expected {
                    return Err(Error::InvalidLength {
                        expected,
                        actual: raw.data.len(),
                    });
                }
                let mut values = Vec::with_capacity(raw.count);
                for index in 0..raw.count {
                    values.push(T::decode(&raw.data[index * size..(index + 1) * size])?);
                }
                handler(tx, values)
            }),
        );
    }

    /// Intercept complete bodies (including any CRC), replacing normal decode
    /// and dispatch. This preserves C++ `onPacketReceive` semantics.
    pub fn on_packet_receive<F>(&mut self, handler: F)
    where
        F: FnMut(&mut PacketWriter, &[u8]) -> Result<()> + Send + 'static,
    {
        self.packet_receiver = Some(Box::new(handler));
    }

    pub fn clear_packet_receiver(&mut self) {
        self.packet_receiver = None;
    }

    pub fn set_receiver_commands(&mut self, receivers: BTreeMap<String, CommandHandler>) {
        self.commands = receivers;
    }

    pub fn set_receiver_values(&mut self, receivers: BTreeMap<String, TextHandler>) {
        self.values = receivers;
    }

    pub fn set_receiver_params(&mut self, receivers: BTreeMap<String, TextHandler>) {
        self.params = receivers;
    }

    pub fn set_receiver_param_values(&mut self, receivers: BTreeMap<String, ParamValueHandler>) {
        self.param_values = receivers;
    }

    pub fn set_receiver_raw(&mut self, receivers: BTreeMap<String, RawHandler>) {
        self.raw = receivers;
    }

    pub fn process_bytes(&mut self, bytes: &[u8]) -> ProcessReport {
        let mut report = ProcessReport::default();
        let mut offset = 0;
        while offset < bytes.len() {
            if self.discard_remaining > 0 {
                let amount = self.discard_remaining.min(bytes.len() - offset);
                self.discard_remaining -= amount;
                offset += amount;
                continue;
            }
            if let Some(expected) = self.expected {
                let amount = (expected - self.buffer.len()).min(bytes.len() - offset);
                self.buffer
                    .extend_from_slice(&bytes[offset..offset + amount]);
                offset += amount;
                if self.buffer.len() == expected {
                    self.expected = None;
                    self.complete(&mut report);
                }
                continue;
            }
            self.buffer.push(bytes[offset]);
            offset += 1;
            // C++ resets its text buffer upon reaching capacity (before matching).
            if self.buffer.len() >= self.capacity {
                self.buffer.clear();
                report
                    .errors
                    .push(Error::LimitExceeded("receive text buffer capacity"));
                continue;
            }
            if self.buffer.len() >= PACKET_SIGNATURE_LEN {
                let start = self.buffer.len() - PACKET_SIGNATURE_LEN;
                if let Some(length) = parse_frame_length(&self.buffer[start..]) {
                    self.buffer.clear();
                    if length < self.capacity {
                        self.expected = Some(length);
                    } else {
                        // Do not reinterpret rejected binary bytes as commands.
                        self.discard_remaining = length;
                        report.errors.push(Error::LimitExceeded(
                            "announced packet must be smaller than receive capacity",
                        ));
                    }
                    continue;
                }
            }
            if !self.delimiters.is_empty() && self.buffer.ends_with(&self.delimiters) {
                self.buffer
                    .truncate(self.buffer.len() - self.delimiters.len());
                self.complete(&mut report);
            }
        }
        report
    }

    fn complete(&mut self, report: &mut ProcessReport) {
        let mut body = std::mem::take(&mut self.buffer);
        if let Some(handler) = &mut self.packet_receiver {
            report.intercepted += 1;
            if let Err(error) = handler(&mut self.writer, &body) {
                report.errors.push(error);
            }
        } else {
            match decode_packet(
                &body,
                self.writer.get_verify_crc(),
                self.writer.initial_crc(),
            ) {
                Ok(packet) => {
                    if let Err(error) = self.dispatch(&packet) {
                        report.errors.push(error);
                    }
                    report.packets.push(packet);
                }
                Err(error) => report.errors.push(error),
            }
        }
        body.clear();
        self.buffer = body;
    }

    fn dispatch(&mut self, packet: &Packet) -> Result<()> {
        match packet {
            Packet::Command(command) => {
                if let Some(handler) = self.commands.get_mut(command) {
                    handler(&mut self.writer)?;
                }
            }
            Packet::Value {
                property,
                type_id,
                data,
            } => {
                self.dispatch_value(
                    property,
                    RawValue {
                        data,
                        type_id: *type_id,
                        element_size: data.len(),
                        count: 1,
                    },
                )?;
            }
            Packet::Array {
                property,
                type_id,
                element_size,
                count,
                data,
            } => {
                self.dispatch_value(
                    property,
                    RawValue {
                        data,
                        type_id: *type_id,
                        element_size: usize::from(*element_size),
                        count: usize::from(*count),
                    },
                )?;
            }
            Packet::Text(bytes) => self.dispatch_text(bytes)?,
        }
        Ok(())
    }

    fn dispatch_value(&mut self, property: &str, raw: RawValue<'_>) -> Result<()> {
        // Registration of a typed handler shadows the raw handler even when
        // the received type does not match, exactly as in C++.
        if let Some(handler) = self.typed.get_mut(property) {
            handler(&mut self.writer, raw)?;
        } else if let Some(handler) = self.raw.get_mut(property) {
            handler(&mut self.writer, raw)?;
        }
        Ok(())
    }

    fn dispatch_text(&mut self, bytes: &[u8]) -> Result<()> {
        fn text(bytes: &[u8]) -> Result<&str> {
            std::str::from_utf8(bytes).map_err(|_| Error::InvalidUtf8)
        }
        if bytes.len() > 6 && bytes[3] == b':' && bytes[5] == b'=' {
            if let Some(handler) = self.param_values.get_mut(text(&bytes[..3])?) {
                handler(&mut self.writer, text(&bytes[4..5])?, text(&bytes[6..])?)?;
            }
        } else if bytes.len() > 4 && bytes[3] == b'=' {
            if let Some(handler) = self.values.get_mut(text(&bytes[..3])?) {
                handler(&mut self.writer, text(&bytes[4..])?)?;
            }
        } else if bytes.len() > 4 && bytes[3] == b':' {
            if let Some(handler) = self.params.get_mut(text(&bytes[..3])?) {
                handler(&mut self.writer, text(&bytes[4..])?)?;
            }
        } else if let Some(handler) = self.commands.get_mut(text(bytes)?) {
            handler(&mut self.writer)?;
        }
        Ok(())
    }
}

fn typed_matches<T: WireValue>(raw: RawValue<'_>) -> bool {
    raw.type_id == T::TYPE_ID
        || (raw.type_id == DATA_TYPE_VOID && T::WIRE_SIZE == Some(raw.element_size))
}

/// Transmit helpers can be called on the parser as well as on `writer()`.
impl Deref for DevicePacketParser {
    type Target = PacketWriter;
    fn deref(&self) -> &Self::Target {
        &self.writer
    }
}

impl DerefMut for DevicePacketParser {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.writer
    }
}
