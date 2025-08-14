# Packet Device

**Packet Device** is a lightweight, event-driven communication protocol built on top of Arduino’s [`Stream`](https://www.arduino.cc/reference/en/language/functions/communication/stream/) interface.  
It provides a **secure, packetized byte-level communication layer** that works over **any Stream-compatible transport** — UART, SPI, I²C/Wire, BluetoothSerial, etc.

Think of it like **HTTP over TCP**:  
- **Stream** acts as the transport layer (UART/SPI/I²C)  
- **Packet Device** acts as the application protocol layer — handling packet framing, CRC integrity checks, and event-based data dispatch.

It also includes a **Node.js integration**, making it possible to connect an Arduino/ESP32 to a PC over serial and exchange structured data securely.

---

## ✨ Features

- Works over **any** `Stream` transport: UART, SPI, I²C/Wire, BluetoothSerial, etc.
- Secure **packetized** byte-level communication with CRC-16 (CCITT-FALSE)
- **Event-driven** architecture for handling incoming data
- Directly map incoming data to **memory pointers**
- Send/receive **arrays, pointers, or raw structures**
- Handles both **command/control** and **bulk data transfer**
- Cross-platform — works with **Arduino** and **Node.js**

---

## 📦 Installation

### Arduino
1. Download or clone this repository:
   ```bash
   git clone https://github.com/Pallob-Gain/packet_device.git
   ```
2. Copy the `Packet_Device` folder into your Arduino `libraries` directory.
3. Restart the Arduino IDE.

### Node.js
```bash
npm install packet_device
```
This will also install the required `serialport` dependency.

---

## 🚀 Quick Start

### 1. Arduino Example (ESP32)

This example sends structured data and responds to requests from a PC.

```cpp
#include "device_packet.h"

DevicePacket<char, MAX_COMMAND_DEFAULT_LEN> packet(&Serial);

void setup() {
  Serial.begin(115200);
  
  // Example: Respond to "VNR" (version request)
  packet.onReceive("VNR", []() {
    packet.restOutStr("version", "1.0.0");
  });

  // Example: Handle incoming structured data on "ULX"
  packet.onReceive("ULX", [](float *data, uint8_t len) {
    Serial.printf("Received amplitude: %.2f, phase: %.2f\n", data[0], data[1]);
  });
}

void loop() {
  packet.readSerialCommand();
  packet.processingQueueCommands();
}
```

---

### 2. Node.js Example (PC Side)

This connects to the ESP32 via serial and sends/receives structured data.

```js
const { PacketDevice, Struct } = require('packet_device');
const { SerialPort } = require('serialport');

const PORT = 'COM3';
const BAUDRATE = 115200;

// Define a struct type
const lockInInfo = Struct.makeType({
  amplitude: Struct.type.float,
  phase: Struct.type.float
});

const packet_device = new PacketDevice('\r\n');

packet_device.onData((err, data) => {
  if (err) return console.error('Data error:', err);
  try {
    console.log('Received ->', PacketDevice.dataParse(data));
  } catch (err) {
    console.error('Data parse error:', err);
  }
});

(async () => {
  let ports = await SerialPort.list();
  let portInfo = ports.find(p => p.path === PORT);
  if (!portInfo) return console.error(`Port ${PORT} not found`);

  const serialport = new SerialPort({ path: PORT, baudRate: BAUDRATE });

  serialport.on('open', async () => {
    packet_device.open(serialport);

    let data = new Struct(lockInInfo);
    data.setJson({ amplitude: 5.0, phase: 0.5 });

    packet_device.println("VNR");
    let version = await packet_device.waitUntillFound(['version'], 2000);
    console.log('version:', version);

    setInterval(() => {
      packet_device.writePacket("ULX", data);
    }, 1000);
  });

  serialport.on('close', () => {
    packet_device.close();
    console.log('Device disconnected.');
  });
})();
```

---

## 📡 How It Works

**Layered architecture:**
```
+----------------------------+
| Application logic          |
| (commands, data handlers)  |
+----------------------------+
| Packet Device protocol     |
| (framing, CRC, events)     |
+----------------------------+
| Arduino Stream transport   |
| (UART, SPI, I²C, etc.)     |
+----------------------------+
```

---

## 🛠 API Overview (Arduino Side)

| Method | Description |
|--------|-------------|
| `onReceive(cmd, callback)` | Register a handler for a command (string, params, or raw buffer). |
| `readSerialCommand()` | Read incoming data from the serial/stream. |
| `processingQueueCommands()` | Process queued commands. |
| `restOut(properties, value)` | Send data with a property name and value. |
| `restArrayOut(properties, array, size)` | Send an array of values. |
| `setBufferMode(bool)` | Switch between raw packet mode and delimited text mode. |

---

## 📜 License
MIT License — see [LICENSE](LICENSE) for details.

---

## 👤 Author
**Pallob K. Gain**  
[GitHub](https://github.com/Pallob-Gain) • [Email](mailto:pallobkgain@gmail.com)
