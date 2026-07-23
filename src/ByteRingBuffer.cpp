#include "ByteRingBuffer.h"

ByteRingBuffer::ByteRingBuffer(uint16_t capacity)
    : buffer_(capacity > 0
                  ? std::make_unique<uint8_t[]>(capacity)
                  : nullptr),
      capacity_(capacity)
{
    this->transfer_locker = xSemaphoreCreateMutex();
    configASSERT(this->transfer_locker != NULL && "TransferQueue making failed."); // verify

    txEvents = xEventGroupCreate();
    configASSERT(txEvents != nullptr && "Failed to create event group for SmartSPIDevice.");
    // Initially, there is no pending data.
    xEventGroupSetBits(txEvents, TX_FLUSHED_BIT);
}

ByteRingBuffer::~ByteRingBuffer()
{
    if (this->transfer_locker != NULL)
    {
        xSemaphoreTake(this->transfer_locker, portMAX_DELAY);
        this->buffer_.reset();
        xSemaphoreGive(this->transfer_locker);

        vSemaphoreDelete(this->transfer_locker);
        this->transfer_locker = NULL;
    }
}

bool ByteRingBuffer::push(const uint8_t *source, uint16_t length)
{
    if (length == 0)
    {
        return true;
    }

    if (source == nullptr)
    {
        return false;
    }

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return false;

    if (capacity_ == 0 || length > availableDirect())
    {
        xSemaphoreGive(this->transfer_locker);
        return false;
    }

    const uint16_t firstPart =
        std::min(length, (uint16_t)(capacity_ - writeIndex_));

    std::memcpy(
        buffer_.get() + writeIndex_,
        source,
        firstPart);

    const uint16_t secondPart = length - firstPart;

    if (secondPart > 0)
    {
        std::memcpy(
            buffer_.get(),
            source + firstPart,
            secondPart);
    }

    writeIndex_ = (writeIndex_ + length) % capacity_;
    size_ += length;

    xSemaphoreGive(this->transfer_locker);

    return true;
}

[[nodiscard]] uint16_t ByteRingBuffer::pop(uint8_t *destination, uint16_t maximumLength)
{
    if (maximumLength == 0 || destination == nullptr)
    {
        return 0;
    }

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return 0;

    if (capacity_ == 0 || size_ == 0)
    {
        xSemaphoreGive(this->transfer_locker);
        return 0;
    }

    uint16_t length =
        std::min(maximumLength, size_);

    copyOut(destination, length);

    readIndex_ = (readIndex_ + length) % capacity_;
    size_ -= length;

    if (size_ == 0)
    {
        // Clear the TX_FLUSHED_BIT to indicate that there is no pending data to be sent.
        flushTrigger();
    }

    xSemaphoreGive(this->transfer_locker);

    return length;
}

bool ByteRingBuffer::popExact(uint8_t *destination, uint16_t length)
{
    if (length == 0)
    {
        return true;
    }
    if (destination == nullptr)
    {
        return false;
    }

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return false;

    if (length > size_)
    {
        xSemaphoreGive(this->transfer_locker);
        return false;
    }

    copyOut(destination, length);

    readIndex_ = (readIndex_ + length) % capacity_;
    size_ -= length;

    if (size_ == 0)
    {
        // Clear the TX_FLUSHED_BIT to indicate that there is no pending data to be sent.
        flushTrigger();
    }

    xSemaphoreGive(this->transfer_locker);

    return true;
}

[[nodiscard]] uint16_t ByteRingBuffer::peek(uint8_t *destination, uint16_t maximumLength) const
{
    if (maximumLength == 0 || destination == nullptr)
    {
        return 0;
    }

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return 0;

    if (capacity_ == 0 || size_ == 0)
    {
        xSemaphoreGive(this->transfer_locker);
        return 0;
    }

    uint16_t length =
        std::min(maximumLength, size_);

    copyOut(destination, length);

    xSemaphoreGive(this->transfer_locker);

    return length;
}

bool ByteRingBuffer::remove(uint16_t length)
{

    if (length == 0)
    {
        return true;
    }

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return false;

    if (length > size_)
    {
        xSemaphoreGive(this->transfer_locker);
        return false;
    }

    readIndex_ = (readIndex_ + length) % capacity_;
    size_ -= length;

    if (size_ == 0)
    {
        // Clear the TX_FLUSHED_BIT to indicate that there is no pending data to be sent.
        flushTrigger();
    }

    xSemaphoreGive(this->transfer_locker);

    return true;
}

bool ByteRingBuffer::clear()
{
    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return false;

    readIndex_ = 0;
    writeIndex_ = 0;
    size_ = 0;

    flushTrigger();

    xSemaphoreGive(this->transfer_locker);

    return true;
}

[[nodiscard]] inline uint16_t ByteRingBuffer::sizeDirect() const
{
    return size_;
}

[[nodiscard]] std::optional<uint16_t> ByteRingBuffer::sizeOpt() const
{

    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return std::nullopt;

    const uint16_t currentSize = size_;

    xSemaphoreGive(this->transfer_locker);

    return currentSize;
}

[[nodiscard]] uint16_t ByteRingBuffer::size() const
{
    std::optional<uint16_t> currentSize = this->sizeOpt();
    if (currentSize.has_value())
        return currentSize.value();
    else
        return 0;
}

[[nodiscard]] uint16_t ByteRingBuffer::capacity() const
{
    return capacity_;
}

[[nodiscard]] inline uint16_t ByteRingBuffer::availableDirect() const
{
    return capacity_ - size_;
}

[[nodiscard]] std::optional<uint16_t> ByteRingBuffer::availableOpt() const
{
    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return std::nullopt;

    const uint16_t availableSpace = capacity_ - size_;

    xSemaphoreGive(this->transfer_locker);

    return availableSpace;
}

[[nodiscard]] uint16_t ByteRingBuffer::available() const
{

    std::optional<uint16_t> availableSpace = this->availableOpt();
    if (availableSpace.has_value())
        return availableSpace.value();
    else
        return 0;
}

[[nodiscard]] inline bool ByteRingBuffer::emptyDirect() const
{
    return size_ == 0;
}

[[nodiscard]] std::optional<bool> ByteRingBuffer::emptyOpt() const
{
    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return std::nullopt;

    const bool isEmpty = (size_ == 0);

    xSemaphoreGive(this->transfer_locker);

    return isEmpty;
}

[[nodiscard]] bool ByteRingBuffer::empty() const
{

    std::optional<bool> isEmpty = this->emptyOpt();
    if (isEmpty.has_value())
        return isEmpty.value();
    else
        return true;
}

[[nodiscard]] std::optional<bool> ByteRingBuffer::fullOpt() const
{
    if (xSemaphoreTake(this->transfer_locker, portMAX_DELAY) == pdFALSE)
        return std::nullopt;

    const bool isFull = (size_ == capacity_);

    xSemaphoreGive(this->transfer_locker);

    return isFull;
}

[[nodiscard]] bool ByteRingBuffer::full() const
{

    std::optional<bool> isFull = this->fullOpt();
    if (isFull.has_value())
        return isFull.value();
    else
        return false;
}

void ByteRingBuffer::copyOut(uint8_t *destination, uint16_t length) const
{
    const uint16_t firstPart =
        std::min(length, (uint16_t)(capacity_ - readIndex_));

    std::memcpy(
        destination,
        buffer_.get() + readIndex_,
        firstPart);

    const uint16_t secondPart = length - firstPart;

    if (secondPart > 0)
    {
        std::memcpy(
            destination + firstPart,
            buffer_.get(),
            secondPart);
    }
}

bool ByteRingBuffer::write(const uint8_t *data, uint16_t length)
{
    bool success = push(data, length);
    if (success)
    {
        // Clear the TX_FLUSHED_BIT to indicate that there is pending data to be sent.
        xEventGroupClearBits(txEvents, TX_FLUSHED_BIT);

        if (writeCallback != nullptr)
        {
            writeCallback(write_callback_context, data, length);
        }
    }
    return success;
}

bool ByteRingBuffer::flush(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(
        txEvents,
        TX_FLUSHED_BIT,
        pdFALSE, // Do not clear the bit when one task receives it.
        pdTRUE,  // Wait for all requested bits.
        timeout);

    return (bits & TX_FLUSHED_BIT) != 0;
}

void ByteRingBuffer::flushTrigger()
{
    xEventGroupSetBits(txEvents, TX_FLUSHED_BIT);
}

void ByteRingBuffer::onWrite(RingBufferWriteCallback_t callback, void *context)
{
    writeCallback = callback;
    write_callback_context = context;
}

void ByteRingBuffer::offWrite()
{
    writeCallback = nullptr;
    write_callback_context = nullptr;
}