#include "pm.h"
#include "scif.h"
#include "core.h"
#include "error.h"

namespace PM {

    // Clock frequencies
    unsigned long _mainClockFrequency = RCSYS_FREQUENCY;
    unsigned long _cpuClockFrequency = RCSYS_FREQUENCY;
    unsigned long _hsbClockFrequency = RCSYS_FREQUENCY;
    unsigned long _pbaClockFrequency = RCSYS_FREQUENCY;

    // Interrupt handlers
    uint32_t _interruptHandlers[N_INTERRUPTS];
    const int _interruptBits[N_INTERRUPTS] = {SR_CFD, SR_CKRDY, SR_WAKE};
    void interruptHandlerWrapper();
    

    // The main clock is used by the CPU and the peripheral buses and can be connected
    // to any of the clock sources listed in MainClockSource
    void setMainClockSource(MainClockSource clockSource, unsigned long cpudiv) {
        if (cpudiv >= 1) {
            // Unlock the CPUSEL register
            (*(volatile uint32_t*)(BASE + OFFSET_UNLOCK))
                    = UNLOCK_KEY           // KEY : Magic word (see datasheet)
                    | OFFSET_CPUSEL;        // ADDR : unlock CPUSEL

            // Configure the CPU clock divider
            if (cpudiv > 7) {
                cpudiv = 7;
            }
            (*(volatile uint32_t*)(BASE + OFFSET_CPUSEL))
                    = (cpudiv - 1) << CPUSEL_CPUSEL  // CPUSEL : select divider factor
                    | 1 << CPUSEL_CPUDIV;         // CPUDIV : enable divider

            // Wait for the divider to be ready
            while (!((*(volatile uint32_t*)(BASE + OFFSET_SR)) & (1 << SR_CKRDY)));
        }

        // Unlock the MCCTRL register, which is locked by default as a safety mesure
        (*(volatile uint32_t*)(BASE + OFFSET_UNLOCK))
                = UNLOCK_KEY               // KEY : Magic word (see datasheet)
                | OFFSET_MCCTRL;           // ADDR : unlock MCCTRL

        // Change the main clock source
        (*(volatile uint32_t*)(BASE + OFFSET_MCCTRL))
                = static_cast<int>(clockSource) << MCCTRL_MCSEL; // MCSEL : select clock source

        // Save the frequency
        switch (clockSource) {
            case MainClockSource::RCSYS:
                _mainClockFrequency = RCSYS_FREQUENCY;
                break;
            
            case MainClockSource::OSC0:
                _mainClockFrequency = SCIF::getOSC0Frequency();
                break;
            
            case MainClockSource::PLL:
                _mainClockFrequency = SCIF::getPLLFrequency();
                break;
            
            case MainClockSource::DFLL:
                _mainClockFrequency = SCIF::getDFLLFrequency();
                break;

            case MainClockSource::RCFAST:
                _mainClockFrequency = SCIF::getRCFASTFrequency();
                break;

            case MainClockSource::RC80M:
                _mainClockFrequency = RC80M_FREQUENCY;
                break;
        }
        _cpuClockFrequency = _mainClockFrequency / (1 << cpudiv);
        _hsbClockFrequency = _mainClockFrequency;
        _pbaClockFrequency = _mainClockFrequency;

        // Unlock the PBASEL register
        (*(volatile uint32_t*)(BASE + OFFSET_UNLOCK))
                = UNLOCK_KEY               // KEY : Magic word (see datasheet)
                | OFFSET_PBASEL;           // ADDR : unlock PBASEL

        // Ensure that the PBA clock is between 4 MHz and 8 MHz using the prescaler
        // If the PBA clock is too high, some modules won't be able to generate
        // clocks low enough (such as the 100 kHz SCL line for the I2C controller
        // or the timer/counter clocks)
        if (_mainClockFrequency > PBA_MAX_FREQUENCY) {

            // Find a divider
            uint8_t divider = 0;
            for (int i = 0; i <= 7; i++) {
                _pbaClockFrequency >>= 1; // Divide by 2
                divider++;

                if (_pbaClockFrequency <= PBA_MAX_FREQUENCY) {
                    break;
                }
            }

            // Set up the divider
            (*(volatile uint32_t*)(BASE + OFFSET_PBASEL))
                    = 1 << 7          // PBDIV : enable the prescaler
                    | (divider - 1);  // PBSEL : divide the clock by 2^(PBSEL + 1)
        } else {
            // Disable the divider
            (*(volatile uint32_t*)(BASE + OFFSET_PBASEL)) = 0;
        }
    }

    unsigned long getModuleClockFrequency(uint8_t peripheral) {
        if (peripheral >= HSBMASK && peripheral < PBAMASK) {
            return _hsbClockFrequency;
        } else if (peripheral >= PBAMASK && peripheral < PBBMASK) {
            return _pbaClockFrequency;
        }
        return 0;
    }

    void enablePeripheralClock(uint8_t peripheral, bool enabled) {
        // Select the correct register
        int offset = 0;
        if (peripheral >= HSBMASK && peripheral < PBAMASK) {
            offset = OFFSET_HSBMASK;
            peripheral -= HSBMASK;
        } else if (peripheral >= PBAMASK && peripheral < PBBMASK) {
            offset = OFFSET_PBAMASK;
            peripheral -= PBAMASK;
        } else if (peripheral >= PBBMASK) {
            offset = OFFSET_PBBMASK;
            peripheral -= PBBMASK;
        }

        // Unlock the selected register, which is locked by default as a safety mesure
        (*(volatile uint32_t*)(BASE + OFFSET_UNLOCK))
                = UNLOCK_KEY                // KEY : Magic word (see datasheet)
                | offset;                   // ADDR : unlock the selected register

        if (enabled) {
            // Unmask the corresponding clock
            (*(volatile uint32_t*)(BASE + offset)) |= 1 << peripheral;
        } else {
            // Unmask the corresponding clock
            (*(volatile uint32_t*)(BASE + offset)) &= ~(uint32_t)(1 << peripheral);
        }
    }

    void disablePeripheralClock(uint8_t peripheral) {
        enablePeripheralClock(peripheral, false);
    }

    void enablePBADivClock(uint8_t bit) {
        // Unlock the selected register, which is locked by default as a safety mesure
        (*(volatile uint32_t*)(BASE + OFFSET_UNLOCK))
                = UNLOCK_KEY                // KEY : Magic word (see datasheet)
                | OFFSET_PBADIVMASK;        // ADDR : unlock PBADIVMASK

        // Unmask the corresponding divided clock
        (*(volatile uint32_t*)(BASE + OFFSET_PBADIVMASK)) |= 1 << bit;
    }


    // Returns the cause of the last reset. This is useful for example to handle faults
    // detected by the watchdog or the brown-out detectors.
    ResetCause resetCause() {
        uint32_t rcause = (*(volatile uint32_t*)(BASE + OFFSET_RCAUSE));
        if (rcause != 0) {
            for (int i = 0; i < 32; i++) {
                if (rcause & 1 << i) {
                    return static_cast<ResetCause>(i);
                }
            }
        }
        return ResetCause::UNKNOWN;
    }

    // Returns the cause of the last wake up
    WakeUpCause wakeUpCause() {
        uint32_t wcause = (*(volatile uint32_t*)(BASE + OFFSET_WCAUSE));
        if (wcause != 0) {
            for (int i = 0; i < 32; i++) {
                if (wcause & 1 << i) {
                    return static_cast<WakeUpCause>(i);
                }
            }
        }
        return WakeUpCause::UNKNOWN;
    }

    void enableWakeUpSource(WakeUpSource src) {
        // AWEN (Asynchronous Wake Up Enable Register) : set the corresponding bit
        (*(volatile uint32_t*)(BASE + OFFSET_AWEN))
            |= 1 << static_cast<int>(src);
    }

    void disableWakeUpSource(WakeUpSource src) {
        // AWEN (Asynchronous Wake Up Enable Register) : clear the corresponding bit
        (*(volatile uint32_t*)(BASE + OFFSET_AWEN))
            &= ~(uint32_t)(1 << static_cast<int>(src));
    }

    void disableWakeUpSources() {
        // AWEN (Asynchronous Wake Up Enable Register) : clear the register
        (*(volatile uint32_t*)(BASE + OFFSET_AWEN)) = 0;
    }


    void enableInterrupt(void (*handler)(), Interrupt interrupt) {
        // Save the user handler
        _interruptHandlers[static_cast<int>(interrupt)] = (uint32_t)handler;

        // IER (Interrupt Enable Register) : enable the requested interrupt (WAKE by default)
        (*(volatile uint32_t*)(BASE + OFFSET_IER))
                = 1 << _interruptBits[static_cast<int>(interrupt)];

        // Set the handler and enable the module interrupt at the Core level
        Core::setInterruptHandler(Core::Interrupt::PM, interruptHandlerWrapper);
        Core::enableInterrupt(Core::Interrupt::PM, INTERRUPT_PRIORITY);
    }

    void disableInterrupt(Interrupt interrupt) {
        // IDR (Interrupt Disable Register) : disable the requested interrupt (WAKE by default)
        (*(volatile uint32_t*)(BASE + OFFSET_IDR))
                = 1 << _interruptBits[static_cast<int>(interrupt)];

        // If no interrupt is enabled anymore, disable the module interrupt at the Core level
        if ((*(volatile uint32_t*)(BASE + OFFSET_IMR)) == 0) {
            Core::disableInterrupt(Core::Interrupt::PM);
        }
    }

    void interruptHandlerWrapper() {
        // Call the user handler of every interrupt that is enabled and pending
        for (int i = 0; i < N_INTERRUPTS; i++) {
            if ((*(volatile uint32_t*)(BASE + OFFSET_IMR)) & (1 << _interruptBits[i]) // Interrupt is enabled
                    && (*(volatile uint32_t*)(BASE + OFFSET_ISR)) & (1 << _interruptBits[i])) { // Interrupt is pending
                void (*handler)() = (void (*)())_interruptHandlers[i];
                if (handler != nullptr) {
                    handler();
                }

                // Clear the interrupt by reading ISR
                (*(volatile uint32_t*)(BASE + OFFSET_ICR)) = 1 << _interruptBits[i];
            }
        }
    }

}