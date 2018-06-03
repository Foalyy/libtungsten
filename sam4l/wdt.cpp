#include "wdt.h"
#include "scif.h"

namespace WDT {

    void enable(unsigned int timeout, Unit unit) {
        // Compute timings
        uint8_t psel = 0;
        if (timeout > 0) {
            unsigned int basePeriod = 10000000L / SCIF::getRCSYSFrequency(); // Base period in 10th of microseconds
            if (unit == Unit::MILLISECONDS) {
                timeout *= 1000; // microseconds
            }
            timeout *= 10;  // 10th of microseconds
            timeout = timeout / basePeriod; // timeout in units of rcsys clocks

            // Find the closest power of two greater than the computed timeout
            // cf datasheet p 491 (20. WDT / 20.6 User Interface / 20.6.1 CTRL Control Register)
            for (int i = 0; i < 32; i++) {
                if (static_cast<unsigned int>(1 << (psel + 1)) >= timeout) {
                    break;
                }
                psel++;
            }
        }

        // CTRL (Control Register) : configure then enable the watchdog
        // The WDT must first be configured, then, in a second time, enabled.
        // The CTRL register must be written twice for each operation, the 
        // first time with the first key (0x55), then with the second key (0xAA).
        // cf datasheet p483 (20. WDT / 20.5 Functional Description / 20.5.1 Basic 
        // Mode / 20.5.1.1 WDT Control Register Access)
        uint32_t ctrl = 1 << CTRL_DAR     // DAR : disable the watchdog after a reset
                      | 1 << CTRL_FCD     // FCD : skip flash calibration after reset
                      | 1 << CTRL_CEN     // CEN : enable the clock
                      | psel << CTRL_PSEL;// PSEL : timeout counter
        (*(volatile uint32_t*)(WDT_BASE + OFFSET_CTRL)) // Configure
            = ctrl
            | CTRL_KEY_1;
        (*(volatile uint32_t*)(WDT_BASE + OFFSET_CTRL))
            = ctrl
            | CTRL_KEY_2;
        (*(volatile uint32_t*)(WDT_BASE + OFFSET_CTRL)) // Enable, keeping the same configuration
            = ctrl
            | 1 << CTRL_EN
            | CTRL_KEY_1;
        (*(volatile uint32_t*)(WDT_BASE + OFFSET_CTRL))
            = ctrl
            | 1 << CTRL_EN
            | CTRL_KEY_2;
    }

}