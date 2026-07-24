#include "./DevicePacketParser.h"
#include <algorithm>

void DevicePacketParser::setStreamCaptureCallback(streamCaptureFunction_t callback)
{
    stream_capture_cb = callback;
}

void DevicePacketParser::onPacketReceive(PacketReceiverFunction_t fun)
{
    packet_receiver_cb = fun;
}

void DevicePacketParser::setDelimeters(const uint8_t *del, size_t len)
{
    delete[] delimeters;
    delimeters = nullptr;
    delimeter_len = len;

    if (len == 0)
        return;

    delimeters = new uint8_t[len];
    std::memcpy(delimeters, del, len * sizeof(uint8_t));
}

void DevicePacketParser::setReceiver(std::map<String, void (*)(String, String)> receivers)
{
    insert_pram_data_cmnds = receivers;
}

void DevicePacketParser::setReceiver(std::map<String, void (*)(String)> receivers, bool prams)
{
    if (prams)
        get_pram_cmnds = receivers;
    else
        insert_data_cmnds = receivers;
}

void DevicePacketParser::setReceiver(std::map<String, void (*)()> receivers)
{
    get_process_cmnds = receivers;
}

void DevicePacketParser::setReceiver(std::map<String, void (*)(const uint8_t *, uint8_t, uint16_t, uint16_t)> receivers)
{
    get_response_buff = receivers;
}

void DevicePacketParser::onReceive(String name, void (*fun)(String, String))
{
    insert_pram_data_cmnds[name] = fun;
}

void DevicePacketParser::onReceive(String name, void (*fun)(String), bool prams)
{
    if (prams)
        get_pram_cmnds[name] = fun;
    else
        insert_data_cmnds[name] = fun;
}

void DevicePacketParser::onReceive(String name, void (*fun)())
{
    get_process_cmnds[name] = fun;
}

void DevicePacketParser::onReceive(String name, void (*fun)(const uint8_t *, uint8_t, uint16_t, uint16_t))
{
    get_response_buff[name] = fun;
}

void DevicePacketParser::commandProcess(const uint8_t *data, uint16_t len)
{

    // Serial.println("Receive:" + String(len));
    // Serial.flush();

    if (len >= 6 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_TEXT_RESPNOSE)
    {
        // TRANSFER_DATA_TEXT_HEADER_LEN+CRC_SIZE(2 bytes)=6

        if (verify_crc)
        {
            // with crc
            if (!CyclicRedundancyCheck::verifyCRC16(data, len, initial_crc))
                return;
            len -= 2; // reduce crc
        }
        uint8_t data_len_msb = data[2];
        uint8_t data_len_lsb = data[3];
        uint16_t data_len = ((data_len_msb << 8) | data_len_lsb) & 0xFFFF;
        if (data_len + TRANSFER_DATA_TEXT_HEADER_LEN <= len)
        {
            String text = String((char *)(data + TRANSFER_DATA_TEXT_HEADER_LEN), data_len);
            if (get_process_cmnds.find(text) != get_process_cmnds.end())
            {
                // Call the function if the key is found
                get_process_cmnds[text]();
            }
        }
    }
    else if (len >= 8 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_PARAM_RESPNOSE)
    {
        // TRANSFER_DATA_PARAMS_HEADER_LEN+CRC_SIZE(2 bytes)=8
        //  Serial.println("Prams:"+String(len)+",type:"+String((uint8_t)data[4]));
        if (verify_crc)
        {
            // with crc
            if (!CyclicRedundancyCheck::verifyCRC16(data, len, initial_crc))
                return;
            len -= 2; // reduce crc
        }

        uint8_t data_type = data[2];
        uint8_t pram_len = data[3];
        uint8_t data_len_msb = data[4];
        uint8_t data_len_lsb = data[5];
        uint16_t data_len = ((data_len_msb << 8) | data_len_lsb) & 0xFFFF;

        // Serial.println("data_type:"+String(data_type)+",pram_len:"+String(pram_len)+",data_len:"+String(data_len));

        if (pram_len + data_len + TRANSFER_DATA_PARAMS_HEADER_LEN <= len)
        {
            String param = String(data + TRANSFER_DATA_PARAMS_HEADER_LEN, pram_len);

            // Serial.println("Data Pram-->"+param);

            // for(uint8_t i=5 + pram_len;i<len;i++){
            //   Serial.print(" "+String(data[i],HEX));
            // }
            // Serial.println();

            if (any_response_buff.find(param) != any_response_buff.end())
            {
                any_response_buff[param](data + (TRANSFER_DATA_PARAMS_HEADER_LEN + pram_len), data_type, data_len, 1); // single data
            }
            else if (get_response_buff.find(param) != get_response_buff.end())
            {
                // Call the function if the key is found
                get_response_buff[param](data + (TRANSFER_DATA_PARAMS_HEADER_LEN + pram_len), data_type, data_len, 1); // single data
            }
        }
    }
    else if (len >= 9 && data[0] == TRANSFER_DATA_BUFFER_SIG && data[1] == BUFFER_ARRY_RESPNOSE)
    {
        // TRANSFER_DATA_ARRAY_HEADER_LEN+CRC_SIZE(2 bytes)=9
        //  Serial.println("Array:"+String(len));
        if (verify_crc)
        {
            // with crc
            if (!CyclicRedundancyCheck::verifyCRC16(data, len, initial_crc))
                return;
            len -= 2; // reduce crc
        }

        uint8_t data_type = data[2];
        uint8_t type_size = data[3];
        uint8_t pram_len = data[4];
        uint8_t data_size_msb = data[5];
        uint8_t data_size_lsb = data[6];
        uint16_t data_size = ((data_size_msb << 8) | data_size_lsb) & 0xFFFF;
        uint16_t data_len = type_size * data_size;
        if (pram_len + data_len + TRANSFER_DATA_ARRAY_HEADER_LEN <= len)
        {
            String param = String(data + TRANSFER_DATA_ARRAY_HEADER_LEN, pram_len);
            // Serial.println(param);
            // Serial.println(get_response_buff.size());
            if (any_response_buff.find(param) != any_response_buff.end())
            {
                any_response_buff[param](data + (TRANSFER_DATA_ARRAY_HEADER_LEN + pram_len), data_type, type_size, data_size);
            }
            else if (get_response_buff.find(param) != get_response_buff.end())
            {
                // Call the function if the key is found
                get_response_buff[param](data + (TRANSFER_DATA_ARRAY_HEADER_LEN + pram_len), data_type, type_size, data_size);
            }
        }
    }
    else
    {
        String cmd = String(data, len);
        uint16_t cmd_len = cmd.length();

        // Serial.println(cmd);
        // Serial.flush();

        if (cmd_len > 6 && cmd[3] == ':' && cmd[5] == '=')
        {
            // for COMMAND:PRAM=DATA
            // if value setting command happen
            String f_cmd = cmd.substring(0, 3);
            // Check if the key exists in the map
            if (insert_pram_data_cmnds.find(f_cmd) != insert_pram_data_cmnds.end())
            {

                String m_cmd = cmd.substring(4, 5);
                String s_cmd = cmd.substring(6);

                // Call the function if the key is found
                insert_pram_data_cmnds[f_cmd](m_cmd, s_cmd);
            }
        }
        else if (cmd_len > 4 && cmd[3] == '=')
        {
            // for COMMAND=DATA
            // if value setting command happen
            String f_cmd = cmd.substring(0, 3);

            if (insert_data_cmnds.find(f_cmd) != insert_data_cmnds.end())
            {

                String s_cmd = cmd.substring(4);
                // Call the function if the key is found
                insert_data_cmnds[f_cmd](s_cmd);
            }
        }
        else if (cmd_len > 4 && cmd[3] == ':')
        {
            // for COMMAND:PRAM
            // if value setting command happen
            String f_cmd = cmd.substring(0, 3);

            if (get_pram_cmnds.find(f_cmd) != get_pram_cmnds.end())
            {
                String s_cmd = cmd.substring(4);
                // Call the function if the key is found
                get_pram_cmnds[f_cmd](s_cmd);
            }
        }
        else
        {
            if (get_process_cmnds.find(cmd) != get_process_cmnds.end())
            {
                // Call the function if the key is found
                get_process_cmnds[cmd]();
            }
        }
    }
}

inline void DevicePacketParser::packetHandler(const uint8_t *data, uint16_t len)
{
    if (packet_receiver_cb != nullptr)
        packet_receiver_cb(data, len);
    else
        commandProcess(data, len);
}

void DevicePacketParser::processBytes(const uint8_t *all_bytes, size_t len)
{
    if (all_bytes == nullptr || len == 0)
        return;

    size_t x = 0;
    while (x < len)
    {
        if (packet_length != 0)
        {
            uint16_t remaining = packet_length > buffer_current_len ? (packet_length - buffer_current_len) : 0;
            if (remaining == 0)
            {
                packet_length = 0;
                packet_timeout_at = 0; // reset timeout

                this->receiver_lock();
                packetHandler(buffer_.get(), buffer_current_len);
                buffer_current_len = 0;
                this->receiver_unlock();
                continue;
            }

            size_t chunk = std::min((size_t)remaining, len - x);

            this->receiver_lock();
            std::memcpy(buffer_.get() + buffer_current_len, all_bytes + x, chunk);
            buffer_current_len += (uint16_t)chunk;
            this->receiver_unlock();

            x += chunk;

            if (buffer_current_len == packet_length)
            {
                packet_length = 0;
                packet_timeout_at = 0; // reset timeout

                this->receiver_lock();
                packetHandler(buffer_.get(), buffer_current_len);
                buffer_current_len = 0;
                this->receiver_unlock();
            }
            continue;
        }

        // Store each byte first, then evaluate whether we have a full packet or delimited command.
        this->receiver_lock();
        buffer_[buffer_current_len] = all_bytes[x];
        buffer_current_len++;
        x++;

        if (buffer_current_len >= buffer_size_)
        {
            buffer_current_len = 0;
        }
        this->receiver_unlock();

        // Non-packet mode: detect packet signature and switch into packet receiving mode.
        if (buffer_current_len >= PACKET_SIGNETURE_LEN)
        {
            size_t offset = buffer_current_len - PACKET_SIGNETURE_LEN;
            uint16_t packet_size = getPacketLength(buffer_.get() + offset);

            if (packet_size != 0)
            {
                this->receiver_lock();
                buffer_current_len = 0; // reset buffer index for making ready to receive actual buffer
                this->receiver_unlock();

                if (packet_size < buffer_size_)
                {
                    packet_length = packet_size;
                    // minimum baud rate could 4800bps that mean 600bytes for second,
                    // considering 2ms for each of byte, and some extra delay (100ms)
                    packet_timeout_at = millis() + (packet_size * 2) + 100;
                }

                continue;
            }
        }

        if (delimeter_len != 0 && buffer_current_len >= delimeter_len)
        {
            size_t offset = buffer_current_len - delimeter_len;

            if (memcmp(buffer_.get() + offset, delimeters, delimeter_len) == 0)
            {
                this->receiver_lock();
                packet_length = 0;
                buffer_current_len = offset; // original data length

                packetHandler(buffer_.get(), buffer_current_len);
                buffer_current_len = 0;

                this->receiver_unlock();
            }
        }
    }
}

void DevicePacketParser::setVerifyCRC(bool state)
{
    verify_crc = state;
}

void DevicePacketParser::setInitialCRC(uint16_t crc)
{
    initial_crc = crc;
}

bool DevicePacketParser::getBufferMode()
{
    return response_buffer_mode;
}

void DevicePacketParser::setBufferMode(bool state)
{
    response_buffer_mode = state;
}

bool DevicePacketParser::writeToPort(uint8_t *buff, uint16_t size)
{
    if (stream_capture_cb == nullptr)
        return false;
    stream_capture_cb(buff, size);
    return true;
}

uint16_t DevicePacketParser::getPacketLength(const uint8_t *transfer_buff)
{
    if (transfer_buff[0] != packet_info[0] || transfer_buff[PACKET_SIGNETURE_LEN - 1] != packet_info[PACKET_SIGNETURE_LEN - 1])
        return 0;

    uint16_t packet_size = 0;
    for (uint8_t i = 1; i < (PACKET_SIGNETURE_LEN - 1); i++)
    {
        if (packet_info[i] == 0x0F)
        {
            // data
            if (transfer_buff[i] > 0x0F)
            {
                // if segment is invalid
                return 0;
            }
            packet_size = (packet_size << 4) | transfer_buff[i];
        }
        else if (packet_info[i] != transfer_buff[i])
        {
            // format is not matching
            return 0;
        }
    }

    return packet_size;
}

void DevicePacketParser::updatePacketLength(uint8_t *transfer_buff, uint16_t packet_size)
{
    memcpy(transfer_buff, packet_info, PACKET_SIGNETURE_LEN);
    // Serial.printf("Updating packet length: %d \r\n", packet_size);
    //{ (packet_size & 0xF000) >> 12, (packet_size & 0x0F00) >> 8, (packet_size & 0x00F0) >> 4, packet_size & 0x000F }
    for (uint8_t i = 0; i < PACKET_SIGNETURE_DATA_LEN; i++)
    {
        transfer_buff[(i * 2) + 1] = (packet_size >> (12 - (i * 4))) & 0x0F;
        // Serial.printf("Len byte %d : %02X \r\n", (i * 2) + 1, transfer_buff[(i * 2) + 1]);
    }
}

void DevicePacketParser::dataOutToSerial(uint8_t *buff, uint16_t size, uint8_t *header, uint8_t header_size)
{
    if (size == 0)
        return;

    uint16_t crc = 0;

    if (verify_crc)
    {
        crc = header_size > 0 ? CyclicRedundancyCheck::calculateCRC16(header, header_size, initial_crc) : 0;
        crc = CyclicRedundancyCheck::calculateCRC16(buff, size, crc);
    }

    if (response_buffer_mode)
    {
        uint16_t packet_size = header_size + size + (verify_crc ? CRC16_BYTE_LEN : 0);

        uint8_t transfer_buff[PACKET_SIGNETURE_LEN];
        updatePacketLength(transfer_buff, packet_size);

        if (!this->writer_lock())
            return;

        writeToPort(transfer_buff, PACKET_SIGNETURE_LEN);
        if (header_size > 0)
            writeToPort(header, header_size);

        writeToPort(buff, size);

        if (verify_crc)
        {
            uint8_t crc_bytes[CRC16_BYTE_LEN] = {(uint8_t)(crc >> 8), (uint8_t)crc};
            writeToPort(crc_bytes, CRC16_BYTE_LEN);
        }

        this->writer_unlock();
    }
    else
    {

        if (!this->writer_lock())
            return;

        if (header_size > 0)
            dataOutToSerial(header, header_size);
        dataOutToSerial(buff, size);

        if (verify_crc)
        {
            uint8_t end_bytes[CRC16_BYTE_LEN + delimeter_len] = {(uint8_t)(crc >> 8), (uint8_t)crc};
            if (delimeter_len > 0)
                memcpy(end_bytes + CRC16_BYTE_LEN, delimeters, delimeter_len);

            dataOutToSerial(end_bytes, CRC16_BYTE_LEN + delimeter_len);
        }
        else if (delimeter_len > 0)
        {
            dataOutToSerial(delimeters, delimeter_len);
        }

        this->writer_unlock();
    }
}

void DevicePacketParser::dataOutToSerial(String str)
{
    // uint16_t str_len=str.length();
    // uint8_t buff[str_len+1];
    // str.toCharArray((char *)buff,str_len+1);
    dataOutToSerial((uint8_t *)str.c_str(), str.length());
}

void DevicePacketParser::restOutStr(String properties, String payload)
{
    restOut(properties, payload);
}

void DevicePacketParser::restOutFloat(String properties, float payload)
{
    restOut(properties, payload);
}

void DevicePacketParser::restOutInt(String properties, int payload)
{
    restOut(properties, payload);
}

void DevicePacketParser::restOutHex(String properties, uint32_t payload)
{
    restOut(properties, payload);
}

void DevicePacketParser::restOutBin(String properties, uint32_t payload)
{
    restOut(properties, payload);
}

void DevicePacketParser::restOutSuccess(String payload)
{
    restOutStr("payload", payload);
}

void DevicePacketParser::restOutError(String err)
{
    restOutStr("error", err);
}

bool DevicePacketParser::writer_lock()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    return xSemaphoreTake(this->writter_locker, portMAX_DELAY) == pdTRUE;
#else
    return true;
#endif
}

bool DevicePacketParser::writer_unlock()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    return xSemaphoreGive(this->writter_locker) == pdTRUE;
#else
    return true;
#endif
}

bool DevicePacketParser::receiver_lock()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    return xSemaphoreTake(this->receiver_locker, portMAX_DELAY) == pdTRUE;
#else
    return true;
#endif
}

bool DevicePacketParser::receiver_unlock()
{
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(FREERTOS) || defined(configUSE_PREEMPTION)
    return xSemaphoreGive(this->receiver_locker) == pdTRUE;
#else
    return true;
#endif
}

void DevicePacketParserBufferCaptureCallback(DevicePacketParser *parser, const uint8_t *data, uint16_t len)
{
    parser->processBytes((uint8_t *)data, len);
}