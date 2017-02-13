#ifndef _CARBIDE_H_
#define _CARBIDE_H_

#include <gpio.h>

#if PACKAGE != 64
#warning "You are compiling for the Carbide with an incorrect package, please set PACKAGE=64 in your Makefile"
#endif

namespace Carbide {

    // Pins definition
    const GPIO::Pin pinLedR = {GPIO::Port::A, 0};
    const GPIO::Pin pinLedG = {GPIO::Port::A, 1};
    const GPIO::Pin pinLedB = {GPIO::Port::A, 2};
    const GPIO::Pin pinButton = {GPIO::Port::A, 4};

    // Helper functions
    void init();
    void warningHandler();
    void criticalHandler();
    inline void initLedR() { GPIO::enableOutput(pinLedR, GPIO::HIGH); }
    inline void setLedR(bool on=true) { GPIO::set(pinLedR, !on); } // Inverted : pin must be LOW to turn the LED on
    inline void initLedG() { GPIO::enableOutput(pinLedG, GPIO::HIGH); }
    inline void setLedG(bool on=true) { GPIO::set(pinLedG, !on); }
    inline void initLedB() { GPIO::enableOutput(pinLedB, GPIO::HIGH); }
    inline void setLedB(bool on=true) { GPIO::set(pinLedB, !on); }
    inline void initLeds() { initLedR(); initLedG(); initLedB(); }
    inline void initButton() { GPIO::enableInput(pinButton, GPIO::Pulling::PULLUP); }
    inline bool isButtonPressed() { return !GPIO::get(pinButton); } // Inverted : the pin is LOW when the button is pressed (pullup)
    inline bool buttonRisingEdge() { return GPIO::fallingEdge(pinButton); } // Rising/falling are also inverted for the same reasons
    inline bool buttonFallingEdge() { return GPIO::risingEdge(pinButton); }

}

#endif