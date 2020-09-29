#include "crc.h"
#include "pm.h"
#include "error.h"
#include <string.h>

namespace CRC {

    struct Descriptor {
        uint32_t addr;
        uint32_t ctrl;
        uint32_t reserved[2];
        uint32_t crc;
    };
    Descriptor _desc __attribute__ ((aligned (512)));

    bool _enabled = false;
    bool _computing = false;
    bool _resultAvailable = false;


    void enable() {
        if (!_enabled) {
            // Enable the high-speed clock used for the computation unit
            PM::enablePeripheralClock(PM::CLK_CRC_HSB);

            // Enable the lower-speed clock used for the register access
            PM::enablePeripheralClock(PM::CLK_CRC);
            _enabled = true;
        }
    }

    void compute(const uint8_t* data, unsigned int length, Polynomial polynomial) {
        // Make sure the module is enabled
        enable();

        // Check the input size
        if (length > 0xFFFF) {
            Error::happened(Error::Module::CRC, WARN_OVERFLOW, Error::Severity::WARNING);
            return;
        }

        // Check if another computation is already in progress
        if (_computing) {
            // There is a computation in progress, check if it is finished
            if (!((*(volatile uint32_t*)(BASE + OFFSET_DMAISR)) & (1 << DMAISR_DMAISR))) {
                Error::happened(Error::Module::CRC, WARN_BUSY, Error::Severity::WARNING);
                return;
            }
        }

        // DMADIS (DMA Disable Register) : disable the internal DMA controller
        // before making changes to the configuration
        (*(volatile uint32_t*)(BASE + OFFSET_DMADIS))
            = 1 << DMASR_DMAEN;

        // Reset the current state
        _computing = false;
        _resultAvailable = false;

        // Configure the DMA descriptor
        memset(&_desc, 0, sizeof(_desc));
        _desc.addr = reinterpret_cast<uint32_t>(data);
        _desc.ctrl = 
            length << DSCR_CTRL_BTSIZE // Number of bytes that the internal DMA should read
            | 00 << DSCR_CTRL_TRWIDTH  // Transfer width: byte
            | 0 << DSCR_CTRL_IEN;      // "End of transfer" interrupt disabled
        _desc.crc = 0;  // Compare mode disabled
        (*(volatile uint32_t*)(BASE + OFFSET_DSCR))
            = reinterpret_cast<uint32_t>(&_desc);

        // CR (Control Register) : reset the controller
        (*(volatile uint32_t*)(BASE + OFFSET_CR))
            = 1 << CR_RESET;

        // MR (Mode Register) : configure the computation
        (*(volatile uint32_t*)(BASE + OFFSET_MR))
            = 1 << MR_ENABLE    // Enable the computation
            | 0 << MR_COMPARE   // Compare mode disabled
            | static_cast<uint8_t>(polynomial) << MR_PTYPE
            | 0 << MR_DIVIDER;

        // DMAEN (DMA Enable Register) : enable the internal DMA controller
        (*(volatile uint32_t*)(BASE + OFFSET_DMAEN))
            = 1 << DMASR_DMAEN;

        // Computation is now started
        _computing = true;
    }

    bool isResultAvailable() {
        if (_resultAvailable) {
            return true;
        }

        if (_computing) {
            // Check if the computation is finished
            if ((*(volatile uint32_t*)(BASE + OFFSET_DMAISR)) & (1 << DMAISR_DMAISR)) {
                _resultAvailable = true;
                return true;
            }
        }

        return false;
    }

    uint32_t getResult() {
        if (isResultAvailable()) {
            return (*(volatile uint32_t*)(BASE + OFFSET_SR));
        } else {
            Error::happened(Error::Module::CRC, WARN_RESULT_UNAVAILABLE, Error::Severity::WARNING);
            return 0;
        }
    }

}