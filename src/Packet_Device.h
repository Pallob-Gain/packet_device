/*
 *  Packet_Device Library
 *  ---------------------
 *  Description:
 *    Packet_Device is a lightweight, event-driven communication protocol
 *    built on top of Arduino's Stream interface. It provides a secure,
 *    packet-based layer for transferring raw bytes between two devices
 *    over any underlying transport supported by the Stream API
 *    (e.g., UART, SPI, I²C/Wire, etc.).
 *
 *    Conceptually, it follows a layered design: the Stream interface
 *    operates as the transport layer, while Packet_Device functions
 *    as the application protocol layer, handling packet framing,
 *    data integrity, and event-driven dispatch.
 *
 *  Features:
 *    - Works over any Stream-compatible transport (UART, SPI, I²C, etc.)
 *    - Secure, packetized byte-level communication
 *    - Event-based architecture for handling incoming data efficiently
 *    - Direct mapping of incoming data to any memory location or pointer
 *    - Easy transmission of arrays, pointers, or raw structures
 *    - Flexible enough for both command/control and bulk data transfer
 *
 *  Use Case:
 *    Ideal for connecting two devices over various physical interfaces
 *    with a unified packet protocol, without rewriting the communication
 *    logic for each transport.
 *
 *  Author: Pallob K. Gain <pallobkgain@gmail.com>
 *  License: MIT
 *  Repository: https://github.com/Pallob-Gain/packet_device
 *  Created: 2 Feb, 2025
 *  Modified: 14 Apr, 2025
 */

#ifndef __PACKET_DEVICE__
#define __PACKET_DEVICE__

#include <Arduino.h>
#include <type_traits> // For std::is_same
#include <map>
#include <functional>
#include <any>
#include <cstring> // For memcpy()

#include <BluetoothSerial.h>
#include <HardwareSerial.h>
#include "./communication_flags.h"

#define MAX_COMMAND_QUEUE_LEN 5 // maximum 5 commands at once (default)
#define MAX_COMMAND_DEFAULT_LEN 128

// Packet Details header: <[]-[]*[]-[]>
#define PACKET_SIGNETURE_DATA_LEN 4
#define PACKET_SIGNETURE_LEN 9

#define CRC_BYTE_LEN 2

// CRC 256-entry lookup table for CRC-16/CCITT-FALSE
static const uint16_t crc16_ccitt_tbl[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

template <typename T, uint16_t N>
struct Command_t
{
  T data[N];
  uint16_t len = 0;
  bool completed = false;
};

template <typename R, uint16_t N>
class DevicePacket
{
private:
  Stream *serial_dev = NULL;
  bool response_buffer_mode = true;
  bool auto_flush = true;

  Command_t<R, N> *commands_holder;
  uint8_t max_command_queue_length = 0;
  uint8_t current_commands_length = 0;
  bool commpleted_cmd_read = false;

  std::map<String, void (*)(String, String)> insert_pram_data_cmnds;
  std::map<String, void (*)(String)> insert_data_cmnds;
  std::map<String, void (*)(String)> get_pram_cmnds;
  std::map<String, void (*)()> get_process_cmnds;
  std::map<String, void (*)(R *, uint8_t, uint8_t, uint8_t)> get_response_buff;
  std::map<String, std::function<void(R *, uint8_t, uint8_t, uint8_t)>> any_response_buff;

  static uint8_t packet_info[PACKET_SIGNETURE_LEN]; // packet length signeture
  uint16_t packet_length = 0;
  uint64_t packet_timeout_at = 0; // packet receving timeout

  R *delimeters;
  bool bulk_read_enabled = false;

  size_t delimeter_len = 0;

  SemaphoreHandle_t receiver_locker = NULL;
  SemaphoreHandle_t writter_locker = NULL;

  void commandProcess(R *data, uint16_t len);

  uint16_t getPacketLength(uint8_t *transfer_buff);
  void updatePacketLength(uint8_t *transfer_buff, uint16_t packet_size);
  void dataOutToSerial(uint8_t *buff, uint16_t size);
  void dataOutToSerial(String str);

  void writer_lock();
  void writer_unlock();
  void receiver_lock();
  void receiver_unlock();

  bool queueCheck();
  bool processEachData(R inchar);

public:
  template <size_t D>
  DevicePacket(Stream *serial, uint8_t receiver_size, const R (&del)[D])
  {
    commands_holder = new Command_t<R, N>[receiver_size];
    max_command_queue_length = receiver_size;

    delimeters = new R[D];
    std::memcpy(delimeters, del, D * sizeof(R)); // Copy memory block
    delimeter_len = D;

    serial_dev = serial;

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    receiver_locker = xSemaphoreCreateMutex();
    writter_locker = xSemaphoreCreateMutex();
#endif
  }

  template <size_t D>
  DevicePacket(Stream *serial, const R (&del)[D])
      : DevicePacket(serial, MAX_COMMAND_QUEUE_LEN, del)
  {
    // delimeters \r\n
  }

  DevicePacket(Stream *serial, uint8_t receiver_size)
      : DevicePacket(serial, receiver_size, {13, 10})
  {
    // delimeters \r\n
  }

  DevicePacket(Stream *serial)
      : DevicePacket(serial, MAX_COMMAND_QUEUE_LEN)
  {
  }

  ~DevicePacket()
  {

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    vSemaphoreDelete(receiver_locker);
    vSemaphoreDelete(writter_locker);
#endif

    delete &receiver_locker;
    delete &writter_locker;

    delete serial_dev, &response_buffer_mode, &auto_flush;
    delete[] commands_holder;
    delete &max_command_queue_length, &current_commands_length, &commpleted_cmd_read;
    delete &insert_pram_data_cmnds, &insert_data_cmnds, &get_pram_cmnds, &get_process_cmnds, &get_response_buff, &any_response_buff;
    delete[] delimeters;
    delete &delimeter_len;
    delete &bulk_read_enabled;
  }

  template <typename T>
  static uint16_t getCRC(T *data, uint16_t len);

  template <typename T>
  static bool verifyCRC(T *data, uint16_t len);

  void enableBulkRead(bool state);

  void setReceiver(std::map<String, void (*)(String, String)> receivers);
  void setReceiver(std::map<String, void (*)(String)> receivers, bool prams = false);
  void setReceiver(std::map<String, void (*)()> receivers);
  void setReceiver(std::map<String, void (*)(R *, uint8_t, uint8_t, uint8_t)> receivers);

  void onReceive(String name, void (*fun)(String, String));
  void onReceive(String name, void (*fun)(String), bool prams = false);
  void onReceive(String name, void (*fun)());
  void onReceive(String name, void (*fun)(R *, uint8_t, uint8_t, uint8_t));
  template <typename T>
  void onReceive(String name, std::function<void(T *)> fun);
  template <typename T>
  void onReceive(String name, std::function<void(T *, uint8_t)> fun);

  void processBytes(R *all_bytes, size_t len);
  void feedBytes(R *all_bytes, size_t len);
  void readSerialCommand();
  void processingQueueCommands();

  void setDevicePort(Stream *serial);
  bool getBufferMode();
  void setBufferMode(bool state);
  void setAutoFlush(bool state);
  void flushDataPort();
  bool writeToPort(uint8_t *buff, uint16_t size);

  // Template function
  template <typename T>
  void restRawOut(String properties, T *payload);
  // Template function
  template <typename T, bool NSL = false>
  void restOut(String properties, T payload);
  // Template function
  template <typename T>
  void restArrayOut(String properties, T data[], uint8_t data_size);

  void restOutStr(String properties, String payload);
  void restOutFloat(String properties, float payload);
  void restOutInt(String properties, int payload);
  void restOutHex(String properties, uint32_t payload);
  void restOutBin(String properties, uint32_t payload);
  void restOutSuccess(String payload);
  void restOutError(String err);
};

#include "./Packet_Device_t.h"

#endif