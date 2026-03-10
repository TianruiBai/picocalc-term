#ifndef I2C_RP2040_H
#define I2C_RP2040_H

class i2c_rp2040 {
public:
    i2c_rp2040(int mod, int sda, int scl, int flags) {}
    void setSpeed(int speed) {}
    int i2cWrite(int addr, const unsigned char *buf, int len) { return 0; }
    int i2cRead(int addr, unsigned char *buf, int len) { return 0; }
};

#endif // I2C_RP2040_H
