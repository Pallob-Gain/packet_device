#include "./Packet_Device.h"
#include <algorithm>

template<typename R, uint16_t N>
void DevicePacket<R, N>::setReceiver(std::map<String, void (*)(String, String)> receivers) {
  insert_pram_data_cmnds = receivers;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setReceiver(std::map<String, void (*)(String)> receivers, bool prams) {
  if (prams) get_pram_cmnds = receivers;
  else insert_data_cmnds = receivers;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setReceiver(std::map<String, void (*)()> receivers) {
  get_process_cmnds = receivers;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setReceiver(std::map<String, void (*)(R *, uint8_t, uint8_t, uint8_t)> receivers) {
  get_response_buff = receivers;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::onReceive(String name, void (*fun)(String, String)) {
  insert_pram_data_cmnds[name] = fun;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::onReceive(String name, void (*fun)(String), bool prams) {
  if (prams) get_pram_cmnds[name] = fun;
  else insert_data_cmnds[name] = fun;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::onReceive(String name, void (*fun)()) {
  get_process_cmnds[name] = fun;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::onReceive(String name, void (*fun)(R *, uint8_t, uint8_t, uint8_t)) {
  get_response_buff[name] = fun;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::commandProcess(R *data, uint16_t len) {

  //Serial.println("Receive:" + String(len));
  //Serial.flush();

  if (len >= 5 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_TEXT_RESPNOSE) {  //with crc
    if (crcCheck<uint8_t>((uint8_t *)data, len)) {
      len -= 2;  //reduce crc
      uint8_t data_len = data[2];
      if (data_len + 3 <= len) {
        String text = String(data + 3, data_len);
        if (get_process_cmnds.find(text) != get_process_cmnds.end()) {
          // Call the function if the key is found
          get_process_cmnds[text]();
        }
      }
    }
  } else if (len >= 7 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_PARAM_RESPNOSE) {
    //Serial.println("Prams:"+String(len)+",type:"+String((uint8_t)data[4]));
    if (crcCheck<uint8_t>((uint8_t *)data, len)) {
      len -= 2;  //reduce crc
      uint8_t data_type = data[2];
      uint8_t pram_len = data[3];
      uint8_t data_len = data[4];

      //Serial.println("data_type:"+String(data_type)+",pram_len:"+String(pram_len)+",data_len:"+String(data_len));

      if (pram_len + data_len + 5 <= len) {
        String param = String(data + 5, pram_len);

        //Serial.println("Data Pram-->"+param);

        // for(uint8_t i=5 + pram_len;i<len;i++){
        //   Serial.print(" "+String(data[i],HEX));
        // }
        // Serial.println();

        if (any_response_buff.find(param) != any_response_buff.end()) {
          any_response_buff[param](data + (5 + pram_len), data_type, data_len, 1);
        } else if (get_response_buff.find(param) != get_response_buff.end()) {
          // Call the function if the key is found
          get_response_buff[param](data + (5 + pram_len), data_type, data_len, 1);
        }
      }
    }
  } else if (len >= 8 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_ARRY_RESPNOSE) {
    //Serial.println("Array:"+String(len));
    if (crcCheck<uint8_t>((uint8_t *)data, len)) {
      len -= 2;  //reduce crc
      uint8_t data_type = data[2];
      uint8_t type_size = data[3];
      uint8_t pram_len = data[4];
      uint8_t data_size = data[5];
      uint8_t data_len = type_size * data_size;
      if (pram_len + data_len + 6 <= len) {
        String param = String(data + 6, pram_len);
        //Serial.println(param);
        //Serial.println(get_response_buff.size());
        if (any_response_buff.find(param) != any_response_buff.end()) {
          any_response_buff[param](data + (6 + pram_len), data_type, type_size, data_size);
        } else if (get_response_buff.find(param) != get_response_buff.end()) {
          // Call the function if the key is found
          get_response_buff[param](data + (6 + pram_len), data_type, type_size, data_size);
        }
      }
    }
  } else {
    String cmd = String(data, len);
    uint16_t cmd_len = cmd.length();

    //Serial.println(cmd);
    //Serial.flush();

    if (cmd_len > 6 && cmd[3] == ':' && cmd[5] == '=') {
      //for COMMAND:PRAM=DATA
      //if value setting command happen
      String f_cmd = cmd.substring(0, 3);
      // Check if the key exists in the map
      if (insert_pram_data_cmnds.find(f_cmd) != insert_pram_data_cmnds.end()) {

        String m_cmd = cmd.substring(4, 5);
        String s_cmd = cmd.substring(6);

        // Call the function if the key is found
        insert_pram_data_cmnds[f_cmd](m_cmd, s_cmd);
      }
    } else if (cmd_len > 4 && cmd[3] == '=') {
      //for COMMAND=DATA
      //if value setting command happen
      String f_cmd = cmd.substring(0, 3);

      if (insert_data_cmnds.find(f_cmd) != insert_data_cmnds.end()) {

        String s_cmd = cmd.substring(4);
        // Call the function if the key is found
        insert_data_cmnds[f_cmd](s_cmd);
      }
    } else if (cmd_len > 4 && cmd[3] == ':') {
      //for COMMAND:PRAM
      //if value setting command happen
      String f_cmd = cmd.substring(0, 3);

      if (get_pram_cmnds.find(f_cmd) != get_pram_cmnds.end()) {
        String s_cmd = cmd.substring(4);
        // Call the function if the key is found
        get_pram_cmnds[f_cmd](s_cmd);
      }
    } else {
      if (get_process_cmnds.find(cmd) != get_process_cmnds.end()) {
        // Call the function if the key is found
        get_process_cmnds[cmd]();
      }
    }
  }
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::readSerialCommand() {
  //command receiving from receiving thread

  if (commpleted_cmd_read) {
    current_commands_length = 0;  //it is restriction to write a variable from two different thread
    commpleted_cmd_read = false;
    packet_length = 0;
  }

  if (current_commands_length >= max_command_queue_length) return;  //if the queue is full then we will not process any receving buffer untill the queue read


  if (packet_timeout_at != 0 && packet_length != 0 && millis() > packet_timeout_at) {
    Command_t<R, N> *cmd = &(commands_holder[current_commands_length]);

    //Serial.println("timeout:"+String(cmd->len)+",t:"+String( millis()-packet_timeout_at));

    cmd->len = 0;
    packet_length = 0;      //reset packet receiveing
    packet_timeout_at = 0;  //reset the time checker, and
  }

  while (serial_dev->available()) {
    R inchar = serial_dev->read();

    //Serial.println(inchar,HEX);

    Command_t<R, N> *cmd = &(commands_holder[current_commands_length]);

    //store data
    cmd->data[cmd->len] = inchar;
    cmd->len++;

    if (cmd->len >= N) {
      cmd->len = 0;
    }

    if (packet_length != 0) {
      //packet receiving mode
      if (cmd->len == packet_length) {
        //Serial.println("Data:"+String(cmd->data[0],HEX));

        //reset the packet receive
        packet_length = 0;
        //packet is ready for process
        packet_timeout_at = 0;  //reset timeout

        cmd->completed = true;                                           //mark it as completed
        current_commands_length++;                                       //store for the next
        if (current_commands_length >= max_command_queue_length) break;  //if the queue if full then not process any more receive
      }
    } else {
      //non macket mode
      if (cmd->len >= PACKET_SIGNETURE_LEN) {
        size_t offset = cmd->len - PACKET_SIGNETURE_LEN;

        //Serial.println("offset:"+String(offset)+",len:"+String(cmd->len)+",l:"+String(PACKET_SIGNETURE_LEN));
        uint16_t packet_size = getPacketLength((uint8_t *)(cmd->data + offset));

        if (packet_size != 0) {
          //valid match
          cmd->len = 0;  //reset buffer index for making ready to receive actual buffer
          if (packet_size < N) {
            //Serial.println("Received:"+String(packet_size)+",l:"+String(current_commands_length));
            //packet size is valid
            packet_length = packet_size;  //update packet size
            //register current time to register timeout of receving data
            packet_timeout_at = millis() + (packet_size * 2) + 100;  //minimum baud rate could 4800bps that mean 600bytes for second, considering 2ms for each of byte, and some extra delay (100ms)
          }
          continue;  //process from next
        }
      }

      if (cmd->len >= delimeter_len) {
        size_t offset = cmd->len - delimeter_len;
        //if data match for deliemter
        //if(std::equal(cmd->data+offset,cmd->data+cmd->len,delimeters)){
        if (memcmp(cmd->data + offset, delimeters, delimeter_len) == 0) {
          //reset the packet receive
          packet_length = 0;

          cmd->len = offset;  //orginal data length
          cmd->completed = true;
          current_commands_length++;                                       //store for the next
          if (current_commands_length >= max_command_queue_length) break;  //if the queue if full then not process any more receive
        }
      }
    }
  }

  //Serial.println();
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::processingQueueCommands() {
  //command process from listening thread
  if (current_commands_length > 0 && commpleted_cmd_read == false) {  //if only the queue has data
    for (uint8_t i = 0; i < current_commands_length; i++) {
      Command_t<R, N> *cmd = &(commands_holder[i]);
      if (cmd->completed) {
        commandProcess(cmd->data, cmd->len);  //process the command
        cmd->completed = false;
      }
      //restore default: when writing to that it is ensure that other thread is not writing in this
      cmd->len = 0;
    }
    //current_commands_length = 0;
    commpleted_cmd_read = true;
  }
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setDevicePort(Stream *serial) {
  serial_dev = serial;
}

template<typename R, uint16_t N>
bool DevicePacket<R, N>::getBufferMode() {
  return response_buffer_mode;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setBufferMode(bool state) {
  response_buffer_mode = state;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::setAutoFlush(bool state) {
  auto_flush = state;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::flushDataPort() {
  serial_dev->flush();
}

template<typename R, uint16_t N>
bool DevicePacket<R, N>::writeToPort(uint8_t *buff, uint16_t size) {
//thread safe write
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
  //if already busy then wait untill free: nessary for RTOS
  while (sending_process_busy) {
    vTaskDelay(pdMS_TO_TICKS(1));  //delay for 1ms to not hog the process
  }
  //register flag as true
  sending_process_busy = true;
#endif

  serial_dev->write(buff, size);
  if (auto_flush) flushDataPort();

#if defined(ARDUINO_ARCH_ESP32)  || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
  sending_process_busy = false;
#endif
  return true;
}

template<typename R, uint16_t N>
uint16_t DevicePacket<R, N>::getPacketLength(uint8_t *transfer_buff) {
  if (transfer_buff[0] != packet_info[0] || transfer_buff[PACKET_SIGNETURE_LEN - 1] != packet_info[PACKET_SIGNETURE_LEN - 1]) return 0;

  uint16_t packet_size = 0;
  for (uint8_t i = 1; i < (PACKET_SIGNETURE_LEN - 1); i++) {
    if (packet_info[i] == 0x0F) {
      //data
      if (transfer_buff[i] > 0x0F) {
        //if segment is invalid
        return 0;
      }
      packet_size = (packet_size << 4) | transfer_buff[i];
    } else if (packet_info[i] != transfer_buff[i]) {
      //format is not matching
      return 0;
    }
  }

  return packet_size;
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::updatePacketLength(uint8_t *transfer_buff, uint16_t packet_size) {
  memcpy(transfer_buff, packet_info, PACKET_SIGNETURE_LEN);

  //{ (packet_size & 0xF000) >> 12, (packet_size & 0x0F00) >> 8, (packet_size & 0x00F0) >> 4, packet_size & 0x000F }
  for (uint8_t i = 0; i < PACKET_SIGNETURE_DATA_LEN; i++) {
    transfer_buff[(i * 2) + 1] = (packet_size >> (12 - (i * 4))) & 0x0F;
  }
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::dataOutToSerial(uint8_t *buff, uint16_t size) {
  uint16_t crc = bufferCrcCalculate<uint8_t>(buff, size);
  uint8_t crc_bytes[CRC_BYTE_LEN] = { (uint8_t)(crc >> 8), (uint8_t)crc };
  uint16_t packet_size = size + CRC_BYTE_LEN;

  if (response_buffer_mode) {

    uint16_t buff_size = PACKET_SIGNETURE_LEN + packet_size;
    uint8_t transfer_buff[buff_size];

    updatePacketLength(transfer_buff, packet_size);
    memcpy(transfer_buff + PACKET_SIGNETURE_LEN, buff, size);
    memcpy(transfer_buff + (PACKET_SIGNETURE_LEN + size), crc_bytes, CRC_BYTE_LEN);

    writeToPort(transfer_buff, buff_size);

  } else {
    uint16_t buff_size = packet_size + delimeter_len;
    uint8_t transfer_buff[buff_size];

    memcpy(transfer_buff, buff, size);
    memcpy(transfer_buff + size, crc_bytes, CRC_BYTE_LEN);
    memcpy(transfer_buff + (size + CRC_BYTE_LEN), delimeters, delimeter_len);

    writeToPort(transfer_buff, buff_size);
  }
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::dataOutToSerial(String str) {
  // uint16_t str_len=str.length();
  // uint8_t buff[str_len+1];
  // str.toCharArray((char *)buff,str_len+1);
  dataOutToSerial((uint8_t *)str.c_str(), str.length());
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutStr(String properties, String payload) {
  restOut(properties, payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutFloat(String properties, float payload) {
  restOut(properties, payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutInt(String properties, int payload) {
  restOut(properties, payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutHex(String properties, uint32_t payload) {
  restOut(properties, payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutBin(String properties, uint32_t payload) {
  restOut(properties, payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutSuccess(String payload) {
  restOutStr("payload", payload);
}

template<typename R, uint16_t N>
void DevicePacket<R, N>::restOutError(String err) {
  restOutStr("error", err);
}


// Explicit instantiation for specific types
template class DevicePacket<char, MAX_COMMAND_DEFAULT_LEN>;  // Instantiating DevicePacket<char, MAX_COMMAND_DEFAULT_LEN>
