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
    Polynomial _polynomial = Polynomial::CCIT8023;
    bool _refOut = false;

    // [hex(int(bin(i).split('b')[1].zfill(8)[::-1], 2)) for i in range(256)]
    const uint8_t _reflected[256] = {
        0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0, 
        0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 
        0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4, 
        0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, 
        0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2, 
        0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, 
        0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6, 
        0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, 
        0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1, 
        0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9, 
        0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5, 
        0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd, 
        0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3, 
        0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb, 
        0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7, 
        0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
    };


    void enable() {
        if (!_enabled) {
            // Enable the high-speed clock used for the computation unit
            PM::enablePeripheralClock(PM::CLK_CRC_HSB);

            // Enable the lower-speed clock used for the register access
            PM::enablePeripheralClock(PM::CLK_CRC);
            _enabled = true;
        }
    }

    uint32_t compute(const uint8_t* data, unsigned int length, Polynomial polynomial, bool refOut, bool async) {
        // Make sure the module is enabled
        enable();

        // Save the settings
        _polynomial = polynomial;
        _refOut = refOut;

        // Check the input size
        if (length > 0xFFFF) {
            Error::happened(Error::Module::CRC, WARN_OVERFLOW, Error::Severity::WARNING);
            return 0;
        }

        // Check if another computation is already in progress
        if (_computing) {
            // There is a computation in progress, check if it is finished
            if (!((*(volatile uint32_t*)(BASE + OFFSET_DMAISR)) & (1 << DMAISR_DMAISR))) {
                Error::happened(Error::Module::CRC, WARN_BUSY, Error::Severity::WARNING);
                return 0;
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

        // In async mode, return now; result can be checked later with isResultAvailable()
        // and getResult()
        if (async) {
            return 0;
        }

        // Wait until the result is available
        while (!isResultAvailable());

        // Return the result
        return getResult();
    }

    bool isResultAvailable() {
        if (_resultAvailable) {
            return true;
        }

        if (_computing) {
            // Check if the computation is finished
            if ((*(volatile uint32_t*)(BASE + OFFSET_DMAISR)) & (1 << DMAISR_DMAISR)) {
                _computing = false;
                _resultAvailable = true;
                return true;
            }
        }

        return false;
    }

    uint32_t getResult() {
        if (isResultAvailable()) {
            uint32_t crc = (*(volatile uint32_t*)(BASE + OFFSET_SR));
            if (_refOut) {
                uint8_t bytes[4];
                for (int i = 0; i < 4; i++) {
                    bytes[i] = _reflected[(crc >> (i * 8)) & 0xFF];
                }
                if (_polynomial == Polynomial::CCIT16) {
                    return (bytes[0] << 8) | bytes[1];
                } else {
                    return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
                }
            } else {
                return crc;
            }
        } else {
            Error::happened(Error::Module::CRC, WARN_RESULT_UNAVAILABLE, Error::Severity::WARNING);
            return 0;
        }
    }

}