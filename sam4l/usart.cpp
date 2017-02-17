#include "usart.h"
#include "core.h"
#include "gpio.h"
#include "pm.h"
#include "dma.h"

namespace USART {

    // Internal functions
    void rxBufferFullHandler();

    // Clocks
    const int PM_CLK[] = {PM::CLK_USART0, PM::CLK_USART1, PM::CLK_USART2, PM::CLK_USART3};

    // Package-dependant, defined in pins_sam4l_XX.cpp
    // Can be modified usign setPin()
    extern struct GPIO::Pin PINS_RX[];
    extern struct GPIO::Pin PINS_TX[];
    extern struct GPIO::Pin PINS_CTS[];
    extern struct GPIO::Pin PINS_RTS[];

    // Ports
    struct USART {
        bool hardwareFlowControl;
        uint8_t rxBuffer[BUFFER_SIZE];
        uint8_t txBuffer[BUFFER_SIZE];
        int rxBufferCursor;
        int txBufferCursor;
        bool rxBufferCursor2Reset;
        bool rxBufferCursor2ResetTwice;
        bool rxBufferOverflow;
        int rxDMAChannel;
        int txDMAChannel;
        unsigned long baudrate;
    };
    struct USART _ports[N_USARTS];

    int _rxDMAChannelsToPorts[DMA::N_CHANNELS_MAX];


    void enable(Port port, unsigned long baudrate, bool hardwareFlowControl) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // Flow control
        p->hardwareFlowControl = hardwareFlowControl;

        // Initialize the buffers
        for (int i = 0; i < BUFFER_SIZE; i++) {
            p->rxBuffer[i] = 0;
            p->txBuffer[i] = 0;
        }
        p->rxBufferCursor = 0;
        p->txBufferCursor = 0;
        p->rxBufferCursor2Reset = false;
        p->rxBufferCursor2ResetTwice = false;
        p->rxBufferOverflow = false;

        // Set the pins in peripheral mode
        GPIO::enablePeripheral(PINS_RX[static_cast<int>(port)]);
        GPIO::enablePeripheral(PINS_TX[static_cast<int>(port)]);
        if (hardwareFlowControl) {
            GPIO::enablePeripheral(PINS_RTS[static_cast<int>(port)]);
            GPIO::enablePeripheral(PINS_CTS[static_cast<int>(port)]);
        }

        // Enable the clock
        PM::enablePeripheralClock(PM_CLK[static_cast<int>(port)]);

        // Set the operating mode : normal RS232 or hardware-handshaking (modem) mode
        setHardwareFlowControl(port, hardwareFlowControl);

        // Set the baudrate (this will also enable the port)
        setBaudrate(port, baudrate);

        // Set up the DMA channels and related interrupts
        p->rxDMAChannel = DMA::newChannel(static_cast<DMA::Device>(static_cast<int>(DMA::Device::USART0_RX) + static_cast<int>(port)), 
                (uint32_t)(p->rxBuffer), BUFFER_SIZE, DMA::Size::BYTE);
        p->txDMAChannel = DMA::newChannel(static_cast<DMA::Device>(static_cast<int>(DMA::Device::USART0_TX) + static_cast<int>(port)), 
                (uint32_t)(p->txBuffer), 0, DMA::Size::BYTE);
        _rxDMAChannelsToPorts[p->rxDMAChannel] = static_cast<int>(port);
        DMA::startChannel(p->rxDMAChannel, (uint32_t)(p->rxBuffer), BUFFER_SIZE);
        DMA::reloadChannel(p->rxDMAChannel, (uint32_t)(p->rxBuffer), BUFFER_SIZE);
        DMA::enableInterrupt(p->rxDMAChannel, &rxBufferFullHandler, DMA::Interrupt::RELOAD_EMPTY);
    }

    void disable(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // Free the pins
        GPIO::disablePeripheral(PINS_RX[static_cast<int>(port)]);
        GPIO::disablePeripheral(PINS_TX[static_cast<int>(port)]);
        if (p->hardwareFlowControl) {
            GPIO::disablePeripheral(PINS_RTS[static_cast<int>(port)]);
            GPIO::disablePeripheral(PINS_CTS[static_cast<int>(port)]);
        }
    }

    void setBaudrate(Port port, unsigned long baudrate) {
        const uint32_t REG_BASE = USART_BASE + static_cast<int>(port) * USART_REG_SIZE;
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // WPMR (Write Protect Mode Register) : disable the Write Protect
        (*(volatile uint32_t*)(REG_BASE + OFFSET_WPMR)) = WPMR_KEY | WPMR_DISABLE;

        // CR (Control Register) : disable RX and TX
        (*(volatile uint32_t*)(REG_BASE + OFFSET_CR))
            = 1 << CR_RXDIS
            | 1 << CR_TXDIS;

        // BRGR (Baud Rate Generator Register) : configure the baudrate generator
        // Cf datasheet 24.6.4 : baudrate = mainclock / (16 * CD), with FP to fine-tune by steps of 1/8
        p->baudrate = baudrate;
        const unsigned long clk = PM::getModuleClockFrequency(PM_CLK[static_cast<int>(port)]);
        const uint64_t cd100 = 100 * clk / 16 / baudrate;
        const uint8_t cd = cd100 / 100;
        const uint64_t fp100 = (cd100 % 100) * 8;
        uint8_t fp = fp100 / 100;
        if (fp100 - fp * 100 >= 5) {
            fp++; // Round
        }
        // TODO : error handling
        (*(volatile uint32_t*)(REG_BASE + OFFSET_BRGR))
            = cd << BRGR_CD
            | fp << BRGR_FP;

        // CR (Control Register) : enable RX and TX
        (*(volatile uint32_t*)(REG_BASE + OFFSET_CR))
            = 1 << CR_RXEN
            | 1 << CR_TXEN;

        // WPMR (Write Protect Mode Register) : re-enable the Write Protect
        (*(volatile uint32_t*)(REG_BASE + OFFSET_WPMR)) = WPMR_KEY | WPMR_ENABLE;
    }

    void setHardwareFlowControl(Port port, bool hardwareFlowControl) {
        const uint32_t REG_BASE = USART_BASE + static_cast<int>(port) * USART_REG_SIZE;

        // WPMR (Write Protect Mode Register) : disable the Write Protect
        (*(volatile uint32_t*)(REG_BASE + OFFSET_WPMR)) = WPMR_KEY | WPMR_DISABLE;

        // CR (Control Register) : disable RX and TX
        (*(volatile uint32_t*)(REG_BASE + OFFSET_CR))
            = 1 << CR_RXDIS
            | 1 << CR_TXDIS;

        // MR (Mode Register) : set the USART to asynchronous, normal or hardware handshaking RS232, 8 bits, no parity, 1 stop bit
        (*(volatile uint32_t*)(REG_BASE + OFFSET_MR))
            = (hardwareFlowControl ? MODE_HARDWARE_HANDSHAKE : MODE_NORMAL) << MR_MODE
            | 0b100 << MR_PAR
            | 0b11 << MR_CHRL;

        // CR (Control Register) : enable RX and TX
        (*(volatile uint32_t*)(REG_BASE + OFFSET_CR))
            = 1 << CR_RXEN
            | 1 << CR_TXEN;

        // WPMR (Write Protect Mode Register) : re-enable the Write Protect
        (*(volatile uint32_t*)(REG_BASE + OFFSET_WPMR)) = WPMR_KEY | WPMR_ENABLE;
    }

    void enableInterrupt(Port port, void (*handler)()) {
        const uint32_t REG_BASE = USART_BASE + static_cast<int>(port) * USART_REG_SIZE;

        // Set up an interrupt to be fired when a character is received
        Core::Interrupt interrupt = static_cast<Core::Interrupt>(static_cast<int>(Core::Interrupt::USART0) + static_cast<int>(port));
        Core::setInterruptHandler(interrupt, handler);
        Core::enableInterrupt(interrupt, INTERRUPT_PRIORITY);
        (*(volatile uint32_t*)(REG_BASE + OFFSET_IER))
            = 1 << IER_RXRDY;
    }

    void rxBufferFullHandler() {
        // Get the port that provoqued this interrupt
        int channel = static_cast<int>(Core::currentInterrupt()) - static_cast<int>(Core::Interrupt::DMA0);
        int portNumber = _rxDMAChannelsToPorts[channel];
        struct USART* p = &(_ports[portNumber]);

        // Reload the DMA channel
        DMA::reloadChannel(p->rxDMAChannel, (uint32_t)(p->rxBuffer), BUFFER_SIZE);

        // If rxBufferCursor2Reset is already set, it means the buffer is largely overflown :
        // it was not emptied since the last reset
        if (p->rxBufferCursor2Reset) {
            p->rxBufferCursor2ResetTwice = true;
        }
        p->rxBufferCursor2Reset = true;
    }

    bool checkOverflow(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // rxBufferCursor2Reset is true when the ring buffer's end cursor has reached the end of the linear buffer
        // and has been reset to 0 (this is detected by rxBufferFullHandler)
        // If rxBufferCursor2Reset is true and the second cursor is ahead of the first, the buffer has overflown
        p->rxBufferOverflow = p->rxBufferCursor2ResetTwice 
                || (p->rxBufferCursor2Reset && (BUFFER_SIZE - DMA::getCounter(p->rxDMAChannel)) >= p->rxBufferCursor);

        // If an overflow was detected, repair it
        if (p->rxBufferOverflow) {
            // Get the second cursors's pos from the DMA
            int cursor2 = BUFFER_SIZE - DMA::getCounter(p->rxDMAChannel);

            // Set the first cursor just behind the second to keep the maximum data possible
            p->rxBufferCursor = (cursor2 + 1) % BUFFER_SIZE;

            // Reset the cursor indicators
            if (p->rxBufferCursor == 0) {
                p->rxBufferCursor2Reset = false;
            } else {
                p->rxBufferCursor2Reset = true;
            }
            p->rxBufferCursor2ResetTwice = false;
        }

        return p->rxBufferOverflow;
    }

    int available(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // Get the second cursors's pos from the DMA
        int cursor2 = BUFFER_SIZE - DMA::getCounter(p->rxDMAChannel);

        // Check for a buffer overflow
        checkOverflow(port);

        // Get the position of the last char inserted into the buffer by DMA
        int length = cursor2 - p->rxBufferCursor;
        if (length >= 0) {
            return length;
        } else {
            return BUFFER_SIZE + length; // length is negative here
        }
    }

    bool contains(Port port, char byte) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // Check if the received buffer contains the specified byte
        int avail = available(port);
        for (int i = 0; i < avail; i++) {
            if (p->rxBuffer[(p->rxBufferCursor + i) % BUFFER_SIZE] == byte) {
                return true;
            }
        }
        return false;
    }

    char peek(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        if (available(port) > 0) {
            // Return the next char in the port's RX buffer
            return p->rxBuffer[p->rxBufferCursor];
        } else {
            return 0;
        }
    }

    bool peek(Port port, const char* test, int size) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        if (available(port) >= size) {
            // Check whether the /size/ next chars in the buffer are equal to test
            for (int i = 0; i < size; i++) {
                if (p->rxBuffer[(p->rxBufferCursor + i) % BUFFER_SIZE] != test[i]) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    }

    // Read one byte
    char read(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        if (available(port) > 0) {
            // Read the next char in the port's RX buffer
            char c = p->rxBuffer[p->rxBufferCursor];

            // Increment the cursor
            p->rxBufferCursor = (p->rxBufferCursor + 1) % BUFFER_SIZE;

            // If the first cursor is reset to the beginning of the linear buffer,
            // the second cursor is now ahead of the first
            if (p->rxBufferCursor == 0) {
                p->rxBufferCursor2Reset = false;
            }
            
            return c;
        } else {
            return 0;
        }
    }

    // Read up to /size/ bytes
    int read(Port port, char* buffer, int size, bool readUntil, char end) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        int avail = available(port);
        int n = 0;
        for (int i = 0; i < size && i < avail; i++) {
            // Copy the next char from the port's RX buffer to the user buffer
            if (buffer != nullptr) {
                buffer[i] = p->rxBuffer[p->rxBufferCursor];
            }

            // Keep track of the number of bytes written
            n++;

            // Increment the cursor
            p->rxBufferCursor = (p->rxBufferCursor + 1) % BUFFER_SIZE;

            // If the first cursor is reset to the beginning of the linear buffer,
            // the second cursor is now ahead of the first
            if (p->rxBufferCursor == 0) {
                p->rxBufferCursor2Reset = false;
            }
            
            // If the "read until" mode is selected, exit the loop if the selected byte is found
            if (readUntil && buffer != nullptr && buffer[i] == end) {
                break;
            }
        }
        return n;
    }

    // Read up to n bytes until the specified byte is found
    int readUntil(Port port, char* buffer, int size, char end) {
        return read(port, buffer, size, true, end);
    }

    // Read an int on n bytes (LSByte first, max n = 8 bytes)
    // and return it as an unsigned long
    unsigned long readInt(Port port, int nBytes, bool wait) {
        if (nBytes > 8) {
            nBytes = 8;
        }

        if (wait) {
            while (available(port) < nBytes);
        }

        unsigned long result = 0;
        for (int i = 0; i < nBytes; i++) {
            uint8_t c = read(port);
            result |= c << (i * 8);
        }
        return result;
    }

    int write(Port port, const char* buffer, int size) {
        const uint32_t REG_BASE = USART_BASE + static_cast<int>(port) * USART_REG_SIZE;
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // If size is not specified, write at most BUFFER_SIZE characters
        int n = size;
        if (n < 0 || n >  BUFFER_SIZE) {
            n = BUFFER_SIZE;
        }

        // Copy the user buffer into the TX buffer
        for (int i = 0; i < n; i++) {
            // If size is not specified, stops at the end of the string
            if (size < 0 && buffer[i] == 0) {
                n = i;
                break;
            }
            p->txBuffer[i] = buffer[i];
        }

        // Start the DMA
        DMA::startChannel(p->txDMAChannel, (uint32_t)(p->txBuffer), n);

        // Wait for the DMA to finish the transfer
        // This will be improved in the future to allow async communication
        while (!(DMA::isFinished(p->txDMAChannel)
            && (*(volatile uint32_t*)(REG_BASE + OFFSET_CSR)) & (1 << CSR_TXEMPTY)));

        // Return the number of bytes written
        return n;
    }

    int write(Port port, char byte) {
        const uint32_t REG_BASE = USART_BASE + static_cast<int>(port) * USART_REG_SIZE;
        struct USART* p = &(_ports[static_cast<int>(port)]);
        
        // Write a single byte
        p->txBuffer[0] = byte;
        DMA::startChannel(p->txDMAChannel, (uint32_t)(p->txBuffer), 1);
        while (!(DMA::isFinished(p->txDMAChannel)
            && (*(volatile uint32_t*)(REG_BASE + OFFSET_CSR)) & (1 << CSR_TXEMPTY)));
        return 1;
    }

    int write(Port port, int number, uint8_t base) {
        // Write a human-readable number in the given base
        const int bufferSize = 32; // Enough to write a 32-bits word in binary
        char buffer[bufferSize];
        int cursor = 0;
        if (base < 2) {
            return 0;
        }

        // Special case : number = 0
        if (number == 0) {
            return write(port, '0');
        }

        // Minus sign
        if (number < 0) {
            buffer[cursor] = '-';
            cursor++;
            number = -number;
        }

        // Compute the number in reverse
        int start = cursor;
        for (; cursor < bufferSize && number > 0; cursor++) {
            char c = number % base;
            if (c < 10) {
                c += '0';
            } else {
                c += 'A' - 10;
            }
            buffer[cursor] = c;
            number = number / base;
        }

        // Reverse the result
        for (int i = 0; i < (cursor - start) / 2; i++) {
            char c = buffer[start + i];
            buffer[start + i] = buffer[cursor - i - 1];
            buffer[cursor - i - 1] = c;
        }

        buffer[cursor] = 0;
        cursor++;
        return write(port, buffer, cursor);
    }

    int write(Port port, bool boolean) {
        // Write a boolean value
        if (boolean) {
            return write(port, "true");
        } else {
            return write(port, "false");
        }
    }

    int writeLine(Port port, const char* buffer, int size) {
        int written = write(port, buffer, size);
        written += write(port, "\r\n", 2);
        return written;
    }

    int writeLine(Port port, char byte) {
        int written = write(port, byte);
        written += write(port, "\r\n", 2);
        return written;
    }

    int writeLine(Port port, int number, uint8_t base) {
        int written = write(port, number, base);
        written += write(port, "\r\n", 2);
        return written;
    }

    int writeLine(Port port, bool boolean) {
        int written = write(port, boolean);
        written += write(port, "\r\n", 2);
        return written;
    }

    bool flush(Port port) {
        // Empty the reception buffer
        bool ret = false;
        while (available(port)) {
            read(port);
            ret = true;
        }
        return ret;
    }

    void waitFinished(Port port, unsigned long timeout) {
        struct USART* p = &(_ports[static_cast<int>(port)]);
        unsigned long tStart = Core::time();
        int n = available(port);
        while (n == 0) {
            n = available(port);
            if (timeout > 0 && Core::time() - tStart > timeout) {
                return;
            }
        }
        unsigned long byteDuration = 1000000UL / p->baudrate * 8; // in us
        while (1) {
            Core::waitMicroseconds(5 * byteDuration); // Wait for 5 times the duration of a byte
            int n2 = available(port);
            if (n2 == n) {
                // No byte received during the delay, the transfer looks finished
                return;
            }
            n = n2;
        }
    }

    bool isOverflow(Port port) {
        struct USART* p = &(_ports[static_cast<int>(port)]);

        // Returns true if the port has overflown and in this case, reset this state to false
        bool overflow = p->rxBufferOverflow;
        p->rxBufferOverflow = false;
        return overflow;
    }

    void setPin(Port port, PinFunction function, GPIO::Pin pin) {
        switch (function) {
            case PinFunction::RX:
                PINS_RX[static_cast<int>(port)] = pin;
                break;

            case PinFunction::TX:
                PINS_TX[static_cast<int>(port)] = pin;
                break;

            case PinFunction::RTS:
                PINS_RTS[static_cast<int>(port)] = pin;
                break;

            case PinFunction::CTS:
                PINS_CTS[static_cast<int>(port)] = pin;
                break;
        }
    };

}