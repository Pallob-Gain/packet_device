# packet_device

`packet_device` is a Node.js library for secure, packetized communication with microcontrollers (Arduino, ESP32, etc.) running the [Packet Device Arduino library](https://github.com/Pallob-Gain/packet_device).

It uses a simple, event-driven architecture to send and receive structured binary data over serial, with CRC-16 validation.

---

## ✨ Features

- Communicate with Arduino/ESP32 over **serial** using the Packet Device protocol
- Send/receive **typed structures** (int, float, arrays, etc.)
- CRC-16 (CCITT-FALSE) data integrity
- Event-based data reception
- Works with any device implementing the Arduino Packet Device library

---

## 📦 Installation

```bash
npm install packet_device
```

This will also install the [`serialport`](https://www.npmjs.com/package/serialport) dependency.

---

## 🚀 Quick Start

### Microcontroller Side (Arduino/ESP32)

Install and flash the [Packet Device Arduino library](https://github.com/Pallob-Gain/packet_device) to your board.  
Example Arduino sketch:

```cpp
#include "Packet_Device.h"
#include "BluetoothSerial.h"

#define MAX_COMMAND_LEN 128
typedef DevicePacket<char, MAX_COMMAND_LEN> PacketProtocol;

PacketProtocol* device_packet = nullptr;

TaskHandle_t systemReceivingTask = nullptr;

#define SENSOR_NUMBERS 5

float sensor_data[SENSOR_NUMBERS] = { 10.0, 20.0, 30.0, 40.0, 50.0 };

// Data transfer definition between DSP and HOST
struct LockInInfo {
  float amplitude;
  float phase;
};

// Callback: Update lock-in info
void updateLockInfoX(LockInInfo* info) {
  device_packet->restOut("amplitude", info->amplitude);
}

// Callback: Check version
void checkVersion() {
  device_packet->restOutStr("version", "Packet Device");
}

// Data receiving thread
void systemReceivingProcess(void* parameter) {
  while (true) {
    // Read incoming commands
    device_packet->readSerialCommand();
    vTaskDelay(pdMS_TO_TICKS(20));  // 20 ms delay
  }

  // If loop breaks, clean up task
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);

  // Create PacketProtocol object with delimiters
  device_packet = new PacketProtocol(&Serial, { '\r', '\n' });

  // Register command callbacks
  device_packet->onReceive("VNR", checkVersion);
  device_packet->onReceive<LockInInfo>("ULX", updateLockInfoX); // Raw mode

  // Create data processing thread
  xTaskCreatePinnedToCore(
    systemReceivingProcess,   // Task function
    "recv",                   // Task name
    10000,                    // Stack size in bytes
    nullptr,                  // Task input parameter
    2,                        // Priority
    &systemReceivingTask,     // Task handle
    0                         // Core ID
  );
}

void loop() {
  device_packet->processingQueueCommands();
  device_packet->restArrayOut<float>("env", sensor_data, SENSOR_NUMBERS);
  delay(500);
}

```

---

### Node.js Side

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

```
+----------------------------+
| Node.js Application        |
| (commands, data handling)  |
+----------------------------+
| packet_device Protocol     |
| (framing, CRC, parsing)    |
+----------------------------+
| Serial Transport           |
| (via serialport)           |
+----------------------------+
```

---

## 🛠 API Overview

| Method | Description |
|--------|-------------|
| `new PacketDevice(delimiter)` | Create a new PacketDevice instance with a delimiter (e.g., `\r\n`). |
| `onData(callback)` | Receive incoming raw or parsed data. |
| `open(serialPort)` | Bind to a `SerialPort` instance. |
| `close()` | Close the connection. |
| `writePacket(cmd, data)` | Send a structured packet. |
| `println(text)` | Send a text command without packet framing. |
| `waitUntillFound(keys, timeout)` | Await a response containing one of the given keys. |

---

## 📜 License
MIT License — see [LICENSE](LICENSE) for details.

---

## 👤 Author
**Pallob K. Gain**  
[GitHub](https://github.com/Pallob-Gain) • [Email](mailto:pallobkgain@gmail.com)
