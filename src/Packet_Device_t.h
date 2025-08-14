template<typename R, uint16_t N>
uint8_t  DevicePacket<R, N>::packet_info[PACKET_SIGNETURE_LEN]={'<',0x0F,'-',0x0F,'*',0x0F,'-',0x0F,'>'}; //packet length signeture

template<typename R, uint16_t N>
template<typename T>
void DevicePacket<R, N>::onReceive(String name, std::function<void(T *)> fun) {
  any_response_buff[name] = [cb = std::move(fun)](R *buffer, uint8_t type, uint8_t type_size, uint8_t len) {
    //Serial.println("Type:"+String(type)+",  retype:"+String(getTypeID<T>())+",  size:"+String(sizeof(T))+",  rsize:"+String(type_size));

    if (buffer && (type == getTypeID<T>() || (type == DATA_TYPE_VOID && sizeof(T) == type_size))) {
      cb((T *)(buffer));  // Safely cast buffer to desired type
    }
  };
}

template<typename R, uint16_t N>
template<typename T>
void DevicePacket<R, N>::onReceive(String name, std::function<void(T *, uint8_t)> fun) {
  any_response_buff[name] = [cb = std::move(fun)](R *buffer, uint8_t type, uint8_t type_size, uint8_t len) {
    if (buffer && (type == getTypeID<T>() || (type == DATA_TYPE_VOID && sizeof(T) == type_size))) {
      cb((T *)(buffer), len);  // Safely cast buffer to desired type
    }
  };
}

// Template function
template<typename R, uint16_t N>
template<typename T>
uint16_t DevicePacket<R, N>::bufferCrcCalculate(T *data, uint16_t len) {
  //CRC-16 (CCITT-FALSE)
  uint16_t crc = 0;

  for (uint16_t i = 0; i < len; i++) {
    T val = data[i];
    // Serial.print(val,HEX);
    crc = crc ^ (val << 8);

    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc = crc << 1;
    }
  }

  // Serial.println();

  return crc;
}

template<typename R, uint16_t N>
template<typename T>
bool DevicePacket<R, N>::crcCheck(T *data, uint16_t len) {
  if (len <= 2) return false;
  uint16_t offset = len - 2;
  uint16_t crc = bufferCrcCalculate<T>(data, offset);

  //Serial.println(String(crc,HEX)+" "+String(crc >> 8,HEX)+" "+String(crc & 0xFF,HEX));
  //Serial.println(String(data[offset],HEX)+" "+String(data[offset+1],HEX));

  return (T)((crc >> 8) & 0xFF) == data[offset] && (T)(crc & 0xFF) == data[offset + 1];
}

template<typename R, uint16_t N>
template<typename T>
void DevicePacket<R, N>::restRawOut(String properties, T *payload) {
  uint8_t data_type = getTypeID<T>();
  uint8_t pram_len = properties.length();
  uint8_t data_len = sizeof(T);
  uint8_t transfer_size = pram_len + data_len + 5;
  uint8_t buff[transfer_size] = { TRANSFER_DATA_BUFFER_SIG, BUFFER_PARAM_RESPNOSE, data_type, pram_len, data_len };  //buff_signeture(1 byte)+data_signeture(1 byte)+pram_len(1 bytes)+data_len(1 bytes)+prams_buff+data_buff
  memcpy(buff + 5, (uint8_t *)properties.c_str(), pram_len);

  memcpy(buff + (5 + pram_len), (uint8_t *)reinterpret_cast<uint8_t *>(payload), data_len);

  dataOutToSerial(buff, transfer_size);
}

template<typename R, uint16_t N>
template<typename T, bool NSL>
void DevicePacket<R, N>::restOut(String properties, T payload) {
  uint8_t data_type = getTypeID<T>();
  if (response_buffer_mode) {
    if (data_type == DATA_TYPE_STRING) {
      String payload_str = String(payload);
      uint8_t pram_len = properties.length();
      uint8_t data_len = payload_str.length();
      uint8_t transfer_size = pram_len + data_len + 5;
      uint8_t buff[transfer_size] = { TRANSFER_DATA_BUFFER_SIG, BUFFER_PARAM_RESPNOSE, data_type, pram_len, data_len };  //buff_signeture(1 byte)+data_signeture(1 byte)+pram_len(1 bytes)+data_len(1 bytes)+prams_buff+data_buff
      memcpy(buff + 5, (uint8_t *)properties.c_str(), pram_len);
      memcpy(buff + (5 + pram_len), (uint8_t *)payload_str.c_str(), data_len);
      dataOutToSerial(buff, transfer_size);
    } else {
      restRawOut<T>(properties, &payload);
    }
  } else {
    String data = "{\"" + properties + (data_type == DATA_TYPE_STRING && NSL == false ? "\":\"" : "\":") + String(payload) + (data_type == DATA_TYPE_STRING && NSL == false ? "\"}" : "}");
    dataOutToSerial((uint8_t *)data.c_str(), data.length());
  }
}

// Template function
template<typename R, uint16_t N>
template<typename T>
void DevicePacket<R, N>::restArrayOut(String properties, T data[], uint8_t data_size) {
  uint8_t type_size = sizeof(T);
  if (response_buffer_mode) {
    uint8_t pram_len = properties.length();
    uint8_t data_len = type_size * data_size;
    uint8_t transfer_size = pram_len + data_len + 6;
    uint8_t type = getTypeID<T>();
    uint8_t buff[transfer_size] = { TRANSFER_DATA_BUFFER_SIG, BUFFER_ARRY_RESPNOSE, type, type_size, pram_len, data_size };  //buff_signeture(1 byte)+data_signeture(1 byte)+type(1 bytes)+type_size(1 bytes)+pram_len(1 bytes)+data_size(1 bytes)+prams_buff+data_buff
    memcpy(buff + 6, (uint8_t *)properties.c_str(), pram_len);
    memcpy(buff + (6 + pram_len), (uint8_t *)data, data_len);
    dataOutToSerial(buff, transfer_size);
  } else {
    bool is_float_type = std::is_same<T, float>::value;
    String str = "[";
    for (uint8_t i = 0; i < data_size; i++) {
      if (i != 0) {
        str += String(",");
      }

      if (is_float_type) str += String(data[i], 5);
      else str += String(data[i]);

      // if (data[i] == nullptr) {
      //   str += "null";
      // } else {
      //   if (is_float_type) str += String(data[i], 5);
      //   else str += String(data[i]);
      // }
    }
    str += "]";
    restOut<String, true>(properties, str);  //no string last (active)
  }
}
