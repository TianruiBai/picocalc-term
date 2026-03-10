#ifndef GPIO_RP2040_H
#define GPIO_RP2040_H

#include <cstdint>

namespace GPIO {
    enum {
        INPUT = 1,
        OUTPUT = 2,
        PULLUP = 4,
        INIT_HIGH = 8
    };
}

class gpio_rp2040_pin {
public:
    gpio_rp2040_pin(int pin = -1) : _pin(pin) {}
    void gpioMode(int mode) {}
    void gpioToggle() {}
    void gpioWrite(int val) {}
    int gpioRead() const { return 0; }
private:
    int _pin;
};

using gpio_rp2040 = gpio_rp2040_pin;

#endif // GPIO_RP2040_H
