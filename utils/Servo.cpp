#include "Servo.h"

// Constructor : must specify the underlying TC channel to use
Servo::Servo(TC::Channel tcChannel, GPIO::Pin pin) {
    // Save the parameters
    _tcChannel = tcChannel;
    _pin = pin;

    // If a custom pin is specified, set it
    if (pin.number != 0xFF) {
        TC::setPin(tcChannel, TC::PinFunction::OUT, pin);
    }

    // Initialize the TC channel to output the PWM signal
    TC::init(tcChannel, DEFAULT_PERIOD, 0, true);

    // Set default timings
    setPWMTimings();
}

// Set the servo position in percent
// This will translate to different angles according to the angular range of the servo
void Servo::set(unsigned int percent) {
    // Check value
    if (percent > 100) {
        percent = 100;
    }

    // Save the value
    _percent = percent;

    // Compute and set the hightime
    TC::setHighTime(_tcChannel, _highTime0 + percent * (_highTime100 - _highTime0) / 100);
}

// Customize the PWM timings
// Passing no argument resets the default values
void Servo::setPWMTimings(unsigned int highTime0, unsigned int highTime100, unsigned int period) {
    // Save the timings
    _highTime0 = highTime0;
    _highTime100 = highTime100;
    _period = period;

    // Set the period
    TC::setPeriod(_tcChannel, period);

    // Recalculate the hightime based on the new timings
    set(_percent);
}