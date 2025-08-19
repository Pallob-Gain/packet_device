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
#include <type_traits>  // For std::is_same
#include <map>
#include <functional>
#include <any>
#include <cstring>  // For memcpy()


#include <BluetoothSerial.h>
#include <HardwareSerial.h>
#include "./communication_flags.h"

#define MAX_COMMAND_QUEUE_LEN 5  //maximum 5 commands at once (default)
#define MAX_COMMAND_DEFAULT_LEN 128


//Packet Details header: <[]-[]*[]-[]>
#define PACKET_SIGNETURE_DATA_LEN 4
#define PACKET_SIGNETURE_LEN 9

#define CRC_BYTE_LEN 2

template<typename T, uint16_t N>
struct Command_t {
  T data[N];
  uint16_t len = 0;
  bool completed = false;
};

template<typename R, uint16_t N>
class DevicePacket {
private:
  Stream *serial_dev = NULL;
  bool response_buffer_mode = true;
  bool auto_flush = true;
  bool sending_process_busy=false;

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

  static uint8_t packet_info[PACKET_SIGNETURE_LEN];  //packet length signeture
  uint16_t packet_length = 0;
  uint64_t packet_timeout_at = 0;  //packet receving timeout

  R *delimeters;
  bool bulk_read_enabled=false;

  size_t delimeter_len = 0;

  void commandProcess(R *data, uint16_t len);

  template<typename T>
  uint16_t bufferCrcCalculate(T *data, uint16_t len);

  template<typename T>
  bool crcCheck(T *data, uint16_t len);

  uint16_t getPacketLength(uint8_t *transfer_buff);
  void updatePacketLength(uint8_t *transfer_buff, uint16_t packet_size);
  void dataOutToSerial(uint8_t *buff, uint16_t size);
  void dataOutToSerial(String str);

public:
  template<size_t D>
  DevicePacket(Stream *serial, uint8_t receiver_size, const R (&del)[D]) {
    commands_holder = new Command_t<R, N>[receiver_size];
    max_command_queue_length = receiver_size;

    delimeters = new R[D];
    std::memcpy(delimeters, del, D * sizeof(R));  // Copy memory block
    delimeter_len = D;

    serial_dev = serial;
  }

  template<size_t D>
  DevicePacket(Stream *serial, const R (&del)[D])
    : DevicePacket(serial, MAX_COMMAND_QUEUE_LEN, del) {
    //delimeters \r\n
  }

  DevicePacket(Stream *serial, uint8_t receiver_size)
    : DevicePacket(serial, receiver_size, { 13, 10 }) {
    //delimeters \r\n
  }

  DevicePacket(Stream *serial)
    : DevicePacket(serial, MAX_COMMAND_QUEUE_LEN) {
  }

  ~DevicePacket() {
    delete serial_dev, &response_buffer_mode, &auto_flush;
    delete[] commands_holder;
    delete &max_command_queue_length, &current_commands_length, &commpleted_cmd_read;
    delete &insert_pram_data_cmnds, &insert_data_cmnds, &get_pram_cmnds, &get_process_cmnds, &get_response_buff, &any_response_buff;
    delete[] delimeters;
    delete &delimeter_len;
    delete &bulk_read_enabled;
  }


  void enableBulkRead(bool state);

  void setReceiver(std::map<String, void (*)(String, String)> receivers);
  void setReceiver(std::map<String, void (*)(String)> receivers, bool prams = false);
  void setReceiver(std::map<String, void (*)()> receivers);
  void setReceiver(std::map<String, void (*)(R *, uint8_t, uint8_t, uint8_t)> receivers);


  void onReceive(String name, void (*fun)(String, String));
  void onReceive(String name, void (*fun)(String), bool prams = false);
  void onReceive(String name, void (*fun)());
  void onReceive(String name, void (*fun)(R *, uint8_t, uint8_t, uint8_t));
  template<typename T>
  void onReceive(String name, std::function<void(T *)> fun);
  template<typename T>
  void onReceive(String name, std::function<void(T *, uint8_t)> fun);

  void readSerialCommand();
  void processingQueueCommands();

  void setDevicePort(Stream *serial);
  bool getBufferMode();
  void setBufferMode(bool state);
  void setAutoFlush(bool state);
  void flushDataPort();
  bool writeToPort(uint8_t *buff, uint16_t size);

  // Template function
  template<typename T>
  void restRawOut(String properties, T *payload);
  // Template function
  template<typename T, bool NSL = false>
  void restOut(String properties, T payload);
  // Template function
  template<typename T>
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