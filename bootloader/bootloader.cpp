#include <stdint.h>
#include <core.h>
#include <scif.h>
#include <pm.h>
#include <gpio.h>
#include <tc.h>
#include <usart.h>
#include <usb.h>
#include <flash.h>
#include <string.h>

// This is the bootloader for the libtungsten library. It can be configured to either open an
// UART (serial) port or to connect via USB, and to be activated with an external input (such
// as another microcontroller or a button) and/or a timeout. Most of the behaviour can be customized
// to your liking.

// Configuration
const bool MODE_INPUT = true;
const bool MODE_TIMEOUT = false;
const bool CHANNEL_USART = false;
const bool CHANNEL_USB = true;
const bool LEDS_ENABLED = true;
const GPIO::Pin PIN_LED_BL = GPIO::PA01; // Green led on Carbide
const GPIO::Pin PIN_LED_WRITE = GPIO::PA02; // Blue led on Carbide
const GPIO::Pin PIN_LED_ERROR = GPIO::PA00; // Red led on Carbide
const GPIO::Pin PIN_BUTTON = GPIO::PA04; // For INPUT mode
const unsigned int TIMEOUT_DELAY = 3000; // ms; for TIMEOUT mode
const unsigned int LED_BLINK_DELAY = 80; // ms
const USART::Port USART_PORT = USART::Port::USART1;


// USB request codes (Host -> Device)
enum class Request {
    START_BOOTLOADER,
    CONNECT,
    STATUS,
    WRITE,
    GET_ERROR,
};

// USB status codes (Device -> Host)
enum class Status {
    READY,
    BUSY,
    ERROR,
};

// USB error codes (Device -> Host)
enum class BLError {
    NONE,
    CHECKSUM_MISMATCH,
    PROTECTED_AREA,
    UNKNOWN_RECORD_TYPE,
    OVERFLOW,
};

// Currently active channel
enum class Channel {
    NONE,
    USART,
    USB,
};

// Currently active mode
enum class Mode {
    NONE,
    INPUT,
    TIMEOUT,
};

// Number of flash pages reserved to the bootloader. If this value is modified, please update
// the FLASH/LENGTH parameter in ld_scripts/bootloader.ld and the FLASH/ORIGIN parameter in the
// three ld_scripts/usercode_bootloader_lsxx.ld files accordingly.
// For BOOTLOADER_N_FLASH_PAGES = 32, the total bootloader size is 32 * 512 (size of a flash page
// in bytes) = 16384 = 0x4000.
const int BOOTLOADER_N_FLASH_PAGES = 32;

const int BUFFER_SIZE = 128;
char _buffer[BUFFER_SIZE];
volatile int _currentPage = -1;
volatile int _frameCounter = 0;
volatile int _bufferCursor = 0;
volatile bool _bufferFull = false;
volatile Status _status = Status::READY;
volatile bool _exitBootloader = false;
volatile bool _connected = false;
volatile BLError _error = BLError::NONE;
volatile Channel _activeChannel = Channel::NONE;
volatile Mode _activeMode = Mode::NONE;
bool _onePageWritten = false;
uint16_t _extendedSegmentAddress = 0;
uint16_t _extendedLinearAddress = 0;


// Handler called when a CONTROL packet is sent over USB
int usbControlHandler(USB::SetupPacket &lastSetupPacket, uint8_t* data, int size) {
    Request request = static_cast<Request>(lastSetupPacket.bRequest);

    if (lastSetupPacket.direction == USB::EPDir::IN || lastSetupPacket.wLength == 0) {
        if (request == Request::START_BOOTLOADER) {
            lastSetupPacket.handled = true;
            // Bootloader is already started

        } else if (request == Request::CONNECT) {
            lastSetupPacket.handled = true;
            _connected = true;
            _currentPage = -1;
            _frameCounter = 0;
            _activeChannel = Channel::USB;

        } else if (request == Request::STATUS) {
            lastSetupPacket.handled = true;
            if (data != nullptr && size >= 1) {
                data[0] = static_cast<int>(_status);
                return 1;
            }

        } else if (request == Request::GET_ERROR) {
            lastSetupPacket.handled = true;
            if (data != nullptr && size >= 1) {
                data[0] = static_cast<int>(_error);
                return 1;
            }
        }

    } else { // OUT
        if (request == Request::WRITE && !lastSetupPacket.handled) {
            lastSetupPacket.handled = true;
            if (size <= BUFFER_SIZE) {
                _status = Status::BUSY;
                memcpy(_buffer, data, size);
                _bufferFull = true;
            } else {
                _status = Status::ERROR;
                _error = BLError::OVERFLOW;
            }
        }
    }

    return 0;
}

// Parse an hex number in text format and return its value
unsigned int parseHex(const char* buffer, int pos, int n) {
    unsigned int r = 0;
    for (int i = 0; i < n; i++) {
        char c = buffer[pos + i];
        if (c >= '0' && c <= '9') {
            r += c - '0';
        } else if (c >= 'A' && c <= 'F') {
            r += c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            r += c - 'a' + 10;
        } else {
            return 0;
        }

        if (i < n - 1) {
            r <<= 4;
        }
    }
    return r;
}

void writePage(int page, const uint8_t* buffer) {
    if (!_onePageWritten) {
        // If this is the first time a page is written, this means that
        // the flash doesn't contain a valid firmware anymore : disable
        // the FW_READY fuse
        _onePageWritten = true;
        Flash::writeFuse(Flash::FUSE_BOOTLOADER_FW_READY, false);
    }
    if (LEDS_ENABLED) {
        GPIO::set(PIN_LED_WRITE, false);
    }
    Flash::writePage(page, (uint32_t*)buffer);
    if (LEDS_ENABLED) {
        GPIO::set(PIN_LED_WRITE, true);
    }
}


int main() {
    bool enterBootloader = false;

    // In TIMEOUT mode, enter bootloader mode except if the core was reset after a timeout
    bool skipTimeout = Flash::getFuse(Flash::FUSE_BOOTLOADER_SKIP_TIMEOUT);
    if (skipTimeout) {
        // Reset the fuse and do not enter bootloader
        Flash::writeFuse(Flash::FUSE_BOOTLOADER_SKIP_TIMEOUT, false);
    } else if (MODE_TIMEOUT) {
        enterBootloader = true;
        _activeMode = Mode::TIMEOUT;
    }


    // In INPUT mode, enter bootloader mode if the button is pressed
    if (MODE_INPUT) {
        GPIO::init();
        GPIO::enableInput(PIN_BUTTON, GPIO::Pulling::PULLUP);
        // Waste a few cycles to let the pullup the time to raise the line
        for (int i = 0; i < 1000; i++) {
            __asm__("nop");
        }
        if (GPIO::get(PIN_BUTTON) == GPIO::LOW) {
            enterBootloader = true;
            _activeMode = Mode::INPUT;
        }
    }

    // Force entering bootloader in these cases :
    // - the reset handler pointer or the stack pointer don't look right (the memory is empty, after the flashing of a new bootloader?)
    // - there is no available firmware according to the FW_READY fuse (a previous upload failed ?)
    // - the BOOTLOADER_FORCE fuse is set (after a call to Core::resetToBootloader() ?)
    uint32_t userStackPointerAddress = (*(volatile uint32_t*)(BOOTLOADER_N_FLASH_PAGES * Flash::FLASH_PAGE_SIZE_BYTES));
    uint32_t userResetHandlerAddress = (*(volatile uint32_t*)(BOOTLOADER_N_FLASH_PAGES * Flash::FLASH_PAGE_SIZE_BYTES + 0x04));
    if (userStackPointerAddress == 0x00000000 || userStackPointerAddress == 0xFFFFFFFF
            || userResetHandlerAddress == 0x00000000 || userResetHandlerAddress == 0xFFFFFFFF
            || !Flash::getFuse(Flash::FUSE_BOOTLOADER_FW_READY)
            || Flash::getFuse(Flash::FUSE_BOOTLOADER_FORCE)) {
        enterBootloader = true;
    }
    

    if (enterBootloader) {
        // Init the basic core systems
        Core::init();

        // Set main clock to the 12MHz RC oscillator
        SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_12MHZ);
        PM::setMainClockSource(PM::MainClockSource::RCFAST);

        // Enable serial port
        if (CHANNEL_USART) {
            USART::enable(USART_PORT, 115200);
        }

        // Enable USB in Device mode
        if (CHANNEL_USB) {
            USB::initDevice();
            USB::setControlHandler(usbControlHandler);
        }

        // Enable LED
        if (LEDS_ENABLED) {
            GPIO::enableOutput(PIN_LED_BL, GPIO::LOW);
            GPIO::enableOutput(PIN_LED_WRITE, GPIO::HIGH);
            GPIO::enableOutput(PIN_LED_ERROR, GPIO::HIGH);
        }

        // Reset the BOOTLOADER_FORCE fuse
        if (Flash::getFuse(Flash::FUSE_BOOTLOADER_FORCE)) {
            Flash::writeFuse(Flash::FUSE_BOOTLOADER_FORCE, false);
        }


        const int PAGE_BUFFER_SIZE = Flash::FLASH_PAGE_SIZE_BYTES;
        uint8_t pageBuffer[PAGE_BUFFER_SIZE];
        memset(pageBuffer, 0, PAGE_BUFFER_SIZE);

        // Wait for instructions on any enabled channel
        Core::Time lastTimeLedToggled = 0;
        GPIO::PinState ledState = GPIO::HIGH;
        while (!_exitBootloader) {
            // Blink rapidly
            if (LEDS_ENABLED && Core::time() > lastTimeLedToggled + LED_BLINK_DELAY) {
                ledState = !ledState;
                GPIO::set(PIN_LED_BL, ledState);
                lastTimeLedToggled = Core::time();
            }

            if (!_connected) {
                // Timeout
                if (_activeMode == Mode::TIMEOUT && Core::time() > TIMEOUT_DELAY) {
                    _exitBootloader = true;
                }

                // In USART mode, if "SYN" is received, the codeuploader is trying to connect
                if (CHANNEL_USART && USART::available(USART_PORT) >= 3) {
                    if (USART::peek(USART_PORT, "SYN", 3)) {
                        // Flush
                        char tmp[3];
                        USART::read(USART_PORT, tmp, 3);

                        // Answer
                        USART::write(USART_PORT, "ACK");

                        _connected = true;
                        _activeChannel = Channel::USART;

                    } else {
                        // Flush a byte
                        USART::read(USART_PORT);
                    }
                }

                // In USB mode, _connected is updated by interrupt in controlHandler()

            } else { // Connected
                // Read incoming data in USART mode
                if (_activeChannel == Channel::USART && USART::available(USART_PORT)) {
                    char c = USART::read(USART_PORT);
                    if (_bufferCursor == 0) {
                        if (c == ':') {
                            // Start of a new frame
                            _buffer[0] = c;
                            _bufferCursor++;
                        } // Otherwise, ignore the byte

                    } else {
                        if (c == '\n') {
                            // End of frame
                            _bufferFull = true;
                            _frameCounter++;
                        } else {
                            _buffer[_bufferCursor] = c;
                            _bufferCursor++;
                        }
                    }
                }
                
                // Handle errors that might have happened in the interrupt handler
                if (_error != BLError::NONE) {
                    _status = Status::ERROR;
                    if (LEDS_ENABLED) {
                        GPIO::set(PIN_LED_ERROR, false);
                    }
                    while (1); // Stall
                }

                // Handle a frame
                if (_bufferFull && _buffer[0] == ':') {
                    // cf https://en.wikipedia.org/wiki/Intel_HEX
                    int cursor = 1;
                    int nBytes = parseHex(_buffer, cursor, 2);
                    cursor += 2;
                    uint32_t addr = parseHex(_buffer, cursor, 4) + _extendedSegmentAddress * 16 + (_extendedLinearAddress << 16);
                    int page = addr / Flash::FLASH_PAGE_SIZE_BYTES;
                    int offset = addr % Flash::FLASH_PAGE_SIZE_BYTES;
                    cursor += 4;
                    uint8_t recordType = parseHex(_buffer, cursor, 2);
                    cursor += 2;

                    // Verify checksum
                    uint8_t checksum = parseHex(_buffer, cursor + 2 * nBytes, 2);
                    uint8_t s = 0;
                    for (int i = 0; i < nBytes + 4; i++) {
                        s += parseHex(_buffer, 2 * i + 1, 2);
                    }
                    s = ~s + 1;
                    if (s != checksum) {
                        // Error
                        _status = Status::ERROR;
                        _error = BLError::CHECKSUM_MISMATCH;
                        if (LEDS_ENABLED) {
                            GPIO::set(PIN_LED_ERROR, false);
                        }
                        if (_activeChannel == Channel::USART) {
                            USART::write(USART_PORT, "CHECKSUM_MISMATCH");
                        }
                        while (1); // Stall
                    }

                    // Command 0x00 is a data frame
                    if (recordType == 0x00) {
                        // Bootloader's flash domain is protected
                        if (page < BOOTLOADER_N_FLASH_PAGES) {
                            // Error
                            _status = Status::ERROR;
                            _error = BLError::PROTECTED_AREA;
                            if (LEDS_ENABLED) {
                                GPIO::set(PIN_LED_ERROR, false);
                            }
                            if (_activeChannel == Channel::USART) {
                                USART::write(USART_PORT, "PROTECTED_AREA");
                            }
                            while (1); // Stall
                        }

                        // Change page if necessary
                        if (page != _currentPage) {
                            if (_currentPage != -1) {
                                writePage(_currentPage, pageBuffer);
                            }
                            // Reset page buffer
                            _currentPage = page;
                            memset(pageBuffer, 0, PAGE_BUFFER_SIZE);
                        }

                        // Save data
                        if (offset + nBytes <= PAGE_BUFFER_SIZE) {
                            for (int i = 0; i < nBytes; i++) {
                                pageBuffer[offset + i] = parseHex(_buffer, cursor + 2 * i, 2);
                            }
                        } else { // Write across pages
                            int nBytesPage1 = PAGE_BUFFER_SIZE - offset;
                            int nBytesPage2 = nBytes - nBytesPage1;
                            for (int i = 0; i < nBytesPage1; i++) {
                                pageBuffer[offset + i] = parseHex(_buffer, cursor + 2 * i, 2);
                            }
                            writePage(_currentPage, pageBuffer);
                            offset = 0;
                            _currentPage++;
                            for (int i = 0; i < nBytesPage2 - offset; i++) {
                                pageBuffer[i] = parseHex(_buffer, cursor + 2 * (nBytesPage1 + i), 2);
                            }
                        }

                    } else if (recordType == 0x01) {
                        // End of file
                        writePage(_currentPage, pageBuffer);

                        // The firmware has been completely uploaded, set the FW_READY fuse
                        Flash::writeFuse(Flash::FUSE_BOOTLOADER_FW_READY, true);

                        // Exit the booloader to reboot
                        _exitBootloader = true;

                    } else if (recordType == 0x02) {
                        _extendedSegmentAddress = parseHex(_buffer, cursor, 4);
                        
                    } else if (recordType == 0x03) {
                        // CS and IP pointer : ignore

                    } else if (recordType == 0x04) {
                        _extendedLinearAddress = parseHex(_buffer, cursor, 4);

                    } else if (recordType == 0x05) {
                        // EIP pointer : ignore

                    } else {
                        // Error
                        _status = Status::ERROR;
                        _error = BLError::UNKNOWN_RECORD_TYPE;
                        if (LEDS_ENABLED) {
                            GPIO::set(PIN_LED_ERROR, false);
                        }
                        if (_activeChannel == Channel::USART) {
                            USART::write(USART_PORT, "UNKNOWN_RECORD_TYPE");
                        }
                        while (1); // Stall
                    }

                    _bufferFull = false;
                    _bufferCursor = 0;
                    // In USART mode, send acknowledge every 5 frames
                    if (_activeChannel == Channel::USART && _frameCounter == 5) {
                        USART::write(USART_PORT, "ACK");
                        _frameCounter = 0;
                    }
                    _status = Status::READY;
                }
            }
        }

        // Reset the chip to free all resources
        Flash::writeFuse(Flash::FUSE_BOOTLOADER_SKIP_TIMEOUT, true);
        Core::reset();
        while (1);

    } else {
        // Load the stack pointer register at offset 0 of the user's vector table
        // See ARMv7-M Architecture Reference Manual, section B1.5.3 The vector Table
        volatile uint32_t* sp = (volatile uint32_t*)(BOOTLOADER_N_FLASH_PAGES * Flash::FLASH_PAGE_SIZE_BYTES);
        __asm__("LDR sp, %0" : : "m" (*sp));

        // Execute the user code by jumping to offset 4 of the user's vector table (ResetHandler)
        // See ARMv7-M Architecture Reference Manual, section B1.5.2 Exception number definition
        void (*userResetHandler)() = (void (*)())(userResetHandlerAddress);
        userResetHandler();
    }
}
