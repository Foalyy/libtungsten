#include "carbide.h"
#include <core.h>
#include <error.h>
#include <scif.h>
#include <pm.h>
#include <gpio.h>
#include <usb.h>

namespace Carbide {

    // USB request codes
    enum class Request {
        START_BOOTLOADER,
        CONNECT,
        STATUS,
        WRITE,
        GET_ERROR,
    };

    // Handler called when a CONTROL packet is sent over USB
    void usbControlHandler(USB::SetupPacket &lastSetupPacket, uint8_t* data, int &size) {
        Request request = static_cast<Request>(lastSetupPacket.bRequest);
        if (request == Request::START_BOOTLOADER) {
            lastSetupPacket.handled = true;
            Core::reset();
        }
    }

    void warningHandler(Error::Module module, int userModule, Error::Code code) {
        setLedR(false);
        Core::sleep(100);
        setLedR();
        Core::sleep(100);
        setLedR(false);
        Core::sleep(100);
        setLedR();
        Core::sleep(100);
        setLedR(false);
        Core::sleep(100);
    }

    void criticalHandler(Error::Module module, int userModule, Error::Code code) {
        while (1) {
            setLedR();
            Core::sleep(100);
            setLedR(false);
            Core::sleep(100);
        }
    }

    void init() {
        // Init the microcontroller on the default 12MHz clock
        Core::init();
        SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_12MHZ);
        PM::setMainClockSource(PM::MainClockSource::RCFAST);

        // Init the USB port start the bootloader when requested
        //USB::initDevice();
        //USB::setControlHandler(usbControlHandler);

        // Init the leds and button
        initLeds();
        initButton();

        // Set error handlers
        Error::setHandler(Error::Severity::WARNING, warningHandler);
        Error::setHandler(Error::Severity::CRITICAL, criticalHandler);
    }

}
