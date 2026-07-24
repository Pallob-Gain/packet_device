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
 *  Modified: 21 July, 2026
 */

#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <type_traits> // For std::is_same
#include <map>
#include <functional>
#include <any>
#include <cstring> // For memcpy()
#include <vector>
#include <memory>

#include "./communication_flags.h"
#include "./basic_flags.h"
#include "./CyclicRedundancyCheck.h"

typedef void (*streamCaptureFunction_t)(const uint8_t *, uint16_t);
typedef void (*PacketReceiverFunction_t)(const uint8_t *, uint16_t);

class DevicePacketParser
{
private:
  bool verify_crc = true;
  uint16_t initial_crc = 0x0000; // for packet device it is zero
  bool response_buffer_mode = true;

  std::map<String, void (*)(String, String)> insert_pram_data_cmnds;
  std::map<String, void (*)(String)> insert_data_cmnds;
  std::map<String, void (*)(String)> get_pram_cmnds;
  std::map<String, void (*)()> get_process_cmnds;

  std::map<String, void (*)(const uint8_t *, uint8_t, uint16_t, uint16_t)> get_response_buff;
  std::map<String, std::function<void(const uint8_t *, uint8_t, uint16_t, uint16_t)>> any_response_buff;

  streamCaptureFunction_t stream_capture_cb = nullptr;
  PacketReceiverFunction_t packet_receiver_cb = nullptr;

  static constexpr uint8_t packet_info[PACKET_SIGNETURE_LEN] = {'<', 0x0F, '-', 0x0F, '*', 0x0F, '-', 0x0F, '>'}; // packet length signeture

  uint8_t *delimeters = nullptr;
  size_t delimeter_len = 0;

  SemaphoreHandle_t receiver_locker = NULL;
  SemaphoreHandle_t writter_locker = NULL;

  uint16_t buffer_size_;
  std::unique_ptr<uint8_t[]> buffer_;
  uint16_t buffer_current_len = 0;
  uint16_t packet_length = 0;
  unsigned long packet_timeout_at = 0;

  void commandProcess(const uint8_t *data, uint16_t len);

  uint16_t getPacketLength(const uint8_t *transfer_buff);
  void updatePacketLength(uint8_t *transfer_buff, uint16_t packet_size);
  void dataOutToSerial(uint8_t *buff, uint16_t size, uint8_t *header = nullptr, uint8_t header_size = 0);
  void dataOutToSerial(String str);

  inline void packetHandler(const uint8_t *data, uint16_t len);

  bool writer_lock();
  bool writer_unlock();
  bool receiver_lock();
  bool receiver_unlock();

  void setDelimeters(const uint8_t *del, size_t len);

public:
  template <uint8_t D>
  DevicePacketParser(uint16_t buffer_size, const uint8_t (&del)[D])
  {
    buffer_size_ = buffer_size;
    buffer_ = std::make_unique<uint8_t[]>(buffer_size_);

    setDelimeters(del, D);

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    receiver_locker = xSemaphoreCreateMutex();
    writter_locker = xSemaphoreCreateMutex();
#endif
  }

  explicit DevicePacketParser(uint16_t buffer_size, bool use_crlf = true)
  {
    buffer_size_ = buffer_size;
    buffer_ = std::make_unique<uint8_t[]>(buffer_size_);

    if (use_crlf)
    {
      static constexpr uint8_t crlf[] = {13, 10};
      setDelimeters(crlf, sizeof(crlf));
    }

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    
    receiver_locker = xSemaphoreCreateMutex();
    writter_locker = xSemaphoreCreateMutex();
#endif
  }

  ~DevicePacketParser()
  {
    if (buffer_ != nullptr)
    {
      writer_lock();
      buffer_.reset();
      writer_unlock();
    }

    delete[] delimeters;

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    vSemaphoreDelete(receiver_locker);
    vSemaphoreDelete(writter_locker);
#endif
  }

  void setStreamCaptureCallback(streamCaptureFunction_t callback);
  void setReceiver(std::map<String, void (*)(String, String)> receivers);
  void setReceiver(std::map<String, void (*)(String)> receivers, bool prams = false);
  void setReceiver(std::map<String, void (*)()> receivers);
  void setReceiver(std::map<String, void (*)(const uint8_t *, uint8_t, uint16_t, uint16_t)> receivers);

  void onReceive(String name, void (*fun)(String, String));
  void onReceive(String name, void (*fun)(String), bool prams = false);
  void onReceive(String name, void (*fun)());
  void onReceive(String name, void (*fun)(const uint8_t *, uint8_t, uint16_t, uint16_t));
  template <typename T>
  void onReceive(String name, std::function<void(T *)> fun);
  template <typename T>
  void onReceive(String name, std::function<void(T *, uint16_t)> fun);
  
  void onPacketReceive(PacketReceiverFunction_t fun);

  void setVerifyCRC(bool state);
  void setInitialCRC(uint16_t crc);
  void processBytes(const uint8_t *all_bytes, size_t len);

  bool getBufferMode();
  void setBufferMode(bool state);
  bool writeToPort(uint8_t *buff, uint16_t size);

  // Template function
  template <typename T>
  void restRawOut(String properties, T *payload);
  // Template function
  template <typename T, bool NSL = false>
  void restOut(String properties, T payload);
  // Template function
  template <typename T>
  void restArrayOut(String properties, T data[], uint16_t data_size);

  void restOutStr(String properties, String payload);
  void restOutFloat(String properties, float payload);
  void restOutInt(String properties, int payload);
  void restOutHex(String properties, uint32_t payload);
  void restOutBin(String properties, uint32_t payload);
  void restOutSuccess(String payload);
  void restOutError(String err);
};

#include "./DevicePacketParser_t.h"
