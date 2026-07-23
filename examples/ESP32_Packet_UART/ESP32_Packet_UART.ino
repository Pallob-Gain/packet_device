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

#define BUFFER_HOLDER_SIZE 2048  //maximum size of packet
#define APP_UART_READ_BUFFER_SIZE 512


static const char *TAG = "UART_APP";

static QueueHandle_t uart_event_queue = NULL;

DevicePacketParser *device_packet = NULL;  //Serial

TaskHandle_t SystemReceivingTask = NULL;

#define SENSOR_NUMBERS 5

float sensor_data[SENSOR_NUMBERS] = { 10.0, 20.0, 30.0, 40.0, 50.0 };

struct MassiveData {
  char name[100] = "Pallob Kumar Gain";
  float other_data[200];
};

MassiveData testData;

//data transfer defination between DSP and HOST
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
  //device_packet->restOutStr("test", "testing");
  //device_packet->restOutStr("test1", "testing");
  device_packet->restOutStr("version", "Packet Device");
}

void uart_send_bytes(const uint8_t *data, uint16_t length) {
  if (data == NULL || length == 0) {
    return;
  }

  uart_write_bytes(
    APP_UART_PORT,
    data,
    length);
}

void process_received_data(const uint8_t *data, uint16_t length) {
  if (data == NULL || length == 0) {
    return;
  }
  device_packet->processBytes(data, length);
}


/**
 * @brief UART receiver event task.
 *
 * This task sleeps inside xQueueReceive() until the UART driver
 * generates an event.
 */
void uart_event_task(void *parameter) {
  uart_event_t event;

  uint8_t *rx_buffer =(uint8_t *) malloc(APP_UART_READ_BUFFER_SIZE);

  if (rx_buffer == NULL) {
    vTaskDelete(NULL);
    return;
  }

  while (true) {
    /*
         * Wait indefinitely for a UART event.
         * This does not continuously poll the UART.
         */
    if (xQueueReceive(
          uart_event_queue,
          &event,
          portMAX_DELAY)
        != pdTRUE) {
      continue;
    }

    switch (event.type) {
      case UART_DATA:
        {
          /*
             * event.size indicates how many bytes caused this event,
             * but limit each read to the allocated local buffer.
             */
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

            process_received_data(
              rx_buffer,bytes_read);

            bytes_remaining -= (size_t)bytes_read;
          }

          break;
        }

      case UART_FIFO_OVF:
        /*
             * Discard unread RX data and reset queued UART events.
             * Data has already been lost when this event occurs.
             */
        uart_flush_input(APP_UART_PORT);
        xQueueReset(uart_event_queue);
        break;

      case UART_BUFFER_FULL:
        uart_flush_input(APP_UART_PORT);
        xQueueReset(uart_event_queue);
        break;

      case UART_BREAK:

        break;

      case UART_PARITY_ERR:

        break;

      case UART_FRAME_ERR:

        break;

      case UART_PATTERN_DET:

        break;

      default:

        break;
    }
  }

  /*
     * The loop currently runs forever, but keep cleanup code
     * in case the implementation changes later.
     */
  free(rx_buffer);
  vTaskDelete(NULL);
}

/**
 * @brief Initialize UART peripheral and driver.
 */
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

  /*
     * Configure baud rate, data bits, parity, stop bits, etc.
     */
  ESP_ERROR_CHECK(
    uart_param_config(
      APP_UART_PORT,
      &uart_config));

  /*
     * Assign UART pins.
     *
     * RTS and CTS are unchanged because hardware flow control
     * is disabled.
     */
  ESP_ERROR_CHECK(
    uart_set_pin(
      APP_UART_PORT,
      APP_UART_TX_PIN,
      APP_UART_RX_PIN,
      UART_PIN_NO_CHANGE,
      UART_PIN_NO_CHANGE));

  /*
     * Install the UART driver.
     *
     * Parameters:
     * 1. UART port
     * 2. RX ring-buffer size
     * 3. TX ring-buffer size
     * 4. UART event queue length
     * 5. Output event-queue handle
     * 6. Interrupt allocation flags
     */
  ESP_ERROR_CHECK(
    uart_driver_install(
      APP_UART_PORT,
      APP_UART_RX_BUFFER_SIZE,
      APP_UART_TX_BUFFER_SIZE,
      APP_UART_EVENT_QUEUE_SIZE,
      &uart_event_queue,
      0));

  return ESP_OK;
}

void setup() {
  //pointer transfer

  device_packet = new DevicePacketParser(BUFFER_HOLDER_SIZE, { '\r', '\n' });  //delimeter passed (it only nessary for the non raw transfer, raw byte are transmit with packet info)
  device_packet->setStreamCaptureCallback(uart_send_bytes);

  device_packet->onReceive("VNR", checkVersion);
  //set on receive function
  device_packet->onReceive<lockInInfo>("ULX", updateLockInfoX);  //raw mode
  device_packet->onReceive<float>("counts", updateCounts);       //raw mode
  device_packet->onReceive<Bio>("bio", updateBio);               //raw mode

  ESP_ERROR_CHECK(uart_initialize());

  ESP_LOGI(
    TAG,
    "UART initialized: port=%d, TX=%d, RX=%d, baud=%d",
    APP_UART_PORT,
    APP_UART_TX_PIN,
    APP_UART_RX_PIN,
    APP_UART_BAUD_RATE);

  BaseType_t result;

  result = xTaskCreate(
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
  delay(500);
}