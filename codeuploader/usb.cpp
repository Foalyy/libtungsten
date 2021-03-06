#include "usb.h"
#include <stdio.h>
#include <iostream>
#include <string>
#include <libusb-1.0/libusb.h>


static const int TIMEOUT = 2000; // ms

static libusb_device_handle* _handle = nullptr;
static bool _initialized = false;

std::string LIBUSB_ERROR_STRINGS[] = {
    "SUCCESS",
    "ERROR_IO",
    "ERROR_INVALID_PARAM",
    "ERROR_ACCESS",
    "ERROR_NO_DEVICE",
    "ERROR_NOT_FOUND",
    "ERROR_BUSY",
    "ERROR_TIMEOUT",
    "ERROR_OVERFLOW",
    "ERROR_PIPE",
    "ERROR_INTERRUPTED",
    "ERROR_NO_MEM",
    "ERROR_NOT_SUPPORTED"
};


void printLibUSBError(std::string message, int r);

int usbInit() {
    int r = libusb_init(NULL);
    if (r < 0) {
        printLibUSBError("Unable to initialize libusb", r);
        return r;
    }
    _initialized = true;
    return r;
}

int usbOpenDevice(uint16_t vid, uint16_t pid, uint8_t interface) {
    int r = 0;

    // Find a matching device
    r = findBoard(vid, pid);
    if (r < 0) {
        return r;
    }

    // Set the device's default configuration
    r = libusb_set_configuration(_handle, 0);
    if (r < 0) {
        printLibUSBError("libusb_set_configuration failed", r);
        libusb_close(_handle);
        return r;
    }

    // Remove any active driver to take control of the interface
    if (libusb_kernel_driver_active(_handle, interface)) {
        r = libusb_detach_kernel_driver(_handle, interface);
        if (r < 0) {
            printLibUSBError("Unable to detach kernel driver", r);
            libusb_close(_handle);
            return r;
        }
    }

    // Claim the interface
    r = libusb_claim_interface(_handle, interface);
    if (r < 0) {
        printLibUSBError("Warning: unable to claim device's interface", r);
    }

    return 0;
}

void usbCloseDevice() {
    if (_handle != nullptr) {
        libusb_close(_handle);
        _handle = nullptr;
    }
}

void usbExit() {
    if (_initialized) {
        usbCloseDevice();
        libusb_exit(NULL);
        _initialized = false;
    }
}

int findBoard(uint16_t vid, uint16_t pid) {
    int r = 0;
    int returnCode = 0;

    // Get the list of devices
    libusb_device** list;
    libusb_device* board = nullptr;
    ssize_t cnt = libusb_get_device_list(NULL, &list);
    if (cnt < 0) {
        printLibUSBError("Unable to find the list of usb devices", r);
        return r;
    }

    // Look for a device with the matching vendor and product ids
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device *device = list[i];
        struct libusb_device_descriptor desc;
        r = libusb_get_device_descriptor(device, &desc);
        if (r < 0) {
            std::cerr << "Warning : unable to get descriptor for device (bus "
                      << libusb_get_bus_number(device) << ", device " << libusb_get_device_address(device) << ")" << std::endl;
            continue;
        }
        if (desc.idVendor == vid && desc.idProduct == pid) {
            // Found matching device
            if (board == nullptr) {
                board = device;
            } else {
                std::cout << "Warning : more than one matching device found, are there multiple boards plugged in? The first match will be used." << std::endl;
                break;
            }
        }
    }

    if (board != nullptr) {
        // Open the device
        r = libusb_open(board, &_handle);
        if (r < 0) {
            //std::cerr << "Unable to open device 0x" << std::hex << vid << ":0x" << pid << std::dec << " : error " << r << std::endl;
            printLibUSBError("Unable to open device", r);
            if (r == LIBUSB_ERROR_IO) {
                std::cerr << "Please try again" << std::endl;
            }
            returnCode = r;
        }
    } else {
        returnCode = LIBUSB_ERROR_NO_DEVICE;
    }

    // Free the device list and return true if a device was found and opened correctly
    libusb_free_device_list(list, 1);
    return returnCode;
}

// cf http://www.beyondlogic.org/usbnutshell/usb6.shtml
int sendRequest(uint8_t request, uint16_t value, uint16_t index, Direction direction, uint8_t* buffer, uint16_t length) {
    uint8_t bmRequestType
        = static_cast<int>(direction) << 7  // Direction
        | 2 << 5                            // Type : vendor
        | 0;                                // Recipent : device
    int r = libusb_control_transfer(_handle, bmRequestType, request, value, index, buffer, length, TIMEOUT);
    if (r < 0) {
        printLibUSBError("Error during request transfer", r);
        exit(0);
    }
    return r;
}

uint8_t ask(uint8_t request, uint16_t value, uint16_t index) {
    uint8_t buffer = 0;
    sendRequest(request, value, index, Direction::INPUT, &buffer, 1);
    return buffer;
}

void printLibUSBError(std::string message, int r) {
    std::cerr << message << " : ";
    if (-r <= 12) {
        std::cerr << LIBUSB_ERROR_STRINGS[-r] << std::endl;
    } else {
        std::cerr << "error " << r << std::endl;
    }
}