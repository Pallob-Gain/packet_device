//! Explicit conversion between Rust values and ESP32-compatible wire bytes.
//!
//! Implement [`WireValue`] for application structures by encoding fields in
//! their C++ order, including any agreed padding. No arbitrary pointer casts or
//! Rust object-memory copies are used by this module.

use crate::codec::*;
use crate::{Error, Result};

/// A value that can be sent as a Packet Device scalar or array element.
///
/// `WIRE_SIZE` is `None` for variable-length values such as strings. Fixed-size
/// implementations must always encode exactly that many bytes. Custom C++
/// structures normally use [`DATA_TYPE_VOID`] and explicitly define their
/// field layout in `encode` and `decode`.
pub trait WireValue: Sized {
    const TYPE_ID: u8;
    const WIRE_SIZE: Option<usize>;

    fn encode(&self) -> Vec<u8>;
    fn decode(bytes: &[u8]) -> Result<Self>;

    /// Arduino-style value text, without JSON quoting or a property name.
    /// Float arrays use five decimal places; other floats use two.
    fn text(&self, _array_element: bool) -> Result<String> {
        Err(Error::UnsupportedText)
    }
}

/// Rust equivalent of the C++ `getTypeID<T>()` helper.
pub const fn get_type_id<T: WireValue>() -> u8 {
    T::TYPE_ID
}

fn fixed_bytes<const N: usize>(bytes: &[u8]) -> Result<[u8; N]> {
    bytes.try_into().map_err(|_| Error::InvalidLength {
        expected: N,
        actual: bytes.len(),
    })
}

macro_rules! integer_value {
    ($type:ty, $id:expr, $size:expr) => {
        impl WireValue for $type {
            const TYPE_ID: u8 = $id;
            const WIRE_SIZE: Option<usize> = Some($size);

            fn encode(&self) -> Vec<u8> {
                self.to_le_bytes().to_vec()
            }

            fn decode(bytes: &[u8]) -> Result<Self> {
                Ok(Self::from_le_bytes(fixed_bytes::<$size>(bytes)?))
            }

            fn text(&self, _array_element: bool) -> Result<String> {
                Ok(self.to_string())
            }
        }
    };
}

integer_value!(u64, DATA_TYPE_UINT64_T, 8);
integer_value!(i64, DATA_TYPE_INT64_T, 8);
integer_value!(u32, DATA_TYPE_UINT32_T, 4);
integer_value!(i32, DATA_TYPE_INT32_T, 4);
integer_value!(u16, DATA_TYPE_UINT16_T, 2);
integer_value!(i16, DATA_TYPE_INT16_T, 2);
integer_value!(u8, DATA_TYPE_UINT8_T, 1);
integer_value!(i8, DATA_TYPE_INT8_T, 1);

fn float_text(value: f64, digits: usize) -> String {
    if value.is_nan() {
        "nan".to_owned()
    } else {
        format!("{value:.digits$}")
    }
}

impl WireValue for f32 {
    const TYPE_ID: u8 = DATA_TYPE_FLOAT;
    const WIRE_SIZE: Option<usize> = Some(4);

    fn encode(&self) -> Vec<u8> {
        self.to_le_bytes().to_vec()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        Ok(Self::from_le_bytes(fixed_bytes::<4>(bytes)?))
    }

    fn text(&self, array_element: bool) -> Result<String> {
        Ok(float_text(
            f64::from(*self),
            if array_element { 5 } else { 2 },
        ))
    }
}

impl WireValue for f64 {
    const TYPE_ID: u8 = DATA_TYPE_DOUBLE;
    const WIRE_SIZE: Option<usize> = Some(8);

    fn encode(&self) -> Vec<u8> {
        self.to_le_bytes().to_vec()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        Ok(Self::from_le_bytes(fixed_bytes::<8>(bytes)?))
    }

    fn text(&self, _array_element: bool) -> Result<String> {
        Ok(float_text(*self, 2))
    }
}

impl WireValue for bool {
    const TYPE_ID: u8 = DATA_TYPE_BOOL;
    const WIRE_SIZE: Option<usize> = Some(1);

    fn encode(&self) -> Vec<u8> {
        vec![u8::from(*self)]
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        Ok(fixed_bytes::<1>(bytes)?[0] != 0)
    }

    fn text(&self, _array_element: bool) -> Result<String> {
        Ok(if *self { "1" } else { "0" }.to_owned())
    }
}

impl WireValue for String {
    const TYPE_ID: u8 = DATA_TYPE_STRING;
    const WIRE_SIZE: Option<usize> = None;

    fn encode(&self) -> Vec<u8> {
        self.as_bytes().to_vec()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        std::str::from_utf8(bytes)
            .map(str::to_owned)
            .map_err(|_| Error::InvalidUtf8)
    }

    fn text(&self, _array_element: bool) -> Result<String> {
        Ok(self.clone())
    }
}

impl<const N: usize> WireValue for [u8; N] {
    const TYPE_ID: u8 = DATA_TYPE_VOID;
    const WIRE_SIZE: Option<usize> = Some(N);

    fn encode(&self) -> Vec<u8> {
        self.to_vec()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        fixed_bytes(bytes)
    }
}

macro_rules! integer_wrapper {
    ($name:ident, $type:ty, $id:expr, $description:literal) => {
        #[doc = $description]
        #[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd, Hash)]
        pub struct $name(pub $type);

        impl WireValue for $name {
            const TYPE_ID: u8 = $id;
            const WIRE_SIZE: Option<usize> = Some(4);

            fn encode(&self) -> Vec<u8> {
                self.0.to_le_bytes().to_vec()
            }

            fn decode(bytes: &[u8]) -> Result<Self> {
                Ok(Self(<$type>::from_le_bytes(fixed_bytes::<4>(bytes)?)))
            }

            fn text(&self, _array_element: bool) -> Result<String> {
                Ok(self.0.to_string())
            }
        }

        impl From<$type> for $name {
            fn from(value: $type) -> Self {
                Self(value)
            }
        }

        impl From<$name> for $type {
            fn from(value: $name) -> Self {
                value.0
            }
        }
    };
}

integer_wrapper!(
    Int,
    i32,
    DATA_TYPE_INT,
    "Explicit type ID 9, with the ESP32 32-bit signed int layout."
);
integer_wrapper!(
    UInt,
    u32,
    DATA_TYPE_UINT,
    "Explicit type ID 10, with the ESP32 32-bit unsigned int layout."
);
integer_wrapper!(
    Long,
    i32,
    DATA_TYPE_LONG,
    "Explicit type ID 13, with the ESP32 32-bit signed long layout."
);
integer_wrapper!(
    ULong,
    u32,
    DATA_TYPE_ULONG,
    "Explicit type ID 14, with the ESP32 32-bit unsigned long layout."
);

/// Explicit null type ID 17, with no payload bytes (as in the Node.js codec).
/// The C++ `null_type` alias itself resolves to its earlier uint8 type check.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Hash)]
pub struct Null;

impl WireValue for Null {
    const TYPE_ID: u8 = DATA_TYPE_NULL;
    const WIRE_SIZE: Option<usize> = Some(0);

    fn encode(&self) -> Vec<u8> {
        Vec::new()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        fixed_bytes::<0>(bytes)?;
        Ok(Self)
    }

    fn text(&self, _array_element: bool) -> Result<String> {
        Ok("null".to_owned())
    }
}
