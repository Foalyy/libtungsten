#include "adc.h"
#include "pm.h"

namespace ADC {

    // Package-dependant, defined in pins_sam4l_XX.cpp
    extern struct GPIO::Pin PINS[];

    // Bitset representing enabled channels
    uint16_t _enabledChannels = 0x0000;

    // Keep track of the state of the module
    bool _initialized = false;

    // Analog reference selected by the user
    AnalogReference _analogReference = AnalogReference::INTERNAL_1V;

    // Reference voltage value in mV
    // For VCC_0625 and VCC_OVER_2, this is simply the Vcc voltage
    int _vref = 0;


    // Initialize the common ressources of the ADC controller
    void init(AnalogReference analogReference, int vref) {
        // Voltage reference
        if (analogReference == AnalogReference::INTERNAL_1V) {
            vref = 1000;
        }
        _analogReference = analogReference;
        _vref = vref;

        // Enable the clock
        PM::enablePeripheralClock(PM::CLK_ADC);

        // CR (Control Register) : enable the ADC
        (*(volatile uint32_t*)(ADC_BASE + OFFSET_CR))
            = 1 << CR_EN        // EN : enable ADC
            | 1 << CR_REFBUFEN  // REFBUFEN : enable reference buffer
            | 1 << CR_BGREQEN;  // BGREQEN : enable bandgap voltage reference

        // CFG (Configuration Register) : set general settings
        (*(volatile uint32_t*)(ADC_BASE + OFFSET_CFG))
            = static_cast<int>(analogReference) << CFG_REFSEL   // REFSEL : voltage reference
            | 0b11 << CFG_SPEED                                 // SPEED : 75ksps
            | 1 << CFG_CLKSEL                                   // CLKSEL : use APB clock
            | 0b000 << CFG_PRESCAL;                             // PRESCAL : divide clock by 4

        // SR (Status Register) : wait for enabled status flag
        while (!((*(volatile uint32_t*)(ADC_BASE + OFFSET_SR)) & (1 << SR_EN)));

        // Update the module status
        _initialized = true;
    }

    void enable(Channel channel) {
        // Set the pin in peripheral mode
        GPIO::enablePeripheral(PINS[channel]);

        // Initialize and enable the ADC controller if necessary
        if (!_initialized) {
            init();
        }
        _enabledChannels |= 1 << channel;
    }

    void disable(Channel channel) {
        // Disable the peripheral mode on the pin
        GPIO::disablePeripheral(PINS[channel]);

        // Disable the ADC controller if necessary
        _enabledChannels &= ~(uint32_t)(1 << channel);
        if (_enabledChannels == 0x0000) {
            // CR (Control Register) : disable the ADC
            (*(volatile uint32_t*)(ADC_BASE + OFFSET_CR))
                = 1 << CR_DIS;      // DIS : disable ADC
            _initialized = false;
        }
    }

    // Read the current raw value measured by the ADC on the given channel
    int readRaw(Channel channel, Gain gain) {
        // Enable this channel if it is not already
        if (!(_enabledChannels & 1 << channel)) {
            enable(channel);
        }

        // SEQCFG (Sequencer Configuration Register) : setup the conversion
        (*(volatile uint32_t*)(ADC_BASE + OFFSET_SEQCFG))
            = 0 << SEQCFG_HWLA                        // HWLA : Half Word Left Adjust disabled
            | 0 << SEQCFG_BIPOLAR                     // BIPOLAR : single-ended mode
            | static_cast<int>(gain) << SEQCFG_GAIN   // GAIN : user-selected gain
            | 1 << SEQCFG_GCOMP                       // GCOMP : gain error reduction enabled
            | 0b000 << SEQCFG_TRGSEL                  // TRGSEL : software trigger
            | 0 << SEQCFG_RES                         // RES : 12-bits resolution
            | 0b10 << SEQCFG_INTERNAL                 // INTERNAL : POS external, NEG internal
            | (channel & 0b1111) << SEQCFG_MUXPOS     // MUXPOS : selected channel
            | 0b111 << SEQCFG_MUXNEG                  // MUXNEG : pad ground
            | 0b000 << SEQCFG_ZOOMRANGE;              // ZOOMRANGE : default

        // CR (Control Register) : start conversion
        (*(volatile uint32_t*)(ADC_BASE + OFFSET_CR))
            = 1 << CR_STRIG;    // STRIG : Sequencer Trigger

        // SR (Status Register) : wait for Sequencer End Of Conversion status flag
        while (!((*(volatile uint32_t*)(ADC_BASE + OFFSET_SR)) & (1 << SR_SEOC)));

        // SCR (Status Clear Register) : clear Sequencer End Of Conversion status flag
        (*(volatile uint32_t*)(ADC_BASE + OFFSET_SCR)) = 1 << SR_SEOC;

        // LCV (Last Converted Value) : conversion result
        return (*(volatile uint32_t*)(ADC_BASE + OFFSET_LCV)) & 0xFFFF;
    }

    // Return the current value on the given channel in mV
    int read(Channel channel, Gain gain) {
        uint32_t value = readRaw(channel, gain);

        // Compute reference
        int vref = _vref;
        if (_analogReference == AnalogReference::VCC_0625) {
            vref = (vref * 625) / 1000;
        } else if (_analogReference == AnalogReference::VCC_OVER_2) {
            vref = vref / 2;
        } else if (_analogReference == INTERNAL_1V) {
            vref = 1000;
        }

        // Convert the result to mV
        // value = voltage * gain * 4095 * ref
        // <=> voltage = value * ref / (gain * 4095)
        int gainCoefficients[] = {1, 2, 4, 8, 16, 32, 64};
        if (gain == Gain::X05) {
            value *= 2;
        }
        value *= vref;
        value /= 4095;
        if (gain != Gain::X05) {
            value /= gainCoefficients[static_cast<int>(gain)];
        }

        return value;
    }

    void setPin(Channel channel, GPIO::Pin pin) {
        PINS[channel] = pin;
    };

}