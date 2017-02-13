#include "scif.h"
#include "gpio.h"
#include "usb.h"
#include "usart.h"
#include "adc.h"
#include "dac.h"
#include "tc.h"
#include "i2c.h"
#include "spi.h"
#include "gloc.h"

// This file defines the pin mapping for peripherals.
// Most of the chip peripherals can map their signals on a few different
// pins. The list of available functions for each pin is defined in the
// datasheet, §3.2.1 Multiplexed Signals. This file can be freely modified to
// match your needs, however, make sure the same pin is not used more than
// once (unless you know what you are doing).

namespace SCIF {

    const GPIO::Pin PINS_GCLK[] = {
        {GPIO::Port::A, 19, GPIO::Periph::E}, // GCLK0
        {GPIO::Port::A, 20, GPIO::Periph::E}, // GCLK1
    };

    const GPIO::Pin PINS_GCLK_IN[] = {
        {GPIO::Port::A, 23, GPIO::Periph::E}, // GCLK_IN0
        {GPIO::Port::A, 24, GPIO::Periph::E}  // GCLK_IN1
    };

    // Alternatives for GCLK0
    //{GPIO::Port::A,  2, GPIO::Periph::A}
}

namespace TC {

    const uint8_t N_TC = 1;

    const GPIO::Pin PINS[MAX_N_TC][N_CHANNELS * N_LINES] = {
        {
            {GPIO::Port::A,  8, GPIO::Periph::B}, // TC0 A0
            {GPIO::Port::A,  9, GPIO::Periph::B}, // TC0 B0
            {GPIO::Port::A, 10, GPIO::Periph::B}, // TC0 A1
            {GPIO::Port::A, 11, GPIO::Periph::B}, // TC0 B1
            {GPIO::Port::A, 12, GPIO::Periph::B}, // TC0 A2
            {GPIO::Port::A, 13, GPIO::Periph::B}  // TC0 B2
        }
    };

    const GPIO::Pin PINS_CLK[MAX_N_TC][N_CHANNELS * N_LINES] = {
        {
            {GPIO::Port::A, 14, GPIO::Periph::B}, // TC0 CLK0
            {GPIO::Port::A, 15, GPIO::Periph::B}, // TC0 CLK1
            {GPIO::Port::A, 16, GPIO::Periph::B}, // TC0 CLK2
        }
    };

}

namespace USB {

    const GPIO::Pin PIN_DM = {GPIO::Port::A, 25, GPIO::Periph::A};
    const GPIO::Pin PIN_DP = {GPIO::Port::A, 26, GPIO::Periph::A};

}

namespace USART {

    // RX
    const GPIO::Pin PINS_RX[] = {
        {GPIO::Port::A, 11, GPIO::Periph::A}, // USART0 RX
        {GPIO::Port::A, 15, GPIO::Periph::A}, // USART1 RX
        {GPIO::Port::A, 19, GPIO::Periph::A}, // USART2 RX
        {GPIO::Port::A, 30, GPIO::Periph::E}  // USART3 RX
    };

    // TX
    const GPIO::Pin PINS_TX[] = {
        {GPIO::Port::A, 12, GPIO::Periph::A}, // USART0 TX
        {GPIO::Port::A, 16, GPIO::Periph::A}, // USART1 TX
        {GPIO::Port::A, 20, GPIO::Periph::A}, // USART2 TX
        {GPIO::Port::A, 31, GPIO::Periph::E}  // USART3 TX
    };


    // Alternatives for USART0
    // Be careful when using these pins that they are not already used for something else

    //{GPIO::Port::A,  5, GPIO::Periph::B} // RX
    //{GPIO::Port::A,  7, GPIO::Periph::B} // TX


    // Alternatives for USART2

    //{GPIO::Port::A, 25, GPIO::Periph::B} // RX
    //{GPIO::Port::A, 26, GPIO::Periph::B} // TX

}

namespace I2C {

    // SDA
    const GPIO::Pin PINS_SDA[] = {
        {GPIO::Port::A, 23, GPIO::Periph::B}, // I2C0 SDA
        {},                                   // I2C1 doesn't exist
        {GPIO::Port::A, 21, GPIO::Periph::E}, // I2C2 SDA
    };

    // SCL
    const GPIO::Pin PINS_SCL[] = {
        {GPIO::Port::A, 24, GPIO::Periph::B}, // I2C0 SCL
        {},                                   // I2C1 doesn't exist
        {GPIO::Port::A, 22, GPIO::Periph::E}, // I2C2 SCL
    };

}

namespace SPI {

    const GPIO::Pin PIN_MISO =  {GPIO::Port::A, 27, GPIO::Periph::A};
    const GPIO::Pin PIN_MOSI =  {GPIO::Port::A, 28, GPIO::Periph::A};
    const GPIO::Pin PIN_SCK =   {GPIO::Port::A, 29, GPIO::Periph::A};
    const GPIO::Pin PIN_NPCS0 = {GPIO::Port::A, 30, GPIO::Periph::A};
    const GPIO::Pin PIN_NPCS1 = {GPIO::Port::A, 31, GPIO::Periph::A};
    const GPIO::Pin PIN_NPCS2 = {GPIO::Port::A, 14, GPIO::Periph::C};
    const GPIO::Pin PIN_NPCS3 = {GPIO::Port::A, 15, GPIO::Periph::C};

    // Alternatives for MISO
    //const GPIO::Pin PIN_MISO =  {GPIO::Port::A,  3, GPIO::Periph::B};
    //const GPIO::Pin PIN_MISO =  {GPIO::Port::A, 21, GPIO::Periph::A};

    // Alternatives for MOSI
    //const GPIO::Pin PIN_MOSI =  {GPIO::Port::A, 22, GPIO::Periph::A};

    // Alternatives for SCK
    //const GPIO::Pin PIN_SCK =   {GPIO::Port::A, 23, GPIO::Periph::A};

    // Alternatives for NPCS0
    //const GPIO::Pin PIN_NPCS0 = {GPIO::Port::A,  2, GPIO::Periph::B};
    //const GPIO::Pin PIN_NPCS0 = {GPIO::Port::A, 24, GPIO::Periph::A};

    // Alternatives for NPCS1
    //const GPIO::Pin PIN_NPCS1 = {GPIO::Port::A, 13, GPIO::Periph::C};

    // No alternatives for NPCS2 ou NPCS3

}

namespace ADC {

    const GPIO::Pin PINS[] = {
        {GPIO::Port::A,  4, GPIO::Periph::A}, // ADC0
        {GPIO::Port::A,  5, GPIO::Periph::A}, // ADC1
        {GPIO::Port::A,  7, GPIO::Periph::A}, // ADC2
    };

}

namespace DAC {

    const GPIO::Pin PIN_VOUT = {GPIO::Port::A,  6, GPIO::Periph::A};

}