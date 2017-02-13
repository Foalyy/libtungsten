#ifndef _USART_H_
#define _USART_H_

#include <stdint.h>
#include "gpio.h"

// Universal Synchronous Asynchronous Receiver Transmitter
// This module allows the chip to communicate on an RS232 link
// (also sometimes called Serial port)
namespace USART {

    // Peripheral memory space base addresses
    const uint32_t USART_BASE = 0x40024000;
    const uint32_t USART_REG_SIZE = 0x4000;

    // Register offsets
    const uint32_t OFFSET_CR =      0x00;
    const uint32_t OFFSET_MR =      0x04;
    const uint32_t OFFSET_IER =     0x08;
    const uint32_t OFFSET_IDR =     0x0C;
    const uint32_t OFFSET_IMR =     0x10;
    const uint32_t OFFSET_CSR =     0x14;
    const uint32_t OFFSET_RHR =     0x18;
    const uint32_t OFFSET_THR =     0x1C;
    const uint32_t OFFSET_BRGR =    0x20;
    const uint32_t OFFSET_RTOR =    0x24;
    const uint32_t OFFSET_TTGR =    0x28;
    const uint32_t OFFSET_FIDI =    0x40;
    const uint32_t OFFSET_NER =     0x44;
    const uint32_t OFFSET_IFR =     0x4C;
    const uint32_t OFFSET_MAN =     0x50;
    const uint32_t OFFSET_LINMR =   0x54;
    const uint32_t OFFSET_LINIR =   0x58;
    const uint32_t OFFSET_LINBR =   0x5C;
    const uint32_t OFFSET_WPMR =    0xE4;
    const uint32_t OFFSET_WPSR =    0xE8;
    const uint32_t OFFSET_VERSION = 0xFC;

    // Subregisters
    const uint32_t CR_RSTRX = 2;
    const uint32_t CR_RSTTX = 3;
    const uint32_t CR_RXEN = 4;
    const uint32_t CR_RXDIS = 5;
    const uint32_t CR_TXEN = 6;
    const uint32_t CR_TXDIS = 7;
    const uint32_t MR_MODE = 0;
    const uint32_t MR_CHRL = 6;
    const uint32_t MR_PAR = 9;
    const uint32_t BRGR_CD = 0;
    const uint32_t BRGR_FP = 16;
    const uint32_t CSR_RXRDY = 0;
    const uint32_t CSR_TXRDY = 1;
    const uint32_t CSR_RXBRK = 2;
    const uint32_t CSR_TXEMPTY = 9;
    const uint32_t IER_RXRDY = 0;

    // Constants
    const uint32_t WPMR_KEY = 0x555341 << 8;
    const uint32_t WPMR_ENABLE = 1;
    const uint32_t WPMR_DISABLE = 0;
    const uint32_t MODE_NORMAL = 0b0000;
    const uint32_t MODE_HARDWARE_HANDSHAKE = 0b0010;

    const int N_USARTS = 4;
    enum class Port {
        USART0,
        USART1,
        USART2,
        USART3
    };

    const uint8_t BIN = 2;
    const uint8_t DEC = 10;
    const uint8_t HEX = 16;

    enum class PinFunction {
        RX,
        TX,
        RTS,
        CTS
    };

    const int BUFFER_SIZE = 256;

    const uint8_t INTERRUPT_PRIORITY = 10;


    // Module API
    void enable(Port port, unsigned long baudrate, bool hardwareFlowControl=false);
    void disable(Port port);
    void setBaudrate(Port port, unsigned long baudrate);
    void setHardwareFlowControl(Port port, bool hardwareFlowControl);
    void enableInterrupt(Port port, void (*handler)());
    int available(Port port);
    bool isOverflow(Port port);
    bool contains(Port port, char byte);
    char peek(Port port);
    bool peek(Port port, const char* test, int size);
    char read(Port port);
    int read(Port port, char* buffer, int size, bool readUntil=false, char end=0x00);
    int readUntil(Port port, char* buffer, int size, char end);
    unsigned long readInt(Port port, int nBytes, bool wait=true);
    int write(Port port, const char* buffer, int size=-1);
    int write(Port port, char byte);
    int write(Port port, int number, uint8_t base);
    int write(Port port, bool boolean);
    int writeLine(Port port, const char* buffer, int size=-1);
    int writeLine(Port port, char byte);
    int writeLine(Port port, int number, uint8_t base);
    int writeLine(Port port, bool boolean);
    void waitFinished(Port port, unsigned long timeout=100);
    bool flush(Port port);
    void setPin(Port port, PinFunction function, GPIO::Pin pin);

}


#endif