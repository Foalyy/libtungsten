#include "tc.h"
#include "core.h"
#include "pm.h"
#include "scif.h"
#include <string.h>

namespace TC {

    // Package-dependant, defined in pins_sam4l_XX.cpp
    extern struct GPIO::Pin PINS[MAX_N_TC][N_COUNTERS_PER_TC * N_CHANNELS_PER_COUNTER];
    extern struct GPIO::Pin PINS_CLK[MAX_N_TC][N_EXTERNAL_CLOCKS_PER_TC];

    // Keep track of pins already enabled in Peripheral mode
    bool _init = false;
    bool _pinsEnabled[MAX_N_TC][N_COUNTERS_PER_TC * N_CHANNELS_PER_COUNTER];
    bool _pinsCLKEnabled[MAX_N_TC][N_EXTERNAL_CLOCKS_PER_TC];

    // Used to save the counters' current configuration
    struct CounterConfig {
        SourceClock sourceClock;
        unsigned long sourceClockFrequency;
    };
    CounterConfig _countersConfig[MAX_N_TC][N_COUNTERS_PER_TC];

    // Interrupts
    void enableInterrupt(Counter counter);
    void interruptHandlerWrapper();
    void (*_counterOverflowHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    bool _counterOverflowHandlerEnabled[MAX_N_TC][N_COUNTERS_PER_TC];
    void (*_counterOverflowInternalHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    void (*_rbLoadingHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    bool _rbLoadingHandlerEnabled[MAX_N_TC][N_COUNTERS_PER_TC];
    void (*_rbLoadingInternalHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    void (*_rcCompareHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    bool _rcCompareHandlerEnabled[MAX_N_TC][N_COUNTERS_PER_TC];
    void (*_rcCompareInternalHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    volatile uint32_t _sr = 0;

    // Simple counter mode
    uint32_t _counterModeMaxValue[MAX_N_TC][N_COUNTERS_PER_TC];
    uint16_t _counterModeMSB[MAX_N_TC][N_COUNTERS_PER_TC];
    void (*_counterModeFullHandler[MAX_N_TC][N_COUNTERS_PER_TC])(Counter counter);
    bool _counterModeFullHandlerEnabled[MAX_N_TC][N_COUNTERS_PER_TC];
    void simpleCounterOverflowHandler(Counter counter);
    void simpleCounterRCCompareHandler(Counter counter);

    // Measurement mode
    uint16_t _periodMSB[MAX_N_TC][N_COUNTERS_PER_TC];
    uint16_t _highTimeMSB[MAX_N_TC][N_COUNTERS_PER_TC];
    uint32_t _periodMSBInternal[MAX_N_TC][N_COUNTERS_PER_TC];
    uint32_t _highTimeMSBInternal[MAX_N_TC][N_COUNTERS_PER_TC];
    void measurementRCCompareHandler(Counter counter);
    void measurementOverflowHandler(Counter counter);
    void measurementRBLoadingHandler(Counter counter);
    const uint16_t MEASUREMENT_RC_TRIGGER = 0xE000;

    // Internal list of delayed callbacks to execute
    extern uint8_t INTERRUPT_PRIORITY;
    struct ExecDelayedData {
        uint32_t handler;
        int skipPeriods;
        int skipPeriodsReset;
        int rest;
        int restReset;
        bool repeat;
    };
    ExecDelayedData _execDelayedData[MAX_N_TC][N_COUNTERS_PER_TC];
    void execDelayedHandlerWrapper();

    // Internal functions
    inline void checkTC(Counter counter) {
        if (counter.tc + 1 > N_TC) {
            Error::happened(Error::Module::TC, ERR_INVALID_TC, Error::Severity::CRITICAL);
        }
    }
    inline void checkTC(Channel channel) {
        checkTC(channel.counter);
    }

    void init() {
        if (!_init) {
            memset(_pinsEnabled, 0, sizeof(_pinsEnabled));
            memset(_pinsCLKEnabled, 0, sizeof(_pinsCLKEnabled));
            memset(_countersConfig, 0, sizeof(_countersConfig));
            memset(_counterModeMSB, 0, sizeof(_counterModeMSB));
            memset(_counterOverflowHandler, 0, sizeof(_counterOverflowHandler));
            memset(_counterOverflowHandlerEnabled, 0, sizeof(_counterOverflowHandlerEnabled));
            memset(_counterOverflowInternalHandler, 0, sizeof(_counterOverflowInternalHandler));
            memset(_rbLoadingHandler, 0, sizeof(_rbLoadingHandler));
            memset(_rbLoadingHandlerEnabled, 0, sizeof(_rbLoadingHandlerEnabled));
            memset(_rbLoadingInternalHandler, 0, sizeof(_rbLoadingInternalHandler));
            memset(_rcCompareHandler, 0, sizeof(_rcCompareHandler));
            memset(_rcCompareHandlerEnabled, 0, sizeof(_rcCompareHandlerEnabled));
            memset(_rcCompareInternalHandler, 0, sizeof(_rcCompareInternalHandler));
            memset(_counterModeFullHandler, 0, sizeof(_counterModeFullHandler));
            memset(_counterModeFullHandlerEnabled, 0, sizeof(_counterModeFullHandlerEnabled));
            memset(_periodMSB, 0, sizeof(_periodMSB));
            memset(_highTimeMSB, 0, sizeof(_highTimeMSB));
            memset(_periodMSBInternal, 0, sizeof(_periodMSB));
            memset(_highTimeMSBInternal, 0, sizeof(_highTimeMSB));
            memset(_execDelayedData, 0, sizeof(_execDelayedData));
            _init = true;
        }
    }

    void initCounter(Counter counter, SourceClock sourceClock, unsigned long sourceClockFrequency) {
        init();

        // Save the config
        _countersConfig[counter.tc][counter.n].sourceClock = sourceClock;
        _countersConfig[counter.tc][counter.n].sourceClockFrequency = sourceClockFrequency;

        // Enable the module clock
        PM::enablePeripheralClock(PM::CLK_TC0 + counter.tc);

        // Enable the divided clock powering the counter
        if (sourceClock == SourceClock::PBA_OVER_2) {
            PM::enablePBADivClock(1); // 2^1 = 2
        } else if (sourceClock == SourceClock::PBA_OVER_8) {
            PM::enablePBADivClock(3); // 2^3 = 8
        } else if (sourceClock == SourceClock::PBA_OVER_32) {
            PM::enablePBADivClock(5); // 2^5 = 32
        } else if (sourceClock == SourceClock::PBA_OVER_128) {
            PM::enablePBADivClock(7); // 2^7 = 128
        }

        // Enable the external input clock pin
        if (sourceClock == SourceClock::CLK0 && !_pinsCLKEnabled[counter.tc][0]) {
            GPIO::enablePeripheral(PINS_CLK[counter.tc][0]);
            _pinsCLKEnabled[counter.tc][0] = true;
        } else if (sourceClock == SourceClock::CLK1 && !_pinsCLKEnabled[counter.tc][1]) {
            GPIO::enablePeripheral(PINS_CLK[counter.tc][1]);
            _pinsCLKEnabled[counter.tc][1] = true;
        } else if (sourceClock == SourceClock::CLK2 && !_pinsCLKEnabled[counter.tc][2]) {
            GPIO::enablePeripheral(PINS_CLK[counter.tc][2]);
            _pinsCLKEnabled[counter.tc][2] = true;
        }
    }

    void disable(Counter counter) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Stop the counter
        Channel channel = {
            .counter = counter,
            .line = TIOA
        };
        setRX(channel, 0);
        channel.line = TIOB;
        setRX(channel, 0);
        setRC(counter, 0);
        stop(counter);

        // Disable the interrupts
        (*(volatile uint32_t*)(REG + OFFSET_IDR0)) = 0xFFFFFFFF;
        _counterOverflowHandler[counter.tc][counter.n] = nullptr;
        _counterOverflowHandlerEnabled[counter.tc][counter.n] = false;
        _counterOverflowInternalHandler[counter.tc][counter.n] = nullptr;
        _rbLoadingHandler[counter.tc][counter.n] = nullptr;
        _rbLoadingHandlerEnabled[counter.tc][counter.n] = false;
        _rbLoadingInternalHandler[counter.tc][counter.n] = nullptr;
        _rcCompareHandler[counter.tc][counter.n] = nullptr;
        _rcCompareHandlerEnabled[counter.tc][counter.n] = false;
        _rcCompareInternalHandler[counter.tc][counter.n] = nullptr;
        _counterModeFullHandler[counter.tc][counter.n] = nullptr;
        _counterModeFullHandlerEnabled[counter.tc][counter.n] = false;

        // Disable the output pins
        for (int i = 0; i < N_CHANNELS_PER_COUNTER; i++) {
            if (_pinsEnabled[counter.tc][N_CHANNELS_PER_COUNTER * counter.n + i]) {
                GPIO::disablePeripheral(PINS[counter.tc][N_CHANNELS_PER_COUNTER * counter.n + i]);
                _pinsEnabled[counter.tc][N_CHANNELS_PER_COUNTER * counter.n + i] = false;
            }
        }

        // Disable the external input clock pin
        if (_pinsCLKEnabled[counter.tc][counter.n]) {
            GPIO::disablePeripheral(PINS_CLK[counter.tc][counter.n]);
            _pinsCLKEnabled[counter.tc][counter.n] = false;
        }
    }


    // Simple counter mode

    void enableSimpleCounter(Counter counter, uint32_t maxValue, SourceClock sourceClock, unsigned long sourceClockFrequency, bool invert, bool upDown) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // When up-down mode is enabled the counter is limited to 16 bits
        if (upDown && maxValue > 0xFFFF) {
            maxValue = 0xFFFF;
        }

        // Initialize the counter and its clock
        initCounter(counter, sourceClock, sourceClockFrequency);

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect disabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // CCR (Channel Control Register) : disable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKDIS;       // CLKDIS : disable the clock

        // Reset the MSB of the counter
        _counterModeMSB[counter.tc][counter.n] = 0;

        // Save the max value
        _counterModeMaxValue[counter.tc][counter.n] = maxValue;

        // Set the RC register with the low 16 bits of the max value
        (*(volatile uint32_t*)(REG + OFFSET_RC0)) = maxValue & 0xFFFF;

        // Automatically enable 32-bit mode when maxValue does not fit on 16-bit
        uint8_t wavesel = 0;
        if (maxValue > 0xFFFF) {
            // Enable the Counter Overflow interrupt
            _counterOverflowInternalHandler[counter.tc][counter.n] = simpleCounterOverflowHandler;
            enableInterrupt(counter);
            (*(volatile uint32_t*)(REG + OFFSET_IER0))
                = 1 << SR_COVFS;    // SR_COVFS : counter overflow status

            // Disable automatic trigger on RC compare
            wavesel = 0;

        } else {
            // Enable automatic trigger on RC compare
            wavesel = upDown ? 3 : 2;
        }

        // CMR (Channel Mode Register) : setup the counter in Waveform Generation Mode
        (*(volatile uint32_t*)(REG + OFFSET_CMR0))
            =                         // TCCLKS : clock selection
              (static_cast<int>(sourceClock) & 0b111) << CMR_TCCLKS
            | invert << CMR_CLKI      // CLKI : clock invert
            | wavesel << CMR_WAVSEL   // WAVSEL : UP or UP/DOWN mode with or without automatic trigger on RC compare
            | 1 << CMR_WAVE;          // WAVE : waveform generation mode

        // CCR (Channel Control Register) : enable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKEN;        // CLKEN : enable the clock

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // Start the counter
        start(counter);
    }

    // Register an interrupt to be called when the max value of the counter has been reached
    void enableSimpleCounterFullInterrupt(Counter counter, void (*handler)(Counter)) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Save the handler and mark it as enabled
        if (handler != nullptr) {
            _counterModeFullHandler[counter.tc][counter.n] = handler;
        }
        _counterModeFullHandlerEnabled[counter.tc][counter.n] = true;

        // If maxValue > 0xFFFF, interrupts are already handled by the 32-bit counter mode and
        // the RC Compare interrupt will be enabled as needed by simpleCounterOverflowHandler()
        if (_counterModeMaxValue[counter.tc][counter.n] <= 0xFFFF) {
            // Enable the interrupts in this counter
            enableInterrupt(counter);

            // Enable the RC Compare interrupt
            _rcCompareInternalHandler[counter.tc][counter.n] = simpleCounterRCCompareHandler;
            (*(volatile uint32_t*)(REG + OFFSET_IER0))
                = 1 << SR_CPCS;    // SR_CPCS : RC compare status
        }
    }

    // Disable the Counter Full interrupt
    void disableSimpleCounterFullInterrupt(Counter counter) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Disable the handler
        _counterModeFullHandlerEnabled[counter.tc][counter.n] = false;

        // Make sure the interrupt is not used internally before disabling it
        if (_rcCompareInternalHandler[counter.tc][counter.n] == nullptr) {
            // IDR (Interrupt Disable Register) : disable the interrupt
            (*(volatile uint32_t*)(REG + OFFSET_IDR0))
                = 1 << SR_CPCS;    // SR_CPCS : RC compare status
        }
    }

    // Internal handler to handle 32-bit mode
    void simpleCounterOverflowHandler(Counter counter) {
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Increment the MSB of the counter
        _counterModeMSB[counter.tc][counter.n]++;

        // If the max value can be reached within the span of the next 16-bit counter, enable automatic trigger on RC compare
        if (_counterModeMSB[counter.tc][counter.n] == _counterModeMaxValue[counter.tc][counter.n] >> 16) {
            // WPMR (Write Protect Mode Register) : disable write protect
            (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
                = 0 << WPMR_WPEN            // WPEN : write protect disabled
                | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

            // CMR (Channel Mode Register) : enable automatic trigger on RC compare
            (*(volatile uint32_t*)(REG + OFFSET_CMR0)) |= 2 << CMR_WAVSEL;

            // Disable counter overflow interrupt
            _counterOverflowInternalHandler[counter.tc][counter.n] = nullptr;
            if (!_counterOverflowHandlerEnabled[counter.tc][counter.n]) {
                // IDR (Interrupt Disable Register) : disable the interrupt
                (*(volatile uint32_t*)(REG + OFFSET_IDR0))
                    = 1 << SR_COVFS;    // SR_COVFS : counter overflow status
            }

            // Enable the RC Compare interrupt
            _rcCompareInternalHandler[counter.tc][counter.n] = simpleCounterRCCompareHandler;
            (*(volatile uint32_t*)(REG + OFFSET_IER0))
                = 1 << SR_CPCS;    // SR_CPCS : RC compare status

            // WPMR (Write Protect Mode Register) : re-enable write protect
            (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
                = 1 << WPMR_WPEN            // WPEN : write protect enabled
                | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key
        }
    }

    // Internal handler to handle 32-bit mode
    void simpleCounterRCCompareHandler(Counter counter) {
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // 32-bit mode
        if (_counterModeMaxValue[counter.tc][counter.n] > 0xFFFF) {
            // Counter has reached its max value, reset the MSB of the counter
            _counterModeMSB[counter.tc][counter.n] = 0;

            // Disable RC compare interrupt
            _rcCompareInternalHandler[counter.tc][counter.n] = nullptr;
            if (!_rcCompareHandlerEnabled[counter.tc][counter.n]) {
                // IDR (Interrupt Disable Register) : disable the interrupt
                (*(volatile uint32_t*)(REG + OFFSET_IDR0))
                    = 1 << SR_CPCS;    // SR_CPCS : RC compare status
            }

            // Enable the Counter Overflow interrupt
            _counterOverflowInternalHandler[counter.tc][counter.n] = simpleCounterOverflowHandler;
            enableInterrupt(counter);
            (*(volatile uint32_t*)(REG + OFFSET_IER0))
                = 1 << SR_COVFS;    // SR_COVFS : counter overflow status

            // WPMR (Write Protect Mode Register) : disable write protect
            (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
                = 0 << WPMR_WPEN            // WPEN : write protect disabled
                | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

            // CMR (Channel Mode Register) : disable automatic trigger on RC compare
            (*(volatile uint32_t*)(REG + OFFSET_CMR0)) &= ~(uint32_t)(0b11 << CMR_WAVSEL);

            // WPMR (Write Protect Mode Register) : re-enable write protect
            (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
                = 1 << WPMR_WPEN            // WPEN : write protect enabled
                | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key
        }

        // If the Counter Full interrupt has been enabled by the user, call the registered handler
        if (_counterModeFullHandler[counter.tc][counter.n] != nullptr && _counterModeFullHandlerEnabled[counter.tc][counter.n]) {
            _counterModeFullHandler[counter.tc][counter.n](counter);
        }
    }


    // PWM mode

    // Initialize a TC channel and counter in PWM mode with the given period and hightime in microseconds
    bool enablePWM(Channel channel, float period, float highTime, bool output, SourceClock sourceClock, unsigned long sourceClockFrequency) {
        checkTC(channel);
        uint32_t REG = TC_BASE + channel.counter.tc * TC_SIZE + channel.counter.n * OFFSET_COUNTER_SIZE;

        // Initialize the counter and its clock
        initCounter(channel.counter, sourceClock, sourceClockFrequency);

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + channel.counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect disabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // CCR (Channel Control Register) : disable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKDIS;       // CLKDIS : disable the clock

        // CMR (Channel Mode Register) : setup the counter in Waveform Generation Mode
        uint32_t cmr = (*(volatile uint32_t*)(REG + OFFSET_CMR0)) & 0xFFFF0000; // Keep the current config of the A and B lines
        cmr = cmr
            | (static_cast<int>(sourceClock) & 0b111) << CMR_TCCLKS // TCCLKS : clock selection
            | 0 << CMR_CLKI     // CLKI : disable clock invert
            | 0 << CMR_BURST    // BURST : disable burst mode
            | 0 << CMR_CPCSTOP  // CPCSTOP : clock is not stopped with RC compare
            | 0 << CMR_CPCDIS   // CPCDIS : clock is not disabled with RC compare
            | 1 << CMR_EEVT     // EEVT : external event selection to XC0 (TIOB is therefore an output)
            | 2 << CMR_WAVSEL   // WAVSEL : UP mode with automatic trigger on RC Compare
            | 1 << CMR_WAVE;     // WAVE : waveform generation mode
        if (channel.line == TIOA) {
            cmr &= 0xFF00FFFF;       // Erase current config for channel A
            cmr |= 2 << CMR_ACPA     // ACPA : RA/TIOA : clear
                |  1 << CMR_ACPC     // ACPC : RC/TIOA : set
                |  2 << CMR_ASWTRG;  // ASWTRG : SoftwareTrigger/TIOA : clear
        } else { // TIOB
            cmr &= 0x00FFFFFF;       // Erase current config for channel B
            cmr |= 2 << CMR_BCPB     // BCPA : RA/TIOB : clear
                |  1 << CMR_BCPC     // BCPC : RC/TIOB : set
                |  2 << CMR_BSWTRG;  // BSWTRG : SoftwareTrigger/TIOB : clear
        }
        (*(volatile uint32_t*)(REG + OFFSET_CMR0)) = cmr;

        // Set the period and high time
        bool isValueValid = true;
        isValueValid = isValueValid && setPeriod(channel.counter, period);
        isValueValid = isValueValid && setHighTime(channel, highTime);

        // CCR (Channel Control Register) : enable and start the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKEN;        // CLKEN : enable the clock

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + channel.counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // Start the counter
        start(channel.counter);

        // If output is enabled, set the pin in peripheral mode
        if (output && !_pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]) {
            GPIO::enablePeripheral(PINS[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]);
            _pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line] = true;
        }

        return isValueValid;
    }

    // Set the period in microseconds for both TIOA and TIOB of the specified counter
    bool setPeriod(Counter counter, float period) {
        checkTC(counter);

        uint64_t clockFrequency = sourceClockFrequency(counter);
        if (clockFrequency == 0) {
            return false;
        }
        return setRC(counter, period * clockFrequency / 1000000L);
    }

    // Set the period in microseconds for both TIOA and TIOB of the specified channel
    bool setPeriod(Channel channel, float period) {
        return setPeriod(channel.counter, period);
    }

    // Set the high time of the specified channel in microseconds
    bool setHighTime(Channel channel, float highTime) {
        checkTC(channel);

        uint64_t clockFrequency = sourceClockFrequency(channel.counter);
        if (clockFrequency == 0) {
            return false;
        }
        return setRX(channel, highTime * clockFrequency / 1000000L);
    }

    // Set the duty cycle of the specified channel in percent
    bool setDutyCycle(Channel channel, int percent) {
        checkTC(channel);
        uint32_t REG = TC_BASE + channel.counter.tc * TC_SIZE + channel.counter.n * OFFSET_COUNTER_SIZE;
        uint32_t rc = (*(volatile uint32_t*)(REG + OFFSET_RC0));
        return setRX(channel, rc * percent / 100);
    }

    // Enable the output of the selected channel
    void enableOutput(Channel channel) {
        checkTC(channel);
        if (!_pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]) {
            GPIO::enablePeripheral(PINS[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]);
            _pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line] = true;
        }
    }

    // Disable the output of the selected channel
    void disableOutput(Channel channel) {
        checkTC(channel);
        if (_pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]) {
            GPIO::disablePeripheral(PINS[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line]);
            _pinsEnabled[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line] = false;
        }
    }


    // Measure mode

    void enableMeasurement(Counter counter, SourceClock sourceClock, unsigned long sourceClockFrequency) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Initialize the counter and its clock
        initCounter(counter, sourceClock, sourceClockFrequency);

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect disabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // CCR (Channel Control Register) : disable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKDIS;       // CLKDIS : disable the clock

        // Reset RA and RB
        (*(volatile uint32_t*)(REG + OFFSET_RA0)) = 0;
        (*(volatile uint32_t*)(REG + OFFSET_RB0)) = 0;

        // CMR (Channel Mode Register) : setup the counter in Capture Mode
        (*(volatile uint32_t*)(REG + OFFSET_CMR0))
            =                   // TCCLKS : clock selection
              (static_cast<int>(sourceClock) & 0b111) << CMR_TCCLKS
            | 0 << CMR_CLKI     // CLKI : disable clock invert
            | 0 << CMR_BURST    // BURST : disable burst mode
            | 1 << CMR_LDBSTOP  // LDBSTOP : stop clock after RB load
            | 1 << CMR_LDBDIS   // LDBSTOP : disable clock after RB load
            | 1 << CMR_ETRGEDG  // ETRGEDG : external trigger on rising edge
            | 1 << CMR_ABETRG   // ABETRG : external trigger on TIOA
            | 0 << CMR_CPCTRG   // CPCTRG : RC disabled
            | 0 << CMR_WAVE     // WAVE : capture mode
            | 2 << CMR_LDRA     // LDRA : load RA on falling edge of TIOA
            | 1 << CMR_LDRB;    // LDRB : load RB on rising edge of TIOA

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // Enable the input pin for TIOA
        if (!_pinsEnabled[counter.tc][N_CHANNELS_PER_COUNTER * counter.n]) {
            GPIO::enablePeripheral(PINS[counter.tc][N_CHANNELS_PER_COUNTER * counter.n]);
            _pinsEnabled[counter.tc][N_CHANNELS_PER_COUNTER * counter.n] = true;
        }
    }

    void measure(Counter counter, bool continuous) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // CCR (Channel Control Register) : disable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKDIS;       // CLKDIS : disable the clock

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect disabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // CMR (Channel Mode Register) : in one-shot mode, configure the TC to 
        // disable the clock after a measure
        if (!continuous) {
            (*(volatile uint32_t*)(REG + OFFSET_CMR0))
                |= 1 << CMR_LDBSTOP    // LDBSTOP : stop clock after RB load
                |  1 << CMR_LDBDIS;    // LDBSTOP : disable clock after RB load
        } else {
            (*(volatile uint32_t*)(REG + OFFSET_CMR0))
                &= ~(uint32_t)(
                      1 << CMR_LDBSTOP // LDBSTOP : do not stop clock after RB load
                    | 1 << CMR_LDBDIS  // LDBSTOP : do not disable clock after RB load
                );
        }

        // Enable the Counter Overflow interrupt
        _counterOverflowInternalHandler[counter.tc][counter.n] = measurementOverflowHandler;
        _rbLoadingInternalHandler[counter.tc][counter.n] = measurementRBLoadingHandler;
        enableInterrupt(counter);
        (*(volatile uint32_t*)(REG + OFFSET_IER0))
            = 1 << SR_COVFS;    // SR_COVFS : counter overflow status

        // Enable the RC Compare interrupt
        // RC is set to trigger an interrupt when the counter reaches about 90% of its
        // max value, which will enable the RB Loading interrupt. This is used to prevent
        // a race condition that can happen when the rising edge of the measured signal
        // happens very close to the Counter Overflow event, which could mask the rising
        // edge (RB Loading) event and produce erroneous values.
        // This is a good compromise instead of always enabling the RB Loading interrupt,
        // which would be uselessly CPU-intensive when measuring high-frequency signals.
        // For applications relying heavily on interrupts with priority higher than TC,
        // it might be a good idea to lower MEASUREMENT_RC_TRIGGER to make sure no rising
        // edge will be missed.
        // However, if low-frequency signals are expected, consider lowering the SourceClock
        // frequency in enableMeasurement() in order to avoid counter overflows altogether.
        (*(volatile uint32_t*)(REG + OFFSET_RC0)) = MEASUREMENT_RC_TRIGGER;
        _rcCompareInternalHandler[counter.tc][counter.n] = measurementRCCompareHandler;
        (*(volatile uint32_t*)(REG + OFFSET_IER0))
            = 1 << SR_CPCS;    // SR_CPCS : RC compare status

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // CCR (Channel Control Register) : enable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKEN;        // CLKEN : enable the clock
    }

    void measurementRCCompareHandler(Counter counter) {
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Enable the RB Loading interrupt to catch the next rising edge
        (*(volatile uint32_t*)(REG + OFFSET_IER0))
            = 1 << SR_LDRBS;    // SR_LDRBS : RB loading status
    }

    void measurementOverflowHandler(Counter counter) {
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Enable the RB Loading interrupt to catch the next rising edge
        (*(volatile uint32_t*)(REG + OFFSET_IER0))
            = 1 << SR_LDRBS;    // SR_LDRBS : RB loading status

        // Increment the MSB of the period
        _periodMSBInternal[counter.tc][counter.n]++;

        // If the signal is high, increment the MSB of the high-time
        if (_sr & (1 << SR_MTIOA)) {
            _highTimeMSBInternal[counter.tc][counter.n]++;
        }
    }

    void measurementRBLoadingHandler(Counter counter) {
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Cache the internal MSB buffers
        _periodMSB[counter.tc][counter.n] = _periodMSBInternal[counter.tc][counter.n];
        _highTimeMSB[counter.tc][counter.n] = _highTimeMSBInternal[counter.tc][counter.n];

        // Reset the internal MSB buffers
        _periodMSBInternal[counter.tc][counter.n] = 0;
        _highTimeMSBInternal[counter.tc][counter.n] = 0;

        // Disable the RB Loading interrupt
        (*(volatile uint32_t*)(REG + OFFSET_IDR0))
            = 1 << SR_LDRBS;    // SR_LDRBS : RB loading status
    }

    uint32_t measuredPeriodRaw(Counter counter) {
        return _periodMSB[counter.tc][counter.n] << 16 | rbValue(counter);
    }

    unsigned long measuredPeriod(Counter counter) {
        unsigned long clockFrequency = sourceClockFrequency(counter);
        if (clockFrequency == 0) {
            return false;
        }
        return (uint64_t)measuredPeriodRaw(counter) * 1000000L / clockFrequency;
    }

    uint32_t measuredHighTimeRaw(Counter counter) {
        return _highTimeMSB[counter.tc][counter.n] << 16 | raValue(counter);
    }

    unsigned long measuredHighTime(Counter counter) {
        unsigned long clockFrequency = sourceClockFrequency(counter);
        if (clockFrequency == 0) {
            return false;
        }
        return (uint64_t)measuredHighTimeRaw(counter) * 1000000L / clockFrequency;
    }

    unsigned int measuredDutyCycle(Counter counter) {
        return measuredHighTimeRaw(counter) * 100 / measuredPeriodRaw(counter);
    }

    bool isMeasureOverflow(Counter counter) {
        // TODO
        //uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;
        //return ((*(volatile uint32_t*)(REG + OFFSET_SR0)) >> SR_COVFS) & 1;
        return false;
    }


    // Interrupts

    // Enable the interrupts for the given counter at the core level
    void enableInterrupt(Counter counter) {
        checkTC(counter);

        Core::Interrupt interrupt = static_cast<Core::Interrupt>(static_cast<int>(Core::Interrupt::TC00) + counter.tc * N_COUNTERS_PER_TC + counter.n);
        Core::setInterruptHandler(interrupt, &interruptHandlerWrapper);
        Core::enableInterrupt(interrupt, INTERRUPT_PRIORITY);
    }

    // Enable the Counter Overflow interrupt on the given counter
    void enableCounterOverflowInterrupt(Counter counter, void (*handler)(Counter)) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Save the user handler and mark it as enabled
        if (handler != nullptr) {
            _counterOverflowHandler[counter.tc][counter.n] = handler;
        }
        _counterOverflowHandlerEnabled[counter.tc][counter.n] = true;

        // Enable the interrupts in this counter
        enableInterrupt(counter);

        // IER (Interrupt Enable Register) : enable the interrupt
        (*(volatile uint32_t*)(REG + OFFSET_IER0))
            = 1 << SR_COVFS;    // SR_COVFS : counter overflow status
    }

    // Disable the Counter Overflow interrupt on the given counter
    void disableCounterOverflowInterrupt(Counter counter) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Mark the handler as disabled
        _counterOverflowHandlerEnabled[counter.tc][counter.n] = false;

        // Make sure the interrupt is not used internally before disabling it
        if (_counterOverflowInternalHandler[counter.tc][counter.n] == nullptr) {
            // IDR (Interrupt Disable Register) : disable the interrupt
            (*(volatile uint32_t*)(REG + OFFSET_IDR0))
                = 1 << SR_COVFS;    // SR_COVFS : counter overflow status
        }
    }

    // Internal interrupt handler wrapper
    void interruptHandlerWrapper() {
        // Get the channel which generated the interrupt
        int interrupt = static_cast<int>(Core::currentInterrupt()) - static_cast<int>(Core::Interrupt::TC00);
        Counter counter = {
            .tc = static_cast<uint8_t>(interrupt / N_COUNTERS_PER_TC),
            .n = static_cast<uint8_t>(interrupt % N_COUNTERS_PER_TC)
        };
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Save the SR (Status Register) in order to read it only once, because each read clears
        // most of the interrupt bits
        _sr = (*(volatile uint32_t*)(REG + OFFSET_SR0));

        // Get the triggered interrupts from SR and IMR (Interrupt Mask Register)
        uint32_t interrupts = _sr & (*(volatile uint32_t*)(REG + OFFSET_IMR0));

        // RC Compare
        if (interrupts & (1 << SR_CPCS)) {
            // Call the internal handler if one has been registered
            if (_rcCompareInternalHandler[counter.tc][counter.n] != nullptr) {
                _rcCompareInternalHandler[counter.tc][counter.n](counter);
            }

            // Call the user handler if one has been registered and enabled
            if (_rcCompareHandler[counter.tc][counter.n] != nullptr && _rcCompareHandlerEnabled[counter.tc][counter.n]) {
                _rcCompareHandler[counter.tc][counter.n](counter);
            }
        }

        // Counter Overflow
        if (interrupts & (1 << SR_COVFS)) {
            // Call the internal handler if one has been registered
            if (_counterOverflowInternalHandler[counter.tc][counter.n] != nullptr) {
                _counterOverflowInternalHandler[counter.tc][counter.n](counter);
            }

            // Call the user handler if one has been registered and enabled
            if (_counterOverflowHandler[counter.tc][counter.n] != nullptr && _counterOverflowHandlerEnabled[counter.tc][counter.n]) {
                _counterOverflowHandler[counter.tc][counter.n](counter);
            }
        }

        // RB Loading
        if (interrupts & (1 << SR_LDRBS)) {
            // Call the internal handler if one has been registered
            if (_rbLoadingInternalHandler[counter.tc][counter.n] != nullptr) {
                _rbLoadingInternalHandler[counter.tc][counter.n](counter);
            }

            // Call the user handler if one has been registered and enabled
            if (_rbLoadingHandler[counter.tc][counter.n] != nullptr && _rbLoadingHandlerEnabled[counter.tc][counter.n]) {
                _rbLoadingHandler[counter.tc][counter.n](counter);
            }
        }
    }


    // Low-level counter functions

    // Set the RA or RB register of the given channel
    bool setRX(Channel channel, unsigned int rx) {
        checkTC(channel);
        uint32_t REG = TC_BASE + channel.counter.tc * TC_SIZE + channel.counter.n * OFFSET_COUNTER_SIZE;

        // Clip value
        bool isValueValid = true;
        if (rx > 0xFFFF) {
            rx = 0xFFFF;
            isValueValid = false;
        }

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + channel.counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // If the counter compare register (RA or RB) is zero, the output will be set by the RC compare
        // (CMR0.ACPC or CMR0.BCPC) but not immediately cleared by the RA/RB compare, and the output will
        // stay high instead of staying low. To match the expected behaviour the CMR register need to be
        // temporarily reconfigured to clear the output on RC compare.
        // When quitting this edge case (current RA or RB is 0), the default behaviour must be reset.
        // Depending on the case, the RA/RB value must be set either before of after configuring CMR.
        if (rx == 0) {
            // CMR (Channel Mode Register) : set RC compare over TIOA to 2
            uint32_t cmr = (*(volatile uint32_t*)(REG + OFFSET_CMR0));
            cmr &= ~(uint32_t)(0b11 << (channel.line == TIOB ? CMR_BCPC : CMR_ACPC));
            cmr |= 2 << (channel.line == TIOB ? CMR_BCPC : CMR_ACPC);
            (*(volatile uint32_t*)(REG + OFFSET_CMR0)) = cmr;

            // Set the signal high time *after* configuring CMR
            (*(volatile uint32_t*)(REG + (channel.line == TIOB ? OFFSET_RB0 : OFFSET_RA0))) = rx;
            
        } else if ((*(volatile uint32_t*)(REG + (channel.line == TIOB ? OFFSET_RB0 : OFFSET_RA0))) == 0) {
            // Set the signal high time *before* configuring CMR
            (*(volatile uint32_t*)(REG + (channel.line == TIOB ? OFFSET_RB0 : OFFSET_RA0))) = rx;

            // CMR (Channel Mode Register) : set RC compare over TIOA to 2
            uint32_t cmr = (*(volatile uint32_t*)(REG + OFFSET_CMR0));
            cmr &= ~(uint32_t)(0b11 << (channel.line == TIOB ? CMR_BCPC : CMR_ACPC));
            cmr |= 1 << (channel.line == TIOB ? CMR_BCPC : CMR_ACPC);
            (*(volatile uint32_t*)(REG + OFFSET_CMR0)) = cmr;

        } else {
            // Set the signal high time
            (*(volatile uint32_t*)(REG + (channel.line == TIOB ? OFFSET_RB0 : OFFSET_RA0))) = rx;
        }

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + channel.counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        return isValueValid;
    }

    // Set the RC register of the given counter
    bool setRC(Counter counter, unsigned int rc) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Clip value
        bool isValueValid = true;
        if (rc > 0xFFFF) {
            rc = 0xFFFF;
            isValueValid = false;
        }

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 0 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        // Set the signal period
        (*(volatile uint32_t*)(REG + OFFSET_RC0)) = rc;

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR))
            = 1 << WPMR_WPEN            // WPEN : write protect enabled
            | UNLOCK_KEY << WPMR_WPKEY; // WPKEY : write protect key

        return isValueValid;
    }

    // Get the value of the given counter
    uint32_t counterValue(Counter counter) {
        checkTC(counter);
        return ((uint32_t)_counterModeMSB[counter.tc][counter.n] << 16)
             | (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE + OFFSET_CV0));
    }

    // Get the value of the RA register for the given counter
    uint16_t raValue(Counter counter) {
        checkTC(counter);
        return (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE + OFFSET_RA0));
    }

    // Get the value of the RB register for the given counter
    uint16_t rbValue(Counter counter) {
        checkTC(counter);
        return (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE + OFFSET_RB0));
    }

    // Get the value of the RC register for the given counter
    uint16_t rcValue(Counter counter) {
        checkTC(counter);
        return (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE + OFFSET_RC0));
    }

    unsigned long sourceClockFrequency(Counter counter) {
        switch (_countersConfig[counter.tc][counter.n].sourceClock) {
            case SourceClock::GENERIC_CLOCK:
            case SourceClock::CLK0:
            case SourceClock::CLK1:
            case SourceClock::CLK2:
                return _countersConfig[counter.tc][counter.n].sourceClockFrequency;

            case SourceClock::PBA_OVER_2:
                return PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc) / 2;

            case SourceClock::PBA_OVER_8:
                return PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc) / 8;

            case SourceClock::PBA_OVER_32:
                return PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc) / 32;

            case SourceClock::PBA_OVER_128:
                return PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc) / 128;
        }
        return 0;
    }

    // Wait for the specified delay
    void wait(Counter counter, unsigned long delay, Unit unit, SourceClock sourceClock, unsigned long sourceClockFrequency) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Initialize the counter and its clock
        initCounter(counter, sourceClock, sourceClockFrequency);

        // Compute timing
        if (unit == Unit::MILLISECONDS) {
            delay *= 1000;
        }
        unsigned int basePeriod = 80000000L / PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc);
        delay = delay * 10 / basePeriod;
        unsigned int repeat = delay / 0x10000; // Max counter value
        unsigned int rest = delay % 0x10000;

        // WPMR (Write Protect Mode Register) : disable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR)) = 0 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;

        for (unsigned int i = 0; i <= repeat; i++) {
            // Set the period length
            (*(volatile uint32_t*)(REG + OFFSET_RC0)) = (i == repeat ? rest : 0xFFFF);

            // Software trigger
            (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_SWTRG;

            // Wait for RC value to be reached
            while (!((*(volatile uint32_t*)(REG + OFFSET_SR0)) & (1 << SR_CPCS)));
        }

        // WPMR (Write Protect Mode Register) : re-enable write protect
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR)) = 1 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
    }

    // Call the given handler after the specified delay
    void execDelayed(Counter counter, void (*handler)(), unsigned long delay, bool repeat, Unit unit, SourceClock sourceClock, unsigned long sourceClockFrequency) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Initialize the counter and its clock
        initCounter(counter, sourceClock, sourceClockFrequency);

        // Stop the timer
        (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_CLKDIS;

        // Set the handler
        ExecDelayedData& data = _execDelayedData[counter.tc][counter.n];
        data.handler = (uint32_t)handler;

        // Compute timings
        // If the requested delay is longer than a full period of the counter, compute and save the number
        // of periods to skip before calling the user handler
        if (unit == Unit::MILLISECONDS) {
            delay *= 1000;
        }
        unsigned int basePeriod = 80000000L / PM::getModuleClockFrequency(PM::CLK_TC0 + counter.tc);
        unsigned long value = delay * 10 / basePeriod;
        unsigned int skipPeriods = value / 0x10000; // 0x10000 is the max counter value
        unsigned int rest = value % 0x10000;
        data.skipPeriods = data.skipPeriodsReset = skipPeriods;
        data.rest = data.restReset = (skipPeriods > 0 ? rest : 0);
        data.repeat = repeat;
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR)) = 0 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
        (*(volatile uint32_t*)(REG + OFFSET_RC0)) = (skipPeriods > 0 ? 0xFFFF : rest);
        (*(volatile uint32_t*)(TC_BASE + counter.tc * TC_SIZE + OFFSET_WPMR)) = 1 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;

        // Enable the interrupt at the core level
        Core::Interrupt interrupt = static_cast<Core::Interrupt>(static_cast<int>(Core::Interrupt::TC00) + counter.tc * N_COUNTERS_PER_TC + counter.n);
        Core::setInterruptHandler(interrupt, &execDelayedHandlerWrapper);
        Core::enableInterrupt(interrupt, INTERRUPT_PRIORITY);

        // IER (Interrupt Enable Register) : enable the CPCS (RC value reached) interrupt
        (*(volatile uint32_t*)(REG + OFFSET_IER0)) = 1 << SR_CPCS;

        // Start the timer
        (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_CLKEN;
        (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_SWTRG;
    }

    void execDelayedHandlerWrapper() {
        // Get the channel which generated the interrupt
        int interrupt = static_cast<int>(Core::currentInterrupt()) - static_cast<int>(Core::Interrupt::TC00);
        int tc = interrupt / N_COUNTERS_PER_TC;
        int counter = interrupt % N_COUNTERS_PER_TC;
        uint32_t REG = TC_BASE + tc * TC_SIZE + counter * OFFSET_COUNTER_SIZE;
        ExecDelayedData& data = _execDelayedData[tc][counter];

        (*(volatile uint32_t*)(REG + OFFSET_SR0));

        // Decrease the counters
        if (data.skipPeriods > 0) {
            // If there are still periods to skip, decrease the periods counter
            data.skipPeriods--;

        } else if (data.rest > 0) {
            // Otherwise, if rest > 0, this is the last period : configure the counter with the remaining time
            (*(volatile uint32_t*)(TC_BASE + tc * TC_SIZE + OFFSET_WPMR)) = 0 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
            (*(volatile uint32_t*)(REG + OFFSET_RC0)) = data.rest;
            (*(volatile uint32_t*)(TC_BASE + tc * TC_SIZE + OFFSET_WPMR)) = 1 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
            (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_SWTRG;
            data.rest = 0; // There will be no time remaining after this

        } else {
            // Otherwise, if skipPeriods == 0 and rest == 0, the time has expired

            // Call the user handler
            void (*handler)() = (void (*)())data.handler;
            if (handler) {
                handler();
            }

            // Repeat
            if (data.repeat) {
                // Reset the data structure with their initial value
                data.skipPeriods = data.skipPeriodsReset;
                data.rest = data.restReset;
                (*(volatile uint32_t*)(TC_BASE + tc * TC_SIZE + OFFSET_WPMR)) = 0 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
                (*(volatile uint32_t*)(REG + OFFSET_RC0)) = (data.skipPeriods > 0 ? 0xFFFF : data.rest);
                (*(volatile uint32_t*)(TC_BASE + tc * TC_SIZE + OFFSET_WPMR)) = 1 << WPMR_WPEN | UNLOCK_KEY << WPMR_WPKEY;
                (*(volatile uint32_t*)(REG + OFFSET_CCR0)) = 1 << CCR_SWTRG;

            } else {
                // Disable the interrupt
                (*(volatile uint32_t*)(REG + OFFSET_IDR0)) = 1 << SR_CPCS;
            }
        }
    }

    // Start the counter and reset its value by issuing a software trigger
    void start(Counter counter) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // Reset the MSB of the counter
        _counterModeMSB[counter.tc][counter.n] = 0;

        // CCR (Channel Control Register) : issue a software trigger
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_SWTRG;        // SWTRG : software trigger
    }

    // Stop the clock of the given counter and freeze its value.
    // If the output is currently high, it will stay that way. Use disableOutput() if necessary.
    void stop(Counter counter) {
        checkTC(counter);
        uint32_t REG = TC_BASE + counter.tc * TC_SIZE + counter.n * OFFSET_COUNTER_SIZE;

        // CCR (Channel Control Register) : disable and reenable the clock to stop it
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKDIS;       // CLKDIS : disable the clock
        (*(volatile uint32_t*)(REG + OFFSET_CCR0))
            = 1 << CCR_CLKEN;       // CLKEN : enable the clock
    }

    // Start all the enabled counters simultaneously
    void sync() {
        // BCR (Block Control Register) : issue a sync command
        (*(volatile uint32_t*)(TC_BASE + OFFSET_BCR))
            = 1 << BCR_SYNC;        // SYNC : synch command
    }

    void setPin(Channel channel, PinFunction function, GPIO::Pin pin) {
        checkTC(channel);

        switch (function) {
            case PinFunction::OUT:
                PINS[channel.counter.tc][N_CHANNELS_PER_COUNTER * channel.counter.n + channel.line] = pin;
                break;

            case PinFunction::CLK:
                PINS_CLK[channel.counter.tc][channel.counter.n] = pin;
                break;
        }
    }


}

