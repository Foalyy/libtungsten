#ifndef _CRC_H_
#define _CRC_H_

#include "error.h"
#include <stdint.h>

// Cyclic Redundancy Check calculation unit
// This module asynchronously computes and checks CRC
// checksums from memory data
namespace CRC {

    // Peripheral memory space base address
    const uint32_t BASE = 0x400A4000;


    // Registers addresses
    const uint32_t OFFSET_DSCR =        0x000; // Descriptor Base Register
    const uint32_t OFFSET_DMAEN =       0x008; // DMA Enable Register
    const uint32_t OFFSET_DMADIS =      0x00C; // DMA Disable Register
    const uint32_t OFFSET_DMASR =       0x010; // DMA Status Register
    const uint32_t OFFSET_DMAIER =      0x014; // DMA Interrupt Enable Register
    const uint32_t OFFSET_DMAIDR =      0x018; // DMA Interrupt Disable Register
    const uint32_t OFFSET_DMAIMR =      0x01C; // DMA Interrupt Mask Register
    const uint32_t OFFSET_DMAISR =      0x020; // DMA Interrupt Status Register
    const uint32_t OFFSET_CR =          0x034; // Control Register
    const uint32_t OFFSET_MR =          0x038; // Mode Register
    const uint32_t OFFSET_SR =          0x03C; // Status Register
    const uint32_t OFFSET_IER =         0x040; // Interrupt Enable Register
    const uint32_t OFFSET_IDR =         0x044; // Interrupt Disable Register
    const uint32_t OFFSET_IMR =         0x048; // Interrupt Mask Register
    const uint32_t OFFSET_ISR =         0x04C; // Interrupt Status Register
    const uint32_t OFFSET_VERSION =     0x0FC; // Version Register

    // Registers in the RAM descriptor
    const uint32_t OFFSET_DSCR_ADDR =   0x000; // Address Register
    const uint32_t OFFSET_DSCR_CTRL =   0x004; // Control Register
    const uint32_t OFFSET_DSCR_CRC =    0x010; // CRC Reference Register

    // Subregisters
    const uint8_t DMASR_DMAEN = 0;
    const uint8_t DMAISR_DMAISR = 0;
    const uint8_t CR_RESET = 0;
    const uint8_t MR_ENABLE = 0;
    const uint8_t MR_COMPARE = 1;
    const uint8_t MR_PTYPE = 2;
    const uint8_t MR_DIVIDER = 4;
    const uint8_t ISR_ERRISR = 0;
    const uint8_t DSCR_CTRL_BTSIZE = 0;
    const uint8_t DSCR_CTRL_TRWIDTH = 24;
    const uint8_t DSCR_CTRL_IEN = 27;

    // Error codes
    const Error::Code WARN_BUSY = 0x0001;
    const Error::Code WARN_OVERFLOW = 0x0002;
    const Error::Code WARN_RESULT_UNAVAILABLE = 0x0003;

    // Available polynomial types
    enum class Polynomial {
        CCIT8023 = 0, // 0x04C11DB7
        CASTAGNOLI = 1, // 0x1EDC6F41
        CCIT16 = 2, // 0x1021
    };

    // Module API
    void compute(const uint8_t* data, unsigned int length, Polynomial polynomial);
    bool isResultAvailable();
    uint32_t getResult();
    void check(const uint8_t* data, unsigned int length, uint32_t crc);
    void enableInterrupt();
    void disableInterrupt();


}


#endif