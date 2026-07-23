#include "CyclicRedundancyCheck.h"

uint16_t CyclicRedundancyCheck::calculateCRC16(const uint8_t *data, size_t length, uint16_t initial_crc)
{
    // CRC-16 (CCITT-FALSE)
    uint16_t crc = initial_crc;
    for (uint16_t i = 0; i < length; ++i)
    {
        uint8_t idx = (uint8_t)((crc >> 8) ^ data[i]);
        crc = (uint16_t)((crc << 8) ^ crc16_ccitt_tbl[idx]);
    }
    return crc;
}

bool CyclicRedundancyCheck::verifyCRC16(const uint8_t *data, size_t length, uint16_t initial_crc)
{
    if (length <= CRC16_BYTE_LEN) return false;
    uint16_t offset = length - CRC16_BYTE_LEN;
    uint16_t crc = calculateCRC16(data, offset, initial_crc);

    return ((crc >> 8) & 0xFF) == data[offset] && (crc & 0xFF) == data[offset + 1]; // Assuming CRC16_BYTE_LEN is 2
}