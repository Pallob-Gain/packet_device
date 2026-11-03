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

This Arduino `Stream` example sends structured data and responds to requests from a PC.

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

### 2. ESP32 UART Parser Example (latest)

This example uses the ESP-IDF UART driver and event queue to receive bytes, then passes them to `DevicePacketParser`. It demonstrates command callbacks, raw structure/array reception, and transmitting arrays and structures. The complete sketch is available at [`examples/ESP32_Packet_UART/ESP32_Packet_UART.ino`](examples/ESP32_Packet_UART/ESP32_Packet_UART.ino).

```cpp
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"

#include "../../src/CyclicRedundancyCheck.h"
#include "../../src/CyclicRedundancyCheck.cpp"
#include "../../src/DevicePacketParser.h"
#include "../../src/DevicePacketParser.cpp"

#define APP_UART_PORT UART_NUM_0
#define APP_UART_TX_PIN UART_PIN_NO_CHANGE
#define APP_UART_RX_PIN UART_PIN_NO_CHANGE
#define APP_UART_BAUD_RATE 115200

#define APP_UART_RX_BUFFER_SIZE 2048
#define APP_UART_TX_BUFFER_SIZE 2048
#define APP_UART_EVENT_QUEUE_SIZE 20

#define BUFFER_HOLDER_SIZE 2048
#define APP_UART_READ_BUFFER_SIZE 512

static const char *TAG = "UART_APP";
static QueueHandle_t uart_event_queue = NULL;

DevicePacketParser *device_packet = NULL;
TaskHandle_t SystemReceivingTask = NULL;

#define SENSOR_NUMBERS 5
float sensor_data[SENSOR_NUMBERS] = { 10.0, 20.0, 30.0, 40.0, 50.0 };

struct MassiveData {
  char name[100] = "Pallob Kumar Gain";
  float other_data[200];
};

MassiveData testData;

struct lockInInfo {
  float amplitude;
  float phase;
};

struct Bio {
  uint8_t age;
  uint8_t height;
};

void updateLockInfoX(lockInInfo *info) {
  device_packet->restOut("amplitude", info->amplitude);
}

void updateCounts(float *counts, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    device_packet->restOut("count" + String(i), counts[i]);
  }
}

void updateBio(Bio *bio, uint8_t len) {
  device_packet->restOut("len", len);
  uint8_t ages[len];
  for (uint16_t i = 0; i < len; i++) {
    ages[i] = bio[i].age;
  }
  device_packet->restArrayOut<uint8_t>("ages", ages, len);
}

void checkVersion() {
  device_packet->restOutStr("version", "Packet Device");
}

void uart_send_bytes(const uint8_t *data, uint16_t length) {
  if (data == NULL || length == 0) {
    return;
  }
  uart_write_bytes(APP_UART_PORT, data, length);
}

void process_received_data(const uint8_t *data, uint16_t length) {
  if (data == NULL || length == 0) {
    return;
  }
  device_packet->processBytes(data, length);
}

void uart_event_task(void *parameter) {
  uart_event_t event;
  uint8_t *rx_buffer = (uint8_t *) malloc(APP_UART_READ_BUFFER_SIZE);

  if (rx_buffer == NULL) {
    vTaskDelete(NULL);
    return;
  }

  while (true) {
    if (xQueueReceive(uart_event_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    switch (event.type) {
      case UART_DATA: {
        size_t bytes_remaining = event.size;

        while (bytes_remaining > 0) {
          size_t read_length = bytes_remaining;
          if (read_length > APP_UART_READ_BUFFER_SIZE) {
            read_length = APP_UART_READ_BUFFER_SIZE;
          }

          int bytes_read = uart_read_bytes(
            APP_UART_PORT,
            rx_buffer,
            read_length,
            pdMS_TO_TICKS(100));

          if (bytes_read <= 0) {
            break;
          }

          process_received_data(rx_buffer, bytes_read);
          bytes_remaining -= (size_t)bytes_read;
        }
        break;
      }

      case UART_FIFO_OVF:
      case UART_BUFFER_FULL:
        uart_flush_input(APP_UART_PORT);
        xQueueReset(uart_event_queue);
        break;

      case UART_BREAK:
      case UART_PARITY_ERR:
      case UART_FRAME_ERR:
      case UART_PATTERN_DET:
      default:
        break;
    }
  }

  free(rx_buffer);
  vTaskDelete(NULL);
}

static esp_err_t uart_initialize(void) {
  const uart_config_t uart_config = {
    .baud_rate = APP_UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0,
    .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(APP_UART_PORT, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(
    APP_UART_PORT,
    APP_UART_TX_PIN,
    APP_UART_RX_PIN,
    UART_PIN_NO_CHANGE,
    UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(
    APP_UART_PORT,
    APP_UART_RX_BUFFER_SIZE,
    APP_UART_TX_BUFFER_SIZE,
    APP_UART_EVENT_QUEUE_SIZE,
    &uart_event_queue,
    0));

  return ESP_OK;
}

void setup() {
  device_packet = new DevicePacketParser(BUFFER_HOLDER_SIZE, { '\r', '\n' });
  device_packet->setStreamCaptureCallback(uart_send_bytes);

  device_packet->onReceive("VNR", checkVersion);
  device_packet->onReceive<lockInInfo>("ULX", updateLockInfoX);
  device_packet->onReceive<float>("counts", updateCounts);
  device_packet->onReceive<Bio>("bio", updateBio);

  ESP_ERROR_CHECK(uart_initialize());

  ESP_LOGI(
    TAG,
    "UART initialized: port=%d, TX=%d, RX=%d, baud=%d",
    APP_UART_PORT,
    APP_UART_TX_PIN,
    APP_UART_RX_PIN,
    APP_UART_BAUD_RATE);

  BaseType_t result = xTaskCreate(
    uart_event_task,
    "uart_event_task",
    4096,
    NULL,
    10,
    NULL);

  if (result != pdPASS) {
    return;
  }
}

void loop() {
  device_packet->restArrayOut<float>("env", sensor_data, SENSOR_NUMBERS);
  device_packet->restRawOut<MassiveData>("mdt", &testData);
  delay(2500);
}
```

---

### 3. Node.js Example (PC Side)

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
| (framing, CRC, events)      |
+----------------------------+
| Arduino Stream transport   |
| (UART, SPI, I²C, etc.)      |
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
