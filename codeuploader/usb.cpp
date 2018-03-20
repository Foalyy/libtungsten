#include "usb.h"
#include <stdio.h>
#include <libusb-1.0/libusb.h>

static const int TIMEOUT = 1000; // ms
static libusb_device_handle* _handle = nullptr;

static const bool DEBUG = true;

int initUSB(uint16_t vid, uint16_t pid, uint8_t interface) {
    int r = libusb_init(NULL);
    if (r < 0) {
        return r;
    }

    // Open the device with requested VID/PID
    _handle = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (_handle == NULL) {
        if (DEBUG) {
            printf("Unable to find device with VID %x/PID %x\n", vid, pid);
        }
        return LIBUSB_ERROR_NO_DEVICE;
    }

    // Select the default configuration
    r = libusb_set_configuration(_handle, 0);
    if (r != 0) {
        if (DEBUG) {
            printf("Error during SET_CONFIGURATION : error %d\n", r);
        }
        libusb_close(_handle);
        return r;
    }

    // Remove any active driver to take control of the interface
    if (libusb_kernel_driver_active(_handle, interface)) {
        r = libusb_detach_kernel_driver(_handle, interface);
        if (r < 0) {
            if (DEBUG) {
                printf("Cannot detach kernel driver : error %d\n", r);
            }
            libusb_close(_handle);
            return r;
        }
    }

    // Claim the interface
    r = libusb_claim_interface(_handle, interface);
    if (r < 0) {
        if (DEBUG) {
            printf("Cannot claim interface %d : error %d\n", interface, r);
        }
        libusb_close(_handle);
        return r;
    }

    return 0;
}

void closeUSB() {
    libusb_close(_handle);
    libusb_exit(NULL);
}

// cf http://www.beyondlogic.org/usbnutshell/usb6.shtml
int sendRequest(uint8_t request, uint16_t value, uint16_t index, Direction direction, uint8_t* buffer, uint16_t length) {
    uint8_t bmRequestType
        = static_cast<int>(direction) << 7  // Direction
        | 2 << 5                            // Type : vendor
        | 0;                                // Recipent : device
    int r = libusb_control_transfer(_handle, bmRequestType, request, value, index, buffer, length, TIMEOUT);
    if (r < 0) {
        if (DEBUG) {
            printf("Error during request transfer : %d\n", r);
        }
        return r;
    }
    return r;
}

uint8_t ask(uint8_t request, uint16_t value, uint16_t index) {
    uint8_t buffer = 0;
    sendRequest(request, value, index, Direction::INPUT, &buffer, 1);
    return buffer;
}
