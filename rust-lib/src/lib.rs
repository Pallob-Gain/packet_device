//! Native Rust implementation of the C++ `DevicePacketParser` interface.
//!
//! The protocol core has no external dependencies. Feed arbitrary received
//! byte chunks to [`DevicePacketParser::process_bytes`] and attach a byte sink
//! with [`DevicePacketParser::set_stream_capture_callback`]. Handlers receive
//! a separate [`PacketWriter`] so they can immediately send a response.
//!
//! ```
//! use packet_device::{DevicePacketParser, Result};
//! # fn main() -> Result<()> {
//! let mut parser = DevicePacketParser::new(2048)?;
//! parser.set_stream_capture_callback(|bytes| {
//!     // Write ALL bytes to the chosen transport here.
//!     assert!(!bytes.is_empty());
//!     Ok(())
//! });
//! parser.on_command("VNR", |tx| tx.rest_out_str("version", "Packet Device"));
//! let report = parser.process_bytes(b"VNR\r\n");
//! assert!(report.errors.is_empty());
//! # Ok(()) }
//! ```

#![forbid(unsafe_code)]

pub mod codec;
mod error;
mod parser;
pub mod value;
mod writer;

pub use codec::*;
pub use error::{Error, Result};
pub use parser::{
    CommandHandler, DevicePacketParser, PacketHandler, ParamValueHandler, ProcessReport,
    RawHandler, RawValue, TextHandler,
};
pub use value::*;
pub use writer::{PacketWriter, StreamCapture};
