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

enum Request {
    START_BOOTLOADER,
    CONNECT,
    STATUS,
    WRITE,
    GET_ERROR,
};

enum class Status {
    READY,
    BUSY,
    ERROR,
};

unsigned int parseHex(const string& str, int pos=0, int n=2);
void waitReady();

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

    // Try to access bootloader
    bool useSerial = false;
    asio::io_service io;
    asio::serial_port serial(io);
    if (serialPortName.empty()) {
        // USB
        int r = initUSB(0x1209, 0x0001);
        if (r == LIBUSB_ERROR_NO_DEVICE) {
            cout << endl << "Device not found. Are you sure the cable is plugged and the bootloader started?" << endl;
        }
        if (r < 0) {
            return 0;
        }
        sendRequest(START_BOOTLOADER);
        closeUSB();
        this_thread::sleep_for(chrono::milliseconds(10));
        int timeout = 500;
        for (int i = 0; i < timeout; i++) {
            int r = initUSB(0x1209, 0x0001);
            if (r == 0) {
                // Connected!
                break;
            } else if (r != LIBUSB_ERROR_NO_DEVICE) {
                // Error
                cout << "Error " << r << endl;
                return -1;
            }
            this_thread::sleep_for(chrono::milliseconds(10));
            if (i == timeout - 1) {
                cout << "timeout" << endl;
                return -1;
            }
        }
        sendRequest(CONNECT);
        waitReady();

    } else {
        // Serial
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

    // Open and read file
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error : no such file" << endl;
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


        int p = 100 * (i + 1) / s;
        if (p > lastp) {
            if (i > 0) {
                cout << "\b\b\b";
            }
            if (p < 10) {
                cout << "0";
            }
            cout << p << "%" << flush;
            //cout << "Line " << i + 1 << " : " << line << endl;
        }

        // Remove newline at the end
        while (line.back() == '\r' || line.back() == '\n') {
            line.pop_back();
        }

        // Check start code
        if (line[0] != ':') {
            continue;
        }

        // Send line
        if (useSerial) {
            asio::write(serial, asio::buffer(line, line.size()));
            asio::write(serial, asio::buffer(&endOfLine, 1));

        } else {
            sendRequest(WRITE, 0, frameNumber, Direction::OUTPUT, (uint8_t*)line.data(), line.size());
            frameNumber++;
            if (i < s - 1) { // If not last frame
                waitReady();
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
        cout << "============================================" << endl;
        cout << "== Firmware uploaded successfully, enjoy! ==" << endl;
        cout << "============================================" << endl;
    }
    
    // Close the serial port
    serial.close();
}

void waitReady() {
    Status status;
    while (true) {
        status = static_cast<Status>(ask(STATUS));
        if (status == Status::READY) {
            break;
        } else if (status == Status::ERROR) {
            cerr << "Error " << static_cast<int>(status) << endl;
            break;
        } else {
            this_thread::sleep_for(chrono::milliseconds(3));
        }
    };
}