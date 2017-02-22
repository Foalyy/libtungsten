#include "RingBuffer.h"
#include <string.h>

// Constructor : must be passed the buffer to use
RingBuffer::RingBuffer(uint8_t* buffer, unsigned int capacity) {
    _buffer = buffer;
    _capacity = capacity;
    _cursorR = 0;
    _cursorW = 0;
    _overflow = false;
    _underflow = false;
}

// Read one byte from the internal buffer
uint8_t RingBuffer::read() {
    if (size() == 0) {
        _underflow = true;
        return 0;
    }

    uint8_t byte = _buffer[_cursorR];
    _cursorR++;
    if (_cursorR >= _capacity) {
        _cursorR = 0;
    }
    return byte;
}

// Read some bytes from the internal buffer
void RingBuffer::read(uint8_t* buffer, unsigned int size) {
    if (RingBuffer::size() < size) {
        _underflow = true;
        return;
    }

    // Copy data to the user buffer
    if (_cursorR + size <= _capacity) {
        memcpy(buffer, _buffer + _cursorR, size);
        _cursorR += size;

    } else {
        unsigned int size2 = _capacity - _cursorR;
        if (size2 > 0) {
            memcpy(buffer, _buffer + _cursorR, size2);
        }
        if (size - size2 > 0) {
            memcpy(buffer + size2, _buffer, size - size2);
        }
        _cursorR = size - size2;
    }
}

// Write one byte into the internal buffer
void RingBuffer::write(uint8_t byte) {
    // Check for overflow
    if (size() + 1 == _capacity) {
        _overflow = true;
        return;
    }

    // Copy the byte and move the cursor
    _buffer[_cursorW] = byte;
    _cursorW++;
    if (_cursorW >= _capacity) {
        _cursorW = 0;
    }
}

// Write some bytes into the internal buffer
void RingBuffer::write(const uint8_t* buffer, unsigned int size) {
    // Check for overflow
    if (RingBuffer::size() + size >= _capacity) {
        _overflow = true;
        return;
    }

    // Copy data into the internal buffer
    if (_cursorW + size <= _capacity) {
        memcpy(_buffer + _cursorW, buffer, size);
        _cursorW += size;

    } else {
        unsigned int size2 = _capacity - _cursorW;
        if (size2 > 0) {
            memcpy(_buffer + _cursorW, buffer, size2);
        }
        if (size - size2 > 0) {
            memcpy(_buffer, buffer + size2, size - size2);
        }
        _cursorW = size - size2;
    }
}

// Number of bytes currently stored in the buffer
unsigned int RingBuffer::size() {
    if (_cursorW >= _cursorR) {
        return _cursorW - _cursorR;
    } else {
        return _capacity - _cursorR + _cursorW;
    }
}

// Internal buffer total capacity
unsigned int RingBuffer::capacity() {
    return _capacity;
}

// Return true if the internal buffer is overflown
// (too many writes, not enough reads)
bool RingBuffer::isOverflow() {
    return _overflow;
}

// Return true if the user attempted to read more bytes than
// were available
bool RingBuffer::isUnderflow() {
    return _underflow;
}

// Revert the ring buffer to its initial state : 
// cursors and overflow/underflow flags are reset
void RingBuffer::reset() {
    _cursorR = 0;
    _cursorW = 0;
    _overflow = false;
    _underflow = false;
}