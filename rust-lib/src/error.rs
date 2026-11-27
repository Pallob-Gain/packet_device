use std::{fmt, io};

pub type Result<T> = std::result::Result<T, Error>;

/// Transport errors and invalid input are reported without unchecked memory access.
#[derive(Debug)]
pub enum Error {
    InvalidConfig(&'static str),
    InvalidPacket(&'static str),
    InvalidCrc,
    InvalidUtf8,
    InvalidLength { expected: usize, actual: usize },
    LimitExceeded(&'static str),
    UnsupportedText,
    NoTransport,
    Io(io::Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidConfig(message) => write!(f, "invalid configuration: {message}"),
            Self::InvalidPacket(message) => write!(f, "invalid packet: {message}"),
            Self::InvalidCrc => f.write_str("CRC verification failed"),
            Self::InvalidUtf8 => f.write_str("command, property, or string is not UTF-8"),
            Self::InvalidLength { expected, actual } => {
                write!(f, "expected {expected} bytes, received {actual}")
            }
            Self::LimitExceeded(message) => write!(f, "protocol limit exceeded: {message}"),
            Self::UnsupportedText => f.write_str("this value has no text representation"),
            Self::NoTransport => f.write_str("no output callback is registered"),
            Self::Io(error) => write!(f, "transport error: {error}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            _ => None,
        }
    }
}

impl From<io::Error> for Error {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}
