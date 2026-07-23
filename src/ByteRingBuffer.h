#pragma once
#include <Arduino.h>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <freertos/event_groups.h>

#include "freertos/portmacro.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>


#include "./CyclicRedundancyCheck.h"

static constexpr EventBits_t TX_FLUSHED_BIT = BIT0;

typedef void (*RingBufferWriteCallback_t)(void*, const uint8_t *, uint16_t);

class ByteRingBuffer
{
public:
    ByteRingBuffer(uint16_t capacity);
    ~ByteRingBuffer();

    RingBufferWriteCallback_t writeCallback = nullptr;
    void* write_callback_context = nullptr;

    // Prevent accidental copying of the owned buffer.
    ByteRingBuffer(const ByteRingBuffer&) = delete;
    ByteRingBuffer& operator=(const ByteRingBuffer&) = delete;

    // Moving is safe with unique_ptr.
    ByteRingBuffer(ByteRingBuffer&&) noexcept = default;
    ByteRingBuffer& operator=(ByteRingBuffer&&) noexcept = default;

    // Push the complete chunk.
    // Returns false if there is insufficient free space.
    bool push(const uint8_t* source, uint16_t length);

    // Pop up to maximumLength bytes.
    // Returns the number of bytes actually removed.
    [[nodiscard]] uint16_t pop(uint8_t* destination, uint16_t maximumLength);

    // Pop exactly length bytes.
    // If there are insufficient bytes, nothing is removed.
    bool popExact(uint8_t* destination, uint16_t length);

    // Copy bytes without removing them.
    [[nodiscard]] uint16_t peek(uint8_t* destination, uint16_t maximumLength) const;
    bool remove(uint16_t length);

    bool clear();

    [[nodiscard]] inline uint16_t sizeDirect() const;
    [[nodiscard]] std::optional<uint16_t> sizeOpt() const;
    [[nodiscard]] uint16_t size() const;

    [[nodiscard]] uint16_t capacity() const;
    
    [[nodiscard]] inline uint16_t availableDirect() const;
    [[nodiscard]] std::optional<uint16_t> availableOpt() const;
    [[nodiscard]] uint16_t available() const;

    [[nodiscard]] inline bool emptyDirect() const;
    [[nodiscard]] std::optional<bool> emptyOpt() const;
    [[nodiscard]] bool empty() const;

    [[nodiscard]] std::optional<bool> fullOpt() const;
    [[nodiscard]] bool full() const;

    bool write(const uint8_t *data, uint16_t length);
    bool flush(TickType_t timeout = portMAX_DELAY);
    void flushTrigger();

    void onWrite(RingBufferWriteCallback_t callback, void* context = nullptr);
    void offWrite();

private:
    void copyOut(uint8_t* destination, uint16_t length) const;

    SemaphoreHandle_t transfer_locker = NULL;
    std::unique_ptr<uint8_t[]> buffer_;

    uint16_t capacity_ = 0;
    uint16_t readIndex_ = 0;
    uint16_t writeIndex_ = 0;
    uint16_t size_ = 0;

    EventGroupHandle_t txEvents;
};