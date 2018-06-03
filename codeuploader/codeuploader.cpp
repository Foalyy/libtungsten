#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <boost/asio.hpp> 
#include <chrono>
#include <thread>
#include "usb.h"

using namespace std;
using namespace boost;

const uint16_t VENDOR_ID = 0x1209;
const uint16_t PRODUCT_ID = 0xCA4B;

const bool DEBUG = false;

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
string ERROR_STRINGS[] = {
    "NONE",
    "CHECKSUM_MISMATCH",
    "PROTECTED_AREA",
    "UNKNOWN_RECORD_TYPE",
    "OVERFLOW",
};

//unsigned int parseHex(const string& str, int pos=0, int n=2);
bool waitReady();
void debug(const char* str);
uint8_t ask(Request request, uint16_t value=0, uint16_t index=0);
int sendRequest(Request request, uint16_t value=0, uint16_t index=0, Direction direction=Direction::OUTPUT, uint8_t* buffer=nullptr, uint16_t length=0);


// Open an ihex file and send it to the bootloader
int main(int argc, char** argv) {
    // Parse arguments
    string filename = "";
    if (argc >= 2) {
        filename = argv[1];
    } else {
        cerr << "Usage : " << argv[0] << " <ihexfile> [serialport]" << endl;
        return -1;
    }
    string serialPortName = "";
    if (argc >= 3) {
        serialPortName = argv[2];
    }
    cout << endl;

    // Try to access bootloader
    int r = 0;
    bool useSerial = false;
    asio::io_service io;
    asio::serial_port serial(io);
    if (serialPortName.empty()) { // USB
        // Init usb library
        r = usbInit();
        if (r < 0) {
            return r;
        }

        // Try to find and open a device
        r = usbOpenDevice(VENDOR_ID, PRODUCT_ID);
        if (r == LIBUSB_ERROR_NO_DEVICE) {
            cout << "Device not found. Are you sure the cable is plugged and the bootloader is started?" << endl;
        }
        if (r < 0) {
            usbExit();
            return r;
        }

        // Send a START_BOOTLOADER request and close the connection
        debug("Sending START_BOOTLOADER request");
        sendRequest(Request::START_BOOTLOADER);
        usbCloseDevice();
        debug("Closing device connection");

        // Give the board some time to reboot into bootloader mode
        this_thread::sleep_for(chrono::milliseconds(2000));

        // Try to find and open the board again
        r = usbOpenDevice(VENDOR_ID, PRODUCT_ID);
        if (r < 0) {
            cout << "Unable to open device : error " << r << endl;
            usbExit();
            return r;
        }

        // Send a CONNECT request and wait for the bootloader to be ready
        debug("Sending CONNECT request");
        sendRequest(Request::CONNECT);
        waitReady();
        cout << "Connected to bootloader" << endl;

    } else { // Serial
        useSerial = true;
        cout << "Opening " << serialPortName << "..." << endl;
        serial.open(serialPortName);
        serial.set_option(asio::serial_port_base::baud_rate(115200));
        
        cout << "Connecting to bootloader... ";
        asio::write(serial, asio::buffer("SYN", 3));
        const int bufferSize = 3;
        char buffer[] = {0, 0, 0};
        while (strncmp(buffer, "ACK", bufferSize) != 0) {
            char c = 0;
            asio::read(serial, asio::buffer(&c, 1));
            for (int i = 0; i < bufferSize - 1; i++) {
                buffer[i] = buffer[i + 1];
            }
            buffer[bufferSize - 1] = c;
        }
        cout << "connected" << endl;
    }

    // Open and read the HEX file
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error : no such file" << endl;
        usbExit();
        return -4;
    }
    string line = "";
    std::vector<string> lines;
    while (getline(file, line)) {
        lines.push_back(line);
    }

    // Upload
    cout << "Uploading... ";
    bool error = false;
    const char endOfLine = '\n';
    unsigned int s = lines.size();
    int frameNumber = 0;
    int lastp = -1;
    for (unsigned int i = 0; i < s; i++) {
        line = lines.at(i);

        // Compute percentage
        int p = 100 * (i + 1) / s;
        if (p > lastp) {
            if (i > 0) {
                cout << "\b\b\b";
            }
            if (p < 10) {
                cout << "0";
            }
            cout << p << "%" << flush;
        }

        // Remove newline at the end
        while (line.back() == '\r' || line.back() == '\n') {
            line.pop_back();
        }

        // Check start code
        if (line.length() == 0) {
            continue;
        } else if (line[0] != ':') {
            cout << "Warning : ignoring line " << i + 1 << " not starting with ':'" << endl;
            continue;
        }

        // Send line
        if (useSerial) {
            asio::write(serial, asio::buffer(line, line.size()));
            asio::write(serial, asio::buffer(&endOfLine, 1));

        } else {
            debug("Sending WRITE request");
            //cout << line << endl;
            sendRequest(Request::WRITE, 0, frameNumber, Direction::OUTPUT, (uint8_t*)line.data(), line.size());
            frameNumber++;
            if (i < s - 1) { // If not last frame
                bool ready = waitReady();
                if (!ready) {
                    error = true;
                    break;
                }
            }
        }

        if (useSerial) {
            // Wait for acknowledge every 5 frames
            if (i % 5 == 4) {
                char c = 0;
                asio::read(serial, asio::buffer(&c, 1));
                if (c != '0') {
                    cout << "error " << c << endl;
                    error = true;
                    break;
                } else {
                    //cout << "OK" << endl;
                }
            }
        }
    }
    cout << endl;
    file.close();

    if (!error) {
        cout << endl;
        cout << "Firmware uploaded successfully!" << endl;
    }
    
    // Close the ports
    serial.close();
    usbExit();
}

uint8_t ask(Request request, uint16_t value, uint16_t index) {
    return ask(static_cast<uint8_t>(request), value, index);
}

Status askStatus() {
    Status status = static_cast<Status>(ask(Request::STATUS));
    if (status == Status::ERROR) {
        BLError error = static_cast<BLError>(ask(Request::GET_ERROR));
        cout << "Error " << ERROR_STRINGS[static_cast<int>(error)] << endl;
        if (error == BLError::PROTECTED_AREA) {
            cout << "This HEX file contains data required to be placed in the protected area at the beginning of the internal Flash where"
            " the bootloader lives. Make sure you have compiled with BOOTLOADER=true." << endl;
        }
    }
    return status;
}

bool waitReady() {
    debug("Waiting for READY status");
    Status status;
    while (true) {
        status = askStatus();
        if (status == Status::READY) {
            debug("READY");
            return true;
        } else if (status == Status::ERROR) {
            return false;
        } else {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    };
    return false;
}

void debug(const char* str) {
    if (DEBUG) {
        printf(str);
        printf("\n");
    }
}

int sendRequest(Request request, uint16_t value, uint16_t index, Direction direction, uint8_t* buffer, uint16_t length) {
    return sendRequest(static_cast<uint8_t>(request), value, index, direction, buffer, length);
}
