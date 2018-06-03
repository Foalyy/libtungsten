#ifndef _USB_H_
#define _USB_H_

#include <stdlib.h>
#include <libusb-1.0/libusb.h>

enum class Direction {
    OUTPUT = 0,
    INPUT = 1,
};

int usbInit();
int usbOpenDevice(uint16_t vid, uint16_t pid, uint8_t interface=0);
int findBoard(uint16_t vid, uint16_t pid);
void usbCloseDevice();
void usbExit();
int sendRequest(uint8_t request, uint16_t value=0, uint16_t index=0, Direction direction=Direction::OUTPUT, uint8_t* buffer=nullptr, uint16_t length=0);
uint8_t ask(uint8_t request, uint16_t value=0, uint16_t index=0);

#endif