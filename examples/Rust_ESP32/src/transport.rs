//! Platform I/O stays outside the protocol library.

use esp_idf_hal::delay::TickType;
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_sys::{EspError, ESP_ERR_TIMEOUT};
use std::io;

fn io_error(error: EspError) -> io::Error {
    io::Error::other(error.to_string())
}

#[cfg(not(feature = "usb-serial-jtag"))]
mod selected {
    use super::*;
    use esp_idf_hal::gpio::AnyIOPin;
    use esp_idf_hal::uart::{config::Config, UartDriver, UartEventPayload};
    use esp_idf_hal::units::Hertz;
    use std::sync::Arc;

    #[derive(Clone)]
    pub struct Transport(Arc<UartDriver<'static>>);

    impl Transport {
        pub fn new() -> io::Result<Self> {
            let peripherals = Peripherals::take().map_err(io_error)?;
            let config = Config::default()
                .baudrate(Hertz(115_200))
                .rx_fifo_size(2048)
                .tx_fifo_size(2048)
                .queue_size(20);
            let driver = UartDriver::new(
                peripherals.uart0,
                peripherals.pins.gpio43,
                peripherals.pins.gpio44,
                None::<AnyIOPin>,
                None::<AnyIOPin>,
                &config,
            )
            .map_err(io_error)?;
            Ok(Self(Arc::new(driver)))
        }

        pub fn read(&self, bytes: &mut [u8]) -> io::Result<usize> {
            let mut lost_data = false;
            if let Some(queue) = self.0.event_queue() {
                while let Some((event, _)) = queue.recv_front(0) {
                    lost_data |= matches!(
                        event.payload(),
                        UartEventPayload::RxBufferFull
                            | UartEventPayload::RxFifoOverflow
                            | UartEventPayload::FrameError
                            | UartEventPayload::ParityError
                    );
                }
            }
            if lost_data {
                self.0.clear_rx().map_err(io_error)?;
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "UART lost receive data",
                ));
            }
            match self.0.read(bytes, TickType::new_millis(50).ticks()) {
                Ok(length) => Ok(length),
                Err(error) if error.code() == ESP_ERR_TIMEOUT => Ok(0),
                Err(error) => {
                    let _ = self.0.clear_rx();
                    Err(io_error(error))
                }
            }
        }

        pub fn write_all(&self, mut bytes: &[u8]) -> io::Result<()> {
            while !bytes.is_empty() {
                let count = self.0.write(bytes).map_err(io_error)?;
                if count == 0 {
                    return Err(io::ErrorKind::WriteZero.into());
                }
                bytes = &bytes[count..];
            }
            Ok(())
        }
    }
}

#[cfg(feature = "usb-serial-jtag")]
mod selected {
    use super::*;
    use esp_idf_hal::usb_serial::{config::Config, UsbSerialDriver};
    use std::sync::{Arc, Mutex};

    #[derive(Clone)]
    pub struct Transport(Arc<Mutex<UsbSerialDriver<'static>>>);

    impl Transport {
        pub fn new() -> io::Result<Self> {
            let peripherals = Peripherals::take().map_err(io_error)?;
            let config = Config::default().rx_buffer_size(2048).tx_buffer_size(2048);
            let driver = UsbSerialDriver::new(
                peripherals.usb_serial,
                peripherals.pins.gpio19,
                peripherals.pins.gpio20,
                &config,
            )
            .map_err(io_error)?;
            Ok(Self(Arc::new(Mutex::new(driver))))
        }

        pub fn read(&self, bytes: &mut [u8]) -> io::Result<usize> {
            let mut driver = self
                .0
                .lock()
                .map_err(|_| io::Error::other("USB driver lock poisoned"))?;
            match driver.read(bytes, TickType::new_millis(50).ticks()) {
                Ok(length) if length <= bytes.len() => Ok(length),
                Ok(_) => Err(io::Error::other(
                    "USB driver returned an invalid read length",
                )),
                Err(error) if error.code() == ESP_ERR_TIMEOUT => Ok(0),
                Err(error) => Err(io_error(error)),
            }
        }

        pub fn write_all(&self, mut bytes: &[u8]) -> io::Result<()> {
            let mut driver = self
                .0
                .lock()
                .map_err(|_| io::Error::other("USB driver lock poisoned"))?;
            while !bytes.is_empty() {
                let count = driver
                    .write(bytes, TickType::new_millis(100).ticks())
                    .map_err(io_error)?;
                if count == 0 {
                    return Err(io::ErrorKind::WriteZero.into());
                }
                if count > bytes.len() {
                    return Err(io::Error::other(
                        "USB driver returned an invalid write length",
                    ));
                }
                bytes = &bytes[count..];
            }
            Ok(())
        }
    }
}

pub use selected::Transport;
