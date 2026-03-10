#ifndef SPI_RP2040_H
#define SPI_RP2040_H

#include "gpio_rp2040.h"

class spi_rp2040 {
public:
    // only one constructor; gpio_rp2040 is an alias for gpio_rp2040_pin
    spi_rp2040(int port, int miso, int mosi, int sclk, gpio_rp2040_pin &cs) {}
};

#endif // SPI_RP2040_H
