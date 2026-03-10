#ifndef SD_SPI_DRV_H
#define SD_SPI_DRV_H

class sd_spi_drv {
public:
    sd_spi_drv() {}
    // allow construction from spi object as real driver might
    sd_spi_drv(spi_rp2040 &spi) {}
};

#endif // SD_SPI_DRV_H
