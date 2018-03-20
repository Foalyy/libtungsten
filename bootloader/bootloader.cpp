#include <stdint.h>
#include <core.h>
#include <scif.h>
#include <pm.h>
#include <gpio.h>
#include <tc.h>
#include <usart.h>
#include <usb.h>
#include <flash.h>
#include <wdt.h>
#include <string.h>

// This is the bootloader for the libtungsten library. It can be configured to either open an
// UART (serial) port or to connect via USB, and to be activated with an external input (such
// as another microcontroller or a button) and/or a timeout. Most of the behaviour can be customized
// to your liking.


// Configuration
const bool MODE_INPUT = false;
const bool MODE_TIMEOUT = true;
const bool CHANNEL_USART = false;
const bool CHANNEL_USB = true;
const bool _ledsEnabled = true;
const GPIO::Pin PIN_LED_BL {GPIO::Port::A, 0}; // Green led on Carbide
const GPIO::Pin PIN_LED_WRITE {GPIO::Port::A, 2}; // Blue led on Carbide
const GPIO::Pin PIN_LED_ERROR {GPIO::Port::A, 1}; // Red led on Carbide
const GPIO::Pin PIN_BUTTON {GPIO::Port::A, 4}; // For INPUT mode
const unsigned int TIMEOUT_DELAY = 3000; // ms; for TIMEOUT mode
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
    CHECKSUM,
    PROTECTED_AREA,
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

const int BOOTLOADER_N_FLASH_PAGES = 32;
const int BUFFER_SIZE = 64;
char _buffer[BUFFER_SIZE];
int _bufferCursor = 0;
bool _bufferFull = false;
volatile Status _status = Status::READY;
volatile bool _exitBootloader = false;
volatile bool _connected = false;
volatile BLError _error = BLError::NONE;
volatile Channel _activeChannel = Channel::NONE;
volatile Mode _activeMode = Mode::NONE;


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


// Handler called when a CONTROL packet is sent over USB
void usbControlHandler(USB::SetupPacket &lastSetupPacket, uint8_t* data, int &size) {
    Request request = static_cast<Request>(lastSetupPacket.bRequest);
    if (request == Request::START_BOOTLOADER) {
        lastSetupPacket.handled = true;
        // Bootloader is already started

    } else if (request == Request::CONNECT) {
        lastSetupPacket.handled = true;
        _connected = true;
        _activeChannel = Channel::USB;

        // Turn on the LED
        GPIO::set(PIN_LED_BL, false);

    } else if (request == Request::STATUS) {
        lastSetupPacket.handled = true;
        size = 1;
        data[0] = static_cast<int>(_status);

    } else if (request == Request::WRITE && _activeChannel == Channel::USB) {
        lastSetupPacket.handled = true;
        _status = Status::BUSY;
        memcpy(_buffer, data, size);
        _bufferFull = true;

    } else if (request == Request::GET_ERROR) {
        lastSetupPacket.handled = true;
        memcpy(_buffer, data, size);
    }
}


int main() {
    bool enterBootloader = false;

    // In INPUT mode, enter bootloader mode if the button is pressed
    if (MODE_INPUT) {
        GPIO::enableInput(PIN_BUTTON, GPIO::Pulling::PULLUP);
        if (GPIO::get(PIN_BUTTON) == GPIO::LOW) {
            enterBootloader = true;
            _activeMode = Mode::INPUT;
        }
    }

    // In TIMEOUT mode, enter bootloader mode except if the core
    // was reset after a timeout (see below)
    if (MODE_TIMEOUT) {
        if (PM::resetCause() != PM::ResetCause::WDT) {
            enterBootloader = true;
            if (_activeMode == Mode::NONE) {
                _activeMode = Mode::TIMEOUT;
            }
        }
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
        if (_ledsEnabled) {
            GPIO::enableOutput(PIN_LED_BL, GPIO::LOW);
            GPIO::enableOutput(PIN_LED_WRITE, GPIO::HIGH);
            GPIO::enableOutput(PIN_LED_ERROR, GPIO::HIGH);
        }


        uint8_t pageBuffer[Flash::FLASH_PAGE_SIZE_BYTES];
        int currentPage = -1;
        int frameCounter = 0;
        memset(pageBuffer, 0, BUFFER_SIZE);

        // Wait for instructions on any enabled channel
        while (!_exitBootloader) {
            if (!_connected) {
                // Blink rapidly
                if (_ledsEnabled) {
                    GPIO::set(PIN_LED_BL, GPIO::LOW);
                    Core::sleep(20);
                    GPIO::set(PIN_LED_BL, GPIO::HIGH);
                    Core::sleep(150);
                }

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

                        // Turn on the LED
                        if (_ledsEnabled) {
                            GPIO::set(PIN_LED_BL, false);
                        }
                        Core::sleep(50);
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
                    if (_bufferCursor == 0) {
                        if (USART::peek(USART_PORT) == ':') {
                            // Start of a new frame
                            _buffer[0] = USART::read(USART_PORT);
                            _bufferCursor++;
                            frameCounter++;
                        } else {
                            // Ignore this byte
                        }

                    } else if (_bufferCursor > 0 && USART::contains(USART_PORT, '\n')) {
                        // A complete frame has been received, read it
                        _bufferCursor += USART::readUntil(USART_PORT, _buffer + _bufferCursor, BUFFER_SIZE, '\n');
                        _bufferFull = true;
                    }
                }

                // Handle a frame
                if (_bufferFull) {
                    // cf https://en.wikipedia.org/wiki/Intel_HEX
                    int cursor = 1;
                    int nBytes = parseHex(_buffer, cursor, 2);
                    cursor += 2;
                    uint16_t addr = parseHex(_buffer, cursor, 4);
                    int page = addr / Flash::FLASH_PAGE_SIZE_BYTES;
                    int offset = addr % Flash::FLASH_PAGE_SIZE_BYTES;
                    cursor += 4;
                    uint8_t command = parseHex(_buffer, cursor, 2);
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
                        _error = BLError::CHECKSUM;
                        if (_ledsEnabled) {
                            GPIO::set(PIN_LED_ERROR, false);
                        }
                        if (_activeChannel == Channel::USART) {
                            USART::write(USART_PORT, '2');
                        }
                        while (1); // Stall
                    }

                    // Command 0x00 is a data frame
                    if (command == 0x00) {
                        // Bootloader's flash domain is protected
                        if (page < BOOTLOADER_N_FLASH_PAGES) {
                            // Error
                            _status = Status::ERROR;
                            _error = BLError::PROTECTED_AREA;
                            if (_ledsEnabled) {
                                GPIO::set(PIN_LED_ERROR, false);
                            }
                            if (_activeChannel == Channel::USART) {
                                USART::write(USART_PORT, '1');
                            }
                            while (1); // Stall
                        }

                        // Change page if necessary
                        if (currentPage != page) {
                            if (currentPage != -1) {
                                if (_ledsEnabled) {
                                    GPIO::set(PIN_LED_WRITE, false);
                                }
                                // Write the previous page
                                Flash::writePage(currentPage, (uint32_t*)pageBuffer);
                                if (_ledsEnabled) {
                                    GPIO::set(PIN_LED_WRITE, true);
                                }
                            }
                            // Reset page buffer
                            currentPage = page;
                            memset(pageBuffer, 0, Flash::FLASH_PAGE_SIZE_BYTES);
                        }

                        // Save data
                        for (int i = 0; i < nBytes; i++) {
                            pageBuffer[offset + i] = parseHex(_buffer, cursor + 2 * i, 2);
                        }

                    } else if (command == 0x01) {
                        // End of file
                        if (_ledsEnabled) {
                            GPIO::set(PIN_LED_WRITE, false);
                        }
                        Flash::writePage(currentPage, (uint32_t*)pageBuffer);
                        if (_ledsEnabled) {
                            GPIO::set(PIN_LED_WRITE, true);
                        }
                        Core::sleep(100);
                        _exitBootloader = true;
                    }

                    _bufferFull = false;
                    _bufferCursor = 0;
                    // In USART mode, send acknowledge every 5 frames
                    if (_activeChannel == Channel::USART && frameCounter == 5) {
                        USART::write(USART_PORT, '0');
                        frameCounter = 0;
                    }
                    _status = Status::READY;
                }
            }
        }

        // Use the Watchdog Timer (WDT) with the minimum delay to generate
        // a WDT reset, which will reboot the chip without entering the 
        // bootloader (see above the PM::resetCause() condition)
        // This way, all the resources reserved and peripherals configured
        // by the bootloader are guaranted to be freed and reinitialised 
        WDT::enable(0);
        while (1); // Wait for the watchdog reset to kick in

    } else {
        // Execute the user code
        uint32_t userResetHandlerAddress = (*(volatile uint32_t*)(BOOTLOADER_N_FLASH_PAGES * Flash::FLASH_PAGE_SIZE_BYTES + 0x04));
        void (*userResetHandler)() = (void (*)())(userResetHandlerAddress);
        userResetHandler();
        //while (1);
    }
}
