#ifndef ILI9488_DRV_H
#define ILI9488_DRV_H

#include "spi_rp2040.h"
#include "gpio_rp2040.h"

class ili9488_drv {
public:
    enum { PICO320 };
    ili9488_drv(spi_rp2040 &spi, gpio_rp2040 &rst, gpio_rp2040 &dc, int mode) {}
    int getSizeX() const { return 320; }
    int getSizeY() const { return 240; }
    void clearScreen(int) {}
    void drawHLine(int, int, int, int) {}
    void drawPixel(int, int, int) {}
    void fillArea(int, int, int, int, int) {}
};

#endif // ILI9488_DRV_H
