#include "RingBuffer.h"
#include <string.h>

// Constructor : must be passed the buffer to use
RingBuffer::RingBuffer(uint8_t* buffer, unsigned int capacity) {
    _buffer = buffer;
    _capacity = capacity;
    _cursorR = 0;
    _cursorW = 0;
    _empty = true;
    _overflow = false;
    _underflow = false;
}

// Get the value of a random byte in the buffer, without
// changing the cursors
uint8_t RingBuffer::operator[](unsigned int i) const {
    if (i >= size()) {
        return 0;
    } else {
        if (_cursorR + i < _capacity) {
            return _buffer[_cursorR + i];
        } else {
            return _buffer[_cursorR + i - _capacity];
        }
    }
}

// Read one byte from the internal buffer
uint8_t RingBuffer::read() {
    if (_empty) {
        _underflow = true;
        return 0;
    }

    uint8_t byte = _buffer[_cursorR];
    _cursorR++;
    if (_cursorR >= _capacity) {
        _cursorR = 0;
    }
    if (_cursorR == _cursorW) {
        _empty = true;
    }
    return byte;
}

// Read some bytes from the internal buffer
int RingBuffer::read(uint8_t* buffer, unsigned int size) {
    // If the internal buffer is empty, return immediately
    if (_empty) {
        _underflow = true;
        return 0;
    }

    // If the requested number of bytes is greater than the
    // current size, read only what is available and set the
    // underflow flag
    unsigned int s = RingBuffer::size();
    if (s < size) {
        size = s;
        _underflow = true;
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
    if (_cursorR == _cursorW) {
        _empty = true;
    }

    // Return the number of bytes that were actually read
    return size;
}

// Write one byte into the internal buffer
void RingBuffer::write(uint8_t byte) {
    // Check for overflow
    if (size() + 1 > _capacity) {
        _overflow = true;
        return;
    }

    // Copy the byte and move the cursor
    _buffer[_cursorW] = byte;
    _cursorW++;
    if (_cursorW >= _capacity) {
        _cursorW = 0;
    }
    _empty = false;
}

// Write some bytes into the internal buffer
void RingBuffer::write(const uint8_t* buffer, unsigned int size) {
    // Check for overflow
    if (RingBuffer::size() + size > _capacity) {
        _overflow = true;
        return;
    }

    // Copy data into the internal buffer
    if (_cursorW + size <= _capacity) {
        memcpy(_buffer + _cursorW, buffer, size);
        _cursorW += size;
        if (_cursorW == _capacity) {
            _cursorW = 0;
        }

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
    _empty = false;
}

// Number of bytes currently stored in the buffer
unsigned int RingBuffer::size() const {
    if (_cursorW == _cursorR) {
        if (_empty) {
            return 0;
        } else {
            return _capacity;
        }
    } else if (_cursorW > _cursorR) {
        return _cursorW - _cursorR;
    } else {
        return _capacity - _cursorR + _cursorW;
    }
}

// Check if the specified byte is in the buffer
int RingBuffer::contains(uint8_t byte) const {
    if (_cursorW > _cursorR) {
        for (unsigned int i = _cursorR; i < _cursorW; i++) {
            if (_buffer[i] == byte) {
                return i - _cursorR;
            }
        }
    } else if (_cursorW < _cursorR) {
        for (unsigned int i = _cursorR; i < _capacity; i++) {
            if (_buffer[i] == byte) {
                return i - _cursorR;
            }
        }
        for (unsigned int i = 0; i < _cursorW; i++) {
            if (_buffer[i] == byte) {
                return _capacity - _cursorR + i;
            }
        }
    }

    // Not found
    return -1;
}

// Internal buffer total capacity
unsigned int RingBuffer::capacity() const {
    return _capacity;
}

// Return true if the internal buffer is overflown
// (too many writes, not enough reads)
bool RingBuffer::isOverflow() const {
    return _overflow;
}

// Return true if the user attempted to read more bytes than
// were available
bool RingBuffer::isUnderflow() const {
    return _underflow;
}

// Revert the ring buffer to its initial state : 
// cursors and overflow/underflow flags are reset
void RingBuffer::reset() {
    _cursorR = 0;
    _cursorW = 0;
    _empty = true;
    _overflow = false;
    _underflow = false;
}