#include "carbide.h"
#include <core.h>
#include <error.h>
#include <scif.h>
#include <pm.h>
#include <bpm.h>
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
        setCPUFrequency(CPUFreq::FREQ_12MHZ);

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

    void setCPUFrequency(CPUFreq frequency) {
        switch (frequency) {
            case CPUFreq::FREQ_4MHZ:
                SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_4MHZ);
                PM::setMainClockSource(PM::MainClockSource::RCFAST);
                break;

            case CPUFreq::FREQ_8MHZ:
                SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_8MHZ);
                PM::setMainClockSource(PM::MainClockSource::RCFAST);
                break;

            case CPUFreq::FREQ_12MHZ:
                SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_12MHZ);
                PM::setMainClockSource(PM::MainClockSource::RCFAST);
                break;

            case CPUFreq::FREQ_24MHZ:
                SCIF::enableDFLL(24000000UL);
                PM::setMainClockSource(PM::MainClockSource::DFLL);
                break;

            case CPUFreq::FREQ_36MHZ:
                SCIF::enableDFLL(36000000UL);
                PM::setMainClockSource(PM::MainClockSource::DFLL);
                break;

            case CPUFreq::FREQ_48MHZ:
                BPM::setPowerScaling(BPM::PowerScaling::PS2);
                SCIF::enableDFLL(48000000UL);
                PM::setMainClockSource(PM::MainClockSource::DFLL);
                break;
        }

        // Wait 100ms to make sure the clocks have stabilized
        Core::sleep(100);
    }

}
