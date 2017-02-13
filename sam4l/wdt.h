#ifndef _WDT_H_
#define _WDT_H_

#include <stdint.h>

// Watchdog Timer
// This module is able to automatically reset the chip after a
// specified delay unless it is periodically serviced. This is
// useful to recover from an unexpected behaviour which leads the
// execution to hang.
namespace WDT {

    // Peripheral memory space base address
    const uint32_t WDT_BASE = 0x400F0C00;


    // Registers addresses
    const uint32_t OFFSET_CTRL =     0x000; // Control Register
    const uint32_t OFFSET_CLR =      0x004; // Clear Register
    const uint32_t OFFSET_SR =       0x008; // Status Register
    const uint32_t OFFSET_IER =      0x00C; // Interrupt Enable Register
    const uint32_t OFFSET_IDR =      0x010; // Interrupt Disable Register
    const uint32_t OFFSET_IMR =      0x014; // Interrupt Mask Register
    const uint32_t OFFSET_ISR =      0x018; // Interrupt Status Register
    const uint32_t OFFSET_ICR =      0x01C; // Interrupt Clear Register


    // Subregisters
    const uint32_t CTRL_EN = 0;
    const uint32_t CTRL_DAR = 1;
    const uint32_t CTRL_MODE = 2;
    const uint32_t CTRL_SFV = 3;
    const uint32_t CTRL_IM = 4;
    const uint32_t CTRL_FCD = 7;
    const uint32_t CTRL_PSEL = 8;
    const uint32_t CTRL_CEN = 16;
    const uint32_t CTRL_CSSEL = 17;
    const uint32_t CTRL_TBAN = 18;


    // Constants
    const uint32_t CTRL_KEY_1 = 0x55 << 24;
    const uint32_t CTRL_KEY_2 = 0xAA << 24;

    enum class Unit {
        milliseconds,
        microseconds
    };

    // Module API
    void enable(unsigned int timeout, Unit unit=Unit::milliseconds);

}


#endif