/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/include/board.h
 *
 * PicoCalc RP2350B-Plus-W Board Pin and Clock Definitions
 *
 * Hardware:
 *   - MCU: RP2350B (Waveshare RP2350B-Plus-W)
 *   - South Bridge: STM32F103R8T6 (I2C0 slave @ 0x1F)
 *     Manages: keyboard, AXP2101 battery PMIC, LCD backlight,
 *     keyboard backlight, PA enable, headphone detect, power control
 *   - Display: 320×320 IPS LCD, ILI9488-compat (SPI1)
 *   - PSRAM: 8 MB SPI PSRAM (PIO-driven, DMA)
 *   - SD Card: SPI0 (default) or PIO 1-bit SDIO (optional)
 *   - Audio: PWM (GP26 left, GP27 right)
 *   - Wi-Fi/BT: CYW43439 (on-module, managed by pico-sdk/cyw43)
 *
 ****************************************************************************/

#ifndef __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_INCLUDE_BOARD_H
#define __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "rp23xx_i2cdev.h"
#include "rp23xx_spidev.h"

#ifndef __ASSEMBLY__
#  include <time.h>
#  include <stdint.h>
#  include <stdbool.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* --- Clock Configuration --- */

#define MHZ                    1000000

#define BOARD_XOSC_FREQ        (12000000)   /* 12 MHz crystal */
#define BOARD_XOSC_STARTUPDELAY 1
#define BOARD_PLL_SYS_FREQ     (150000000)  /* 150 MHz system clock */
#define BOARD_PLL_USB_FREQ     (48000000)   /* 48 MHz USB clock */

#define BOARD_REF_FREQ         (12 * MHZ)
#define BOARD_SYS_FREQ         (150 * MHZ)
#define BOARD_PERI_FREQ        (150 * MHZ)
#define BOARD_USB_FREQ         (48 * MHZ)
#define BOARD_ADC_FREQ         (48 * MHZ)
#define BOARD_HSTX_FREQ        (150 * MHZ)

#define BOARD_UART_BASEFREQ    BOARD_PERI_FREQ
#define BOARD_TICK_CLOCK       (1 * MHZ)

/* =====================================================================
 * STM32 South Bridge (I2C0 slave @ 0x1F)
 *
 * The STM32F103R8T6 south bridge manages multiple peripherals that
 * the RP2350 accesses via I2C register reads/writes:
 *   - 67-key keyboard matrix with FIFO
 *   - AXP2101 battery PMIC (charge status, percent, shutdown)
 *   - LCD backlight PWM (PA8, 10 kHz)
 *   - Keyboard backlight PWM (PC8, 10 kHz)
 *   - PA (speaker amplifier) enable (PA14)
 *   - Headphone detect (PC12, auto-mutes PA)
 *   - Pico power enable (PA13)
 *
 * I2C Protocol:
 *   Write: send [reg_id | 0x80, value]
 *   Read:  send [reg_id], then read 2 bytes [reg_id, value]
 *   FIFO:  send [0x09], read 2 bytes [state, keycode]
 *          state: 1=pressed, 2=held, 3=released
 * ===================================================================== */

#define BOARD_SB_I2C_PORT       1            /* I2C1 — GPIO 6/7 are I2C1 in HW mux */
#define BOARD_SB_I2C_FREQ       (10000)      /* 10 kHz — slow for noise immunity with dual SPI */
#define BOARD_SB_I2C_ADDR       0x1F         /* 7-bit I2C slave address */

#define BOARD_SB_PIN_SDA        6            /* GP6  - I2C1 SDA */
#define BOARD_SB_PIN_SCL        7            /* GP7  - I2C1 SCL */
#define BOARD_SB_PIN_INT        8            /* GP8  - South bridge interrupt (active low) */

/* South Bridge Register Map */

#define SB_REG_VER              0x01  /* R:  [0x00, bios_ver] */
#define SB_REG_CFG              0x02  /* RW: config bits */
#define SB_REG_INT              0x03  /* R:  interrupt status */
#define SB_REG_KEY              0x04  /* R:  key status (FIFO count, caps/num) */
#define SB_REG_BKL              0x05  /* RW: LCD backlight 0-255 */
#define SB_REG_DEB              0x06  /* RW: debounce time (default 10) */
#define SB_REG_FRQ              0x07  /* RW: poll frequency (default 5) */
#define SB_REG_RST              0x08  /* RW: reset (write=delay+reset) */
#define SB_REG_FIF              0x09  /* R:  FIFO [state, keycode] */
#define SB_REG_BK2              0x0A  /* RW: keyboard backlight 0-255 */
#define SB_REG_BAT              0x0B  /* R:  [0x0B, percent] bit7=charging */
#define SB_REG_C64_MTX          0x0C  /* R:  C64 keyboard matrix (9 bytes) */
#define SB_REG_C64_JS           0x0D  /* R:  joystick bits */
#define SB_REG_OFF              0x0E  /* W:  power off (value=delay secs) */

#define SB_WRITE_FLAG           0x80  /* OR with reg_id for write */
#define SB_BIOS_VERSION         0x16  /* Expected BIOS version */

/* South Bridge Interrupt Status Bits (REG_INT) */

#define SB_INT_OVERFLOW         0x01
#define SB_INT_CAPSLOCK         0x02
#define SB_INT_NUMLOCK          0x04
#define SB_INT_KEY              0x08  /* Key event in FIFO */
#define SB_INT_PANIC            0x10

/* South Bridge Key State (from FIFO read) */

#define SB_KEY_STATE_PRESSED    1
#define SB_KEY_STATE_HELD       2
#define SB_KEY_STATE_RELEASED   3

/* South Bridge Battery Status Bits */

#define SB_BAT_CHARGING         0x80  /* Bit 7 of percent byte */
#define SB_BAT_PERCENT_MASK     0x7F  /* Bits 0-6 = 0-100% */

/* South Bridge Config Bits (REG_CFG) */

#define SB_CFG_OVERFLOW_ON      0x01  /* FIFO overwrite on overflow */
#define SB_CFG_OVERFLOW_ON      0x01  /* FIFO overwrite on overflow */
#define SB_CFG_OVERFLOW_INT     0x02  /* FIFO overflow interrupt enable */
#define SB_CFG_CAPSLOCK_INT     0x04  /* Capslock change interrupt enable */
#define SB_CFG_NUMLOCK_INT      0x08  /* Numlock change interrupt enable */
#define SB_CFG_KEY_INT          0x10  /* Key event interrupt enable */
#define SB_CFG_PANIC_INT        0x20  /* Panic interrupt enable */
#define SB_CFG_REPORT_MODS      0x40  /* Report modifiers as keycodes */
#define SB_CFG_USE_MODS         0x80  /* Apply modifier transform to key output */
#define SB_CFG_USE_MODS         0x80  /* Apply modifier transform to key output */

/* Special Key Codes from South Bridge FIFO */

#define SB_KEY_POWER            0x91  /* Power button short press */

/* --- Keyboard (aliases to south bridge) --- */

#define BOARD_KBD_I2C_PORT      BOARD_SB_I2C_PORT
#define BOARD_KBD_I2C_FREQ      BOARD_SB_I2C_FREQ
#define BOARD_KBD_I2C_ADDR      BOARD_SB_I2C_ADDR
#define BOARD_KBD_PIN_SDA       BOARD_SB_PIN_SDA
#define BOARD_KBD_PIN_SCL       BOARD_SB_PIN_SCL
#define BOARD_KBD_PIN_INT       BOARD_SB_PIN_INT

/* --- SPI1: Display (ILI9488-compatible, ST7365P) --- */

#define BOARD_LCD_SPI_PORT      1
#define BOARD_LCD_SPI_FREQ      (25000000)   /* 25 MHz SPI clock */

#define BOARD_LCD_PIN_SCK       10           /* GP10 - SPI1 SCK */
#define BOARD_LCD_PIN_MOSI      11           /* GP11 - SPI1 TX (MOSI) */
#define BOARD_LCD_PIN_MISO      12           /* GP12 - SPI1 RX (unused) */
#define BOARD_LCD_PIN_CS        13           /* GP13 - SPI1 CSn */
#define BOARD_LCD_PIN_DC        14           /* GP14 - Data/Command select */
#define BOARD_LCD_PIN_RST       15           /* GP15 - Hardware reset */

/* LCD backlight is controlled by STM32 south bridge register SB_REG_BKL.
 * There is NO direct GPIO backlight pin on the RP2350.
 * The south bridge drives PA8 PWM at 10 kHz for LCD backlight.
 * Use rp23xx_sb_set_lcd_backlight(0-255) to control brightness.
 */

#define BOARD_LCD_WIDTH         320
#define BOARD_LCD_HEIGHT        320
#define BOARD_LCD_BPP           16           /* RGB565 internal */
#define BOARD_LCD_SPI_BPP       18           /* RGB666 over SPI (3 bytes/px) */
#define BOARD_LCD_INVERSION     1            /* Display inversion ON */

/* MADCTL: MX | BGR = 0x48 (portrait, BGR pixel order) */

#define BOARD_LCD_MADCTL        0x48

/* --- SPI0: SD Card (legacy SPI mode) --- */

#define BOARD_SD_SPI_PORT       0
#define BOARD_SD_SPI_FREQ       (25000000)   /* 25 MHz SPI high-speed */

#define BOARD_SD_PIN_SCK        18           /* GP18 - SPI0 SCK */
#define BOARD_SD_PIN_MOSI       19           /* GP19 - SPI0 TX (MOSI) */
#define BOARD_SD_PIN_MISO       16           /* GP16 - SPI0 RX (MISO) */
#define BOARD_SD_PIN_CS         17           /* GP17 - SPI0 CSn */

/* --- PIO SDIO 1-bit (optional, faster SD card access) ---
 *
 * When CONFIG_PICOCALC_PIO_SDIO is defined, the SD card is accessed
 * via PIO-simulated 1-bit SDIO instead of SPI0. This provides
 * significantly faster transfer rates.
 *
 * Uses the same physical pins as SPI0 but driven by PIO:
 *   CLK = GP18, CMD = GP19, DAT0 = GP16
 */

#ifdef CONFIG_PICOCALC_PIO_SDIO
#define BOARD_SDIO_PIO_INST     1            /* PIO1 (PIO0 used for PSRAM) */
#define BOARD_SDIO_PIN_CLK      18           /* GP18 - SDIO CLK */
#define BOARD_SDIO_PIN_CMD      19           /* GP19 - SDIO CMD */
#define BOARD_SDIO_PIN_DAT0     16           /* GP16 - SDIO DAT0 */
#define BOARD_SDIO_FREQ         (25000000)   /* 25 MHz SDIO clock */
#endif

/* --- Audio: PWM --- */

#define BOARD_AUDIO_PIN_LEFT    40           /* GP26 - PWM left channel */
#define BOARD_AUDIO_PIN_RIGHT   41           /* GP27 - PWM right channel */
#define BOARD_AUDIO_PWM_FREQ    (44100)      /* Base sample rate */
#define BOARD_AUDIO_PWM_BITS    10           /* PWM resolution bits */

/* Speaker PA (power amplifier) is controlled by STM32 south bridge PA14.
 * The south bridge auto-disables PA when headphone is detected (PC12).
 * SW can force PA enable/disable via south bridge I2C commands.
 * Headphone detect status is read from the keyboard FIFO events.
 */

/* --- Battery (AXP2101 PMIC via South Bridge) ---
 *
 * Battery management is entirely handled by the STM32 south bridge
 * which communicates with the AXP2101 PMIC on its own I2C bus
 * (PB10/PB11). The RP2350 reads battery status from the south
 * bridge via SB_REG_BAT (register 0x0B):
 *   Byte 1: 0x0B (register echo)
 *   Byte 2: percentage (0-100), bit 7 = charging flag
 *
 * Power off: write delay_seconds to SB_REG_OFF (register 0x0E)
 * The south bridge calls PMU.shutdown() after the delay.
 *
 * There is NO direct battery ADC connection to the RP2350.
 */

/* --- PSRAM (8 MB, PIO-driven SPI with DMA) ---
 *
 * The PicoCalc mainboard has 8 MB SPI PSRAM (e.g., APS6404L or
 * compatible). Access is via PIO-simulated SPI using DMA for
 * maximum throughput.
 *
 * PIO sideset constraint: CS and SCK must be on consecutive GPIOs
 * with CS at the lower pin. The PIO program uses 2-bit sideset
 * where bit 0 = CS, bit 1 = SCK.
 *
 *   Pin assignment (from PicoCalc reference):
 *   CS  = GP20 (sideset base, active low)
 *   SCK = GP21 (sideset base + 1)
 *   MOSI = GP2 (SPI mode: IO0 output)
 *   MISO = GP3 (SPI mode: IO0 input)
 *
 * For QSPI mode (fast bulk transfers):
 *   SIO0-SIO3 = GP2-GP5 (consecutive, bidirectional)
 *
 * Commands:
 *   0x02 = Write (addr[23:0] + data)
 *   0x0B = Fast Read (addr[23:0] + dummy_byte + data)
 *   0x66 = Reset Enable
 *   0x99 = Reset
 *   0x35 = Enter QPI mode
 */

#define BOARD_PSRAM_SIZE        (8 * 1024 * 1024)  /* 8 MB */
#define BOARD_PSRAM_PIO_INST    1            /* PIO1 for PSRAM (matches reference) */
#define BOARD_PSRAM_PIO_SM      0            /* State machine 0 */

#define BOARD_PSRAM_PIN_CS      20           /* GP20 - PSRAM CS (PIO sideset base) */
#define BOARD_PSRAM_PIN_SCK     21           /* GP21 - PSRAM SCK (CS + 1) */
#define BOARD_PSRAM_PIN_MOSI    2            /* GP2  - PSRAM MOSI (SPI mode) */
#define BOARD_PSRAM_PIN_MISO    3            /* GP3  - PSRAM MISO (SPI mode) */
#define BOARD_PSRAM_PIN_SIO0    2            /* GP2  - PSRAM SIO0 (QSPI base) */
#define BOARD_PSRAM_PIN_SIO1    3            /* GP3  - PSRAM SIO1 */
#define BOARD_PSRAM_PIN_SIO2    4            /* GP4  - PSRAM SIO2 */
#define BOARD_PSRAM_PIN_SIO3    5            /* GP5  - PSRAM SIO3 */

/* PSRAM clock frequency: sys_clk / clkdiv
 * At 150 MHz sys_clk with clkdiv=2.0: SPI_CLK = 75 MHz
 * Use fudge factor (extra read cycle) above 83 MHz
 */

#define BOARD_PSRAM_CLKDIV      1.0f         /* 75 MHz effective SPI clock */
#define BOARD_PSRAM_USE_FUDGE   true         /* Extra read cycle for timing margin */

/* PSRAM memory-mapped base (after PIO init + heap registration) */

#define BOARD_PSRAM_HEAP_NAME   "psram"

/* --- UART0: Debug Console --- */

#define BOARD_UART0_PIN_TX      0            /* GP0 - UART0 TX */
#define BOARD_UART0_PIN_RX      1            /* GP1 - UART0 RX */
#define BOARD_UART0_BAUD        115200

/* --- LED (on Waveshare module) --- */

/* The CYW43439 module controls the on-board LED via SPI
 * (same as Pico W). No direct GPIO LED available. */

/* --- Wi-Fi / Bluetooth (CYW43439) --- */

/* Managed internally by the CYW43 driver via PIO + SPI.
 * No GPIO configuration needed here — the driver accesses
 * the module through the pico-sdk CYW43 interface.
 *
 * Key CYW43 pins (directly wired on the Waveshare module):
 *   WL_ON    = GP23
 *   WL_D     = GP24
 *   WL_CS    = GP25
 *   WL_CLK   = GP29
 *
 * Note: GP25 is exclusively used by CYW43439 for WL_CS.
 * The LCD backlight is NOT on GP25 — it is controlled by the
 * STM32 south bridge.
 */

/* =====================================================================
 * GPIO Pin Summary (RP2350B-Plus-W on PicoCalc)
 *
 *   GP0  - UART0 TX (debug console)
 *   GP1  - UART0 RX (debug console)
 *   GP2  - PSRAM SIO0 / MOSI (PIO0)
 *   GP3  - PSRAM SIO1 / MISO (PIO0)
 *   GP4  - PSRAM SIO2 (QSPI mode)
 *   GP5  - PSRAM SIO3 (QSPI mode)
 *   GP6  - I2C1 SDA (south bridge)
 *   GP7  - I2C1 SCL (south bridge)
 *   GP8  - (available)
 *   GP9  - (available)
 *   GP10 - SPI1 SCK (LCD)
 *   GP11 - SPI1 MOSI (LCD)
 *   GP12 - SPI1 MISO (LCD, unused)
 *   GP13 - SPI1 CS (LCD)
 *   GP14 - LCD DC
 *   GP15 - LCD RST
 *   GP16 - SPI0 MISO / SDIO DAT0 (SD card)
 *   GP17 - SPI0 CS (SD card)
 *   GP18 - SPI0 SCK / SDIO CLK (SD card)
 *   GP19 - SPI0 MOSI / SDIO CMD (SD card)
 *   GP20 - PSRAM CS (PIO0 sideset base)
 *   GP21 - PSRAM SCK (PIO0 sideset base + 1)
 *   GP22 - (available)
 *   GP23 - CYW43 WL_ON
 *   GP24 - CYW43 WL_D
 *   GP25 - CYW43 WL_CS
 *   GP26 - Audio PWM left
 *   GP27 - Audio PWM right
 *   GP28 - (available / ADC2)
 *   GP29 - CYW43 WL_CLK
 * ===================================================================== */

/****************************************************************************
 * Assembly Macros
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/* South Bridge access functions (rp23xx_southbridge.c) */

int     rp23xx_sb_init(void);
uint8_t rp23xx_sb_read_reg(uint8_t reg);
int     rp23xx_sb_write_reg(uint8_t reg, uint8_t value);
int     rp23xx_sb_read_fifo(uint8_t *state, uint8_t *keycode);
int     rp23xx_sb_get_battery(uint8_t *percent, bool *charging);
int     rp23xx_sb_set_lcd_backlight(uint8_t brightness);
int     rp23xx_sb_set_kbd_backlight(uint8_t brightness);
int     rp23xx_sb_power_off(uint8_t delay_secs);
uint8_t rp23xx_sb_get_version(void);

/* Always-on timer / wall-time helpers (rp23xx_aonrtc.c) */

int rp23xx_aon_timer_gettime(struct timespec *ts);
int rp23xx_aon_walltime_gettime(struct timespec *ts);
int rp23xx_aon_walltime_settime(const struct timespec *ts);

/* Core clock management (rp23xx_clockmgr.c) */

void rp23xx_set_power_profile(int profile);
int  rp23xx_get_power_profile(void);

/* Backlight / sleep management (rp23xx_sleep.c) */

void rp23xx_backlight_activity(void);
void rp23xx_backlight_timer_tick(void);
void rp23xx_sleep_enter(void);
void rp23xx_sleep_exit(void);

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_RP23XX_PICOCALC_RP2350B_INCLUDE_BOARD_H */
