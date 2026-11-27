//! Wire layouts shared by the Windows and ESP32 Rust examples.
//! Encode every field explicitly: Rust struct layout is not a wire format.

use packet_device::{Error, Result, WireValue};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LockInInfo {
    pub amplitude: f32,
    pub phase: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Bio {
    pub age: u8,
    pub height: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub struct MassiveData {
    pub name: [u8; 100],
    pub other_data: [f32; 200],
}

impl Default for MassiveData {
    fn default() -> Self {
        let mut name = [0; 100];
        let label = b"Pallob Kumar Gain";
        name[..label.len()].copy_from_slice(label);
        Self {
            name,
            other_data: [0.0; 200],
        }
    }
}

impl MassiveData {
    pub fn name_text(&self) -> String {
        let length = self
            .name
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(self.name.len());
        String::from_utf8_lossy(&self.name[..length]).into_owned()
    }
}

fn check_length(bytes: &[u8], expected: usize) -> Result<()> {
    if bytes.len() != expected {
        return Err(Error::InvalidLength {
            expected,
            actual: bytes.len(),
        });
    }
    Ok(())
}

impl WireValue for LockInInfo {
    const TYPE_ID: u8 = 0; // C++ DATA_TYPE_VOID: an explicitly described raw struct
    const WIRE_SIZE: Option<usize> = Some(8);

    fn encode(&self) -> Vec<u8> {
        [self.amplitude.to_le_bytes(), self.phase.to_le_bytes()].concat()
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        check_length(bytes, 8)?;
        Ok(Self {
            amplitude: f32::decode(&bytes[..4])?,
            phase: f32::decode(&bytes[4..])?,
        })
    }
}

impl WireValue for Bio {
    const TYPE_ID: u8 = 0;
    const WIRE_SIZE: Option<usize> = Some(2);

    fn encode(&self) -> Vec<u8> {
        vec![self.age, self.height]
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        check_length(bytes, 2)?;
        Ok(Self {
            age: bytes[0],
            height: bytes[1],
        })
    }
}

impl WireValue for MassiveData {
    const TYPE_ID: u8 = 0;
    const WIRE_SIZE: Option<usize> = Some(900);

    fn encode(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(900);
        bytes.extend_from_slice(&self.name);
        for value in self.other_data {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes
    }

    fn decode(bytes: &[u8]) -> Result<Self> {
        check_length(bytes, 900)?;
        let mut value = Self {
            name: [0; 100],
            other_data: [0.0; 200],
        };
        value.name.copy_from_slice(&bytes[..100]);
        for (destination, encoded) in value
            .other_data
            .iter_mut()
            .zip(bytes[100..].chunks_exact(4))
        {
            *destination = f32::decode(encoded)?;
        }
        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layouts_match_existing_cpp_and_node_examples() {
        let lock = LockInInfo {
            amplitude: 5.0,
            phase: 0.5,
        };
        assert_eq!(lock.encode(), [0, 0, 160, 64, 0, 0, 0, 63]);
        assert_eq!(LockInInfo::decode(&lock.encode()).unwrap(), lock);
        assert_eq!(
            Bio {
                age: 20,
                height: 150
            }
            .encode(),
            [20, 150]
        );
        let massive = MassiveData::default();
        assert_eq!(massive.encode().len(), 900);
        assert_eq!(MassiveData::decode(&massive.encode()).unwrap(), massive);
        assert!(LockInInfo::decode(&[0; 7]).is_err());
        assert!(Bio::decode(&[0; 3]).is_err());
        assert!(MassiveData::decode(&[0; 899]).is_err());
    }
}
