/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram.c
 *
 * PIO-driven SPI PSRAM driver for PicoCalc mainboard (8 MB).
 *
 * Based on rp2040-psram by Ian Scott (MIT license).
 * Adapted for NuttX RTOS on RP2350B.
 *
 * The PSRAM chip (APS6404L / LY68L6400 / IPS6404 compatible) is accessed
 * via PIO-simulated SPI (1-bit) for stable compatibility with PicoCalc
 * reference hardware. Initialization sends reset commands, then all data
 * access uses a PIO SPI program on MOSI/MISO with CS+SCK sideset pins.
 *
 * The PIO program uses 2-bit sideset for CS (bit0) and SCK (bit1).
 * After initialization, a block allocator provides psram_malloc/free.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/mm/mm.h>
#include <nuttx/mutex.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "rp23xx_gpio.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RP2350 PIO and DMA register base addresses */

#define PIO0_BASE              0x50200000
#define PIO1_BASE              0x50300000
#define DMA_BASE               0x50000000

/* PIO registers (relative to PIO base) */

#define PIO_CTRL               0x000
#define PIO_FSTAT              0x004
#define PIO_FDEBUG             0x008
#define PIO_FLEVEL             0x00C
#define PIO_TXF(sm)            (0x010 + (sm) * 4)
#define PIO_RXF(sm)            (0x020 + (sm) * 4)
#define PIO_IRQ                0x030
#define PIO_IRQ_FORCE          0x034
#define PIO_INPUT_SYNC_BYPASS  0x038
#define PIO_INSTR_MEM(n)       (0x048 + (n) * 4)
#define PIO_SM_CLKDIV(sm)      (0x0C8 + (sm) * 0x18)
#define PIO_SM_EXECCTRL(sm)    (0x0CC + (sm) * 0x18)
#define PIO_SM_SHIFTCTRL(sm)   (0x0D0 + (sm) * 0x18)
#define PIO_SM_ADDR(sm)        (0x0D4 + (sm) * 0x18)
#define PIO_SM_INSTR(sm)       (0x0D8 + (sm) * 0x18)
#define PIO_SM_PINCTRL(sm)     (0x0DC + (sm) * 0x18)

/* DMA channel registers */

#define DMA_CH_READ_ADDR(ch)   (0x000 + (ch) * 0x40)
#define DMA_CH_WRITE_ADDR(ch)  (0x004 + (ch) * 0x40)
#define DMA_CH_TRANS_COUNT(ch) (0x008 + (ch) * 0x40)
#define DMA_CH_CTRL_TRIG(ch)   (0x00C + (ch) * 0x40)
#define DMA_CH_AL1_CTRL(ch)    (0x010 + (ch) * 0x40)

/* DMA DREQ values for PIO */

#define DREQ_PIO0_TX(sm)       (sm)
#define DREQ_PIO0_RX(sm)       (4 + (sm))
#define DREQ_PIO1_TX(sm)       (8 + (sm))
#define DREQ_PIO1_RX(sm)       (12 + (sm))

/* PSRAM commands */

#define PSRAM_CMD_WRITE        0x02  /* SPI Write */
#define PSRAM_CMD_FAST_READ    0x0B  /* SPI Fast Read (+ dummy byte) */
#define PSRAM_CMD_RESET_EN     0x66  /* Reset Enable */
#define PSRAM_CMD_RESET        0x99  /* Reset */

/* PIO SPI byte-stream format (matching reference rp2040-psram):
 * Byte 0: number of bits to write (actual count, PIO pre-decrements)
 * Byte 1: number of bits to read
 * Byte 2..N: data to write (MSB first)
 *
 * Uses autopull=8 with byte-sized FIFO writes.  On RP2040/RP2350
 * the bus fabric replicates narrow writes across all byte lanes,
 * so a byte write of 0xAB → FIFO entry 0xABABABAB.  With shift-left
 * the PIO's `out x, 8` extracts bits [31:24] = 0xAB correctly.
 *
 * For PSRAM write:  cmd(8) + addr(24) + data(N)
 * For PSRAM read:   cmd(8) + addr(24) + dummy(8), then read N bits
 */

/* Register access helpers */

#define PIO_BASE     (BOARD_PSRAM_PIO_INST == 0 ? PIO0_BASE : PIO1_BASE)

#define pio_reg(off)   (*(volatile uint32_t *)(PIO_BASE + (off)))
#define dma_reg(off)   (*(volatile uint32_t *)(DMA_BASE + (off)))

/* DMA channels (using 3 channels for PSRAM) */

#define DMA_CH_WRITE    8    /* PSRAM write DMA channel */
#define DMA_CH_READ     9    /* PSRAM read DMA channel */
#define DMA_CH_ASYNC    10   /* PSRAM async write DMA channel */

/****************************************************************************
 * PIO Program: SPI PSRAM
 *
 * Uses 2-bit sideset: bit0=CS, bit1=SCK.
 * Program variants mirror rp2040-psram reference:
 *   - standard read timing
 *   - fudge timing (extra cycle + falling-edge sampling)
 *
 ****************************************************************************/

static const uint16_t g_psram_pio_program[] =
{
  0x6828,   /* out x, 8        side 0b01 */
  0x6848,   /* out y, 8        side 0b01 */
  0x0843,   /* jmp x--, 3      side 0b01 */
  0x6001,   /* out pins, 1     side 0b00 */
  0x1043,   /* jmp x--, 3      side 0b10 */
  0x0060,   /* jmp !y, 0       side 0b00 */
  0x1007,   /* jmp 7           side 0b10 */
  0x5001,   /* in pins, 1      side 0b10 */
  0x0087,   /* jmp y--, 7      side 0b00 */
};

#define PSRAM_PIO_PROGRAM_LEN  9

static const uint16_t g_psram_pio_fudge_program[] =
{
  0x6828,   /* out x, 8        side 0b01 */
  0x6848,   /* out y, 8        side 0b01 */
  0x0843,   /* jmp x--, 3      side 0b01 */
  0x6001,   /* out pins, 1     side 0b00 */
  0x1043,   /* jmp x--, 3      side 0b10 */
  0x0060,   /* jmp !y, 0       side 0b00 */
  0xb042,   /* nop             side 0b10 */
  0x0009,   /* jmp 9           side 0b00 */
  0x4001,   /* in pins, 1      side 0b00 */
  0x1088,   /* jmp y--, 8      side 0b10 */
};

#define PSRAM_PIO_FUDGE_PROGRAM_LEN  10

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct psram_dev_s
{
  uint32_t pio_base;           /* PIO peripheral base address */
  uint8_t  sm;                 /* PIO state machine number */
  uint8_t  prog_offset;        /* Program offset in instruction memory */
  uint8_t  dma_write;          /* DMA channel for write */
  uint8_t  dma_read;           /* DMA channel for read */
  uint8_t  dma_async;          /* DMA channel for async write */
  mutex_t  lock;               /* Thread-safety mutex */
  bool     fudge;              /* Use fudge timing program */
  bool     initialized;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct psram_dev_s g_psramdev;

/* Static buffer for PSRAM heap backing.
 * Since we can't memory-map PIO-accessed PSRAM, we allocate a large
 * SRAM region if available, or use a software-managed approach.
 *
 * Note: For the real hardware, the PSRAM is accessed byte-by-byte
 * through PIO SPI commands. For NuttX heap integration, we provide
 * psram_malloc/free that perform PIO transfers transparently.
 * The heap metadata is kept in SRAM; actual data storage in PSRAM.
 */

/* Scratch buffer for PIO transfers (in SRAM, DMA-accessible)
 * Note: g_pio_txbuf/rxbuf reserved for future DMA bulk transfer paths.
 */

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int psram_write8(struct psram_dev_s *dev, uint32_t addr, uint8_t val);
static int psram_read8(struct psram_dev_s *dev, uint32_t addr,
                       uint8_t *val);
void psram_free(void *ptr);
void psram_memcpy(void *dst_handle, const void *src_handle, size_t len);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pio_load_program
 *
 * Description:
 *   Load a PIO program into instruction memory.
 *
 ****************************************************************************/

static int pio_load_program(uint32_t pio_base, const uint16_t *program,
                            size_t len, uint8_t *offset)
{
  /* Find free instruction space (simple: load at offset 0) */

  *offset = 0;

  for (size_t i = 0; i < len; i++)
    {
      uint32_t instr = program[i];

      /* Adjust JMP targets for program offset */

      uint8_t op = (instr >> 13) & 0x07;
      if (op == 0x00)  /* JMP instruction */
        {
          uint8_t target = instr & 0x1F;
          instr = (instr & ~0x1F) | ((target + *offset) & 0x1F);
        }

      putreg32(instr, pio_base + PIO_INSTR_MEM(*offset + i));
    }

  return 0;
}

/****************************************************************************
 * Name: pio_sm_configure_spi
 *
 * Description:
 *   Configure a PIO state machine for SPI PSRAM.
 *
 ****************************************************************************/

static void pio_sm_configure_spi(struct psram_dev_s *dev)
{
  uint32_t base = dev->pio_base;
  uint8_t sm = dev->sm;

  /* Disable state machine */

  uint32_t ctrl = getreg32(base + PIO_CTRL);
  ctrl &= ~(1 << sm);
  putreg32(ctrl, base + PIO_CTRL);

  /* Restart SM (clear internal state) */

  putreg32(1 << (sm + 4), base + PIO_CTRL);

  /* Drain FIFOs */

  putreg32((1u << (sm + 24)) | (1u << (sm + 28)),
           base + PIO_FDEBUG);

  uint32_t pinctrl =
    (2u << 29) |                               /* SIDESET_COUNT = 2 */
    (1u << 20) |                               /* OUT_COUNT = 1 */
    ((uint32_t)BOARD_PSRAM_PIN_MISO << 15) |   /* IN_BASE = MISO */
    ((uint32_t)BOARD_PSRAM_PIN_CS << 10) |     /* SIDESET_BASE = CS */
    ((uint32_t)BOARD_PSRAM_PIN_MOSI << 0);     /* OUT_BASE = MOSI */

  putreg32(pinctrl, base + PIO_SM_PINCTRL(sm));

  /* EXECCTRL: wrap around loaded program */

  uint8_t prog_len = dev->fudge ?
                     PSRAM_PIO_FUDGE_PROGRAM_LEN :
                     PSRAM_PIO_PROGRAM_LEN;

  uint32_t execctrl = getreg32(base + PIO_SM_EXECCTRL(sm));
  execctrl &= ~(0x1F << 7);   /* Clear wrap_bottom */
  execctrl &= ~(0x1F << 12);  /* Clear wrap_top */
  execctrl |= ((dev->prog_offset) << 7);
  execctrl |= ((dev->prog_offset + prog_len - 1) << 12);
  putreg32(execctrl, base + PIO_SM_EXECCTRL(sm));

  /* SHIFTCTRL: autopull/autopush at 8 bits, shift left (MSB first) */

  uint32_t shiftctrl =
    (1u << 17) |   /* AUTOPULL */
    (1u << 16) |   /* AUTOPUSH */
    (8u << 25) |   /* PULL_THRESH = 8 */
    (8u << 20) |   /* PUSH_THRESH = 8 */
    (0u << 19) |   /* OUT_SHIFTDIR = left (MSB first) */
    (0u << 18);    /* IN_SHIFTDIR = left (MSB first) */

  putreg32(shiftctrl, base + PIO_SM_SHIFTCTRL(sm));

  /* CLKDIV: set clock divider as 16.8 fixed-point */

  float div = BOARD_PSRAM_CLKDIV;
  uint16_t div_int = (uint16_t)div;
  uint8_t div_frac = (uint8_t)((div - div_int) * 256);
  uint32_t clkdiv = ((uint32_t)div_int << 16) | ((uint32_t)div_frac << 8);
  putreg32(clkdiv, base + PIO_SM_CLKDIV(sm));

  /* Configure GPIO pins for PIO function */

  uint32_t pio_func = (BOARD_PSRAM_PIO_INST == 0) ? 6 : 7;

  rp23xx_gpio_init(BOARD_PSRAM_PIN_MOSI);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_MOSI, true);
  rp23xx_gpio_set_drive_strength(BOARD_PSRAM_PIN_MOSI,
                                 RP23XX_PADS_BANK0_GPIO_DRIVE_4MA);
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_MOSI, pio_func);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_MISO);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_MISO, false);
  rp23xx_gpio_set_pulls(BOARD_PSRAM_PIN_MISO, 0, 0);  /* No pull */
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_MISO, pio_func);

  /* CS (GP20) and SCK (GP21): PIO function, output */

  rp23xx_gpio_init(BOARD_PSRAM_PIN_CS);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_CS, true);
  rp23xx_gpio_set_drive_strength(BOARD_PSRAM_PIN_CS,
                                 RP23XX_PADS_BANK0_GPIO_DRIVE_4MA);
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_CS, pio_func);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SCK);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SCK, true);
  rp23xx_gpio_set_drive_strength(BOARD_PSRAM_PIN_SCK,
                                 RP23XX_PADS_BANK0_GPIO_DRIVE_4MA);
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_SCK, pio_func);

  /* Enable input synchronizer bypass for MISO */

  uint32_t sync = getreg32(base + PIO_INPUT_SYNC_BYPASS);
  sync |= (1u << BOARD_PSRAM_PIN_MISO);
  putreg32(sync, base + PIO_INPUT_SYNC_BYPASS);

  /* Set initial PC to program start */

  putreg32(dev->prog_offset, base + PIO_SM_INSTR(sm));

  /* Enable state machine */

  ctrl = getreg32(base + PIO_CTRL);
  ctrl |= (1 << sm);
  putreg32(ctrl, base + PIO_CTRL);
}

/****************************************************************************
 * Name: dma_configure
 *
 * Description:
 *   Configure DMA channels for PIO PSRAM transfers.
 *
 ****************************************************************************/

static void dma_configure(struct psram_dev_s *dev)
{
  uint8_t sm = dev->sm;

  /* Determine DREQ based on PIO instance */

  uint8_t dreq_tx = (BOARD_PSRAM_PIO_INST == 0) ?
                    DREQ_PIO0_TX(sm) : DREQ_PIO1_TX(sm);
  uint8_t dreq_rx = (BOARD_PSRAM_PIO_INST == 0) ?
                    DREQ_PIO0_RX(sm) : DREQ_PIO1_RX(sm);

  /* Write DMA channel config:
   * Transfer from memory to PIO TX FIFO
   * 32-bit transfers, source increment, dest fixed
   */

  uint32_t write_ctrl =
    (1u << 0)  |    /* Enable channel */
    (0u << 1)  |    /* DATA_SIZE = 0 (byte) -- will be set per transfer */
    (1u << 4)  |    /* INCR_READ = 1 */
    (0u << 5)  |    /* INCR_WRITE = 0 (PIO FIFO is fixed addr) */
    ((uint32_t)dreq_tx << 15);  /* TREQ_SEL = PIO TX */

  /* Pre-configure: will be finalized before each transfer */

  dma_reg(DMA_CH_WRITE_ADDR(dev->dma_write)) =
    dev->pio_base + PIO_TXF(sm);
  (void)write_ctrl;

  /* Read DMA channel config:
   * Transfer from PIO RX FIFO to memory
   * 32-bit transfers, dest increment, source fixed
   */

  uint32_t read_ctrl =
    (1u << 0)  |
    (0u << 4)  |  /* INCR_READ = 0 (PIO FIFO is fixed addr) */
    (1u << 5)  |  /* INCR_WRITE = 1 */
    ((uint32_t)dreq_rx << 15);

  dma_reg(DMA_CH_READ_ADDR(dev->dma_read)) =
    dev->pio_base + PIO_RXF(sm);
  (void)read_ctrl;
}

/****************************************************************************
 * Name: psram_pio_write_read
 *
 * Description:
 *   Perform a PIO SPI write-then-read transaction.
 *   Header format is [write_bits, read_bits] followed by write payload.
 *   Returns 0 on success, -ETIMEDOUT if PIO FIFO stalls.
 *
 ****************************************************************************/

/* Timeout for PIO FIFO busy-waits.  At 150 MHz the CPU executes ~2
 * instructions per loop iteration; 500 000 iterations ≈ 3–6 ms which is
 * far longer than any single-byte PIO SPI transfer should ever take
 * (< 1 µs at 18.75 MHz SCK).  If we reach the timeout, hardware is stuck.
 */

#define PIO_FIFO_TIMEOUT  500000

static int psram_pio_write_read(struct psram_dev_s *dev,
                                const uint8_t *write_data,
                                size_t write_bits,
                                uint8_t *read_data,
                                size_t read_bits)
{
  uint32_t base = dev->pio_base;
  uint8_t sm = dev->sm;
  uint32_t timeout;

  volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));
  volatile uint8_t *rxfifo = (volatile uint8_t *)(base + PIO_RXF(sm));

  /* The non-fudge PIO program reads y+1 bits (read-then-check loop),
   * while the fudge program reads exactly y bits (check-then-read).
   * Compensate: pass read_bits-1 for non-fudge so PIO reads the
   * correct number of bits.
   */

  uint8_t pio_read_bits = (uint8_t)read_bits;
  if (read_bits > 0 && !dev->fudge)
    {
      pio_read_bits = (uint8_t)(read_bits - 1);
    }

  /* --- Send header: write_bits count --- */

  timeout = PIO_FIFO_TIMEOUT;
  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)))
    {
      if (--timeout == 0)
        {
          return -ETIMEDOUT;
        }
    }

  *txfifo = (uint8_t)write_bits;

  /* --- Send header: read_bits count --- */

  timeout = PIO_FIFO_TIMEOUT;
  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)))
    {
      if (--timeout == 0)
        {
          return -ETIMEDOUT;
        }
    }

  *txfifo = pio_read_bits;

  /* --- Send write payload --- */

  size_t write_bytes = (write_bits + 7) / 8;
  for (size_t i = 0; i < write_bytes; i++)
    {
      timeout = PIO_FIFO_TIMEOUT;
      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)))
        {
          if (--timeout == 0)
            {
              return -ETIMEDOUT;
            }
        }

      *txfifo = write_data[i];
    }

  /* --- Read response --- */

  if (read_bits > 0 && read_data != NULL)
    {
      size_t read_bytes = (read_bits + 7) / 8;
      for (size_t i = 0; i < read_bytes; i++)
        {
          /* Wait for RX FIFO not empty (RXEMPTY bit at position 8+sm).
           * NOTE: RXFULL is at bit sm, RXEMPTY at bit 8+sm in FSTAT.
           */

          timeout = PIO_FIFO_TIMEOUT;
          while (getreg32(base + PIO_FSTAT) & (1u << (8 + sm)))
            {
              if (--timeout == 0)
                {
                  return -ETIMEDOUT;
                }
            }

          read_data[i] = *rxfifo;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: psram_reset
 *
 * Description:
 *   Send reset-enable and reset commands over SPI transport.
 *
 ****************************************************************************/

static int psram_reset(struct psram_dev_s *dev)
{
  uint8_t cmd;
  int ret;

  cmd = PSRAM_CMD_RESET_EN;
  ret = psram_pio_write_read(dev, &cmd, 8, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(100);

  cmd = PSRAM_CMD_RESET;
  ret = psram_pio_write_read(dev, &cmd, 8, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  up_udelay(500);  /* APS6404L tRST ≤ 200 µs; add margin */
  return 0;
}

/****************************************************************************
 * Name: psram_write8
 *
 * Description:
 *   Write a single byte to PSRAM.
 *   Command: 0x02 [A23:A16] [A15:A8] [A7:A0] [DATA]
 *
 ****************************************************************************/

static int psram_write8(struct psram_dev_s *dev, uint32_t addr,
                        uint8_t value)
{
  uint8_t cmd[5];

  cmd[0] = PSRAM_CMD_WRITE;
  cmd[1] = (addr >> 16) & 0xFF;
  cmd[2] = (addr >> 8) & 0xFF;
  cmd[3] = addr & 0xFF;
  cmd[4] = value;

  return psram_pio_write_read(dev, cmd, 40, NULL, 0);
}

/****************************************************************************
 * Name: psram_read8
 *
 * Description:
 *   Read a single byte from PSRAM.
 *   Command: 0x0B [A23:A16] [A15:A8] [A7:A0] [DUMMY] → [DATA]
 *
 ****************************************************************************/

static int psram_read8(struct psram_dev_s *dev, uint32_t addr,
                       uint8_t *result)
{
  uint8_t cmd[5];

  cmd[0] = PSRAM_CMD_FAST_READ;
  cmd[1] = (addr >> 16) & 0xFF;
  cmd[2] = (addr >> 8) & 0xFF;
  cmd[3] = addr & 0xFF;
  cmd[4] = 0x00;

  return psram_pio_write_read(dev, cmd, 40, result, 8);
}

/****************************************************************************
 * Name: psram_write_bulk
 *
 * Description:
 *   Write a block of bytes to PSRAM starting at 'addr'.
 *   The PIO header uses 8-bit bit-counts, so chunk size is capped.
 *
 ****************************************************************************/

#define PSRAM_SPI_MAX_WRITE_DATA  27
#define PSRAM_SPI_MAX_READ_DATA   31

static int psram_write_bulk(struct psram_dev_s *dev, uint32_t addr,
                            const uint8_t *data, size_t len)
{
  int ret;

  while (len > 0)
    {
      size_t page_remain = 1024 - (addr & 0x3FF);
      size_t chunk = (len < page_remain) ? len : page_remain;
      if (chunk > PSRAM_SPI_MAX_WRITE_DATA)
        {
          chunk = PSRAM_SPI_MAX_WRITE_DATA;
        }

      uint8_t txbuf[4 + PSRAM_SPI_MAX_WRITE_DATA];

      txbuf[0] = PSRAM_CMD_WRITE;
      txbuf[1] = (addr >> 16) & 0xFF;
      txbuf[2] = (addr >> 8) & 0xFF;
      txbuf[3] = addr & 0xFF;
      memcpy(&txbuf[4], data, chunk);

      ret = psram_pio_write_read(dev, txbuf, (4 + chunk) * 8, NULL, 0);
      if (ret < 0)
        {
          return ret;
        }

      addr += chunk;
      data += chunk;
      len  -= chunk;
    }

  return 0;
}

/****************************************************************************
 * Name: psram_read_bulk
 *
 * Description:
 *   Read a block of bytes from PSRAM starting at 'addr'.
 *
 ****************************************************************************/

static int psram_read_bulk(struct psram_dev_s *dev, uint32_t addr,
                           uint8_t *data, size_t len)
{
  int ret;

  while (len > 0)
    {
      size_t chunk = (len < PSRAM_SPI_MAX_READ_DATA) ?
             len : PSRAM_SPI_MAX_READ_DATA;

      uint8_t cmd[5];

      cmd[0] = PSRAM_CMD_FAST_READ;
      cmd[1] = (addr >> 16) & 0xFF;
      cmd[2] = (addr >> 8) & 0xFF;
      cmd[3] = addr & 0xFF;
      cmd[4] = 0x00;

      ret = psram_pio_write_read(dev, cmd, 40, data, chunk * 8);
      if (ret < 0)
        {
          return ret;
        }

      addr += chunk;
      data += chunk;
      len  -= chunk;
    }

  return 0;
}

/****************************************************************************
 * Name: psram_test
 *
 * Description:
 *   Quick probe/test: write and read back a few test patterns.
 *   Returns 0 on success, -1 on failure.
 *
 ****************************************************************************/

static int psram_test(struct psram_dev_s *dev)
{
  /* Test pattern at address 0 */

  psram_write8(dev, 0, 0xDE);
  uint8_t val = psram_read8(dev, 0);
  if (val != 0xDE)
    {
      syslog(LOG_ERR, "psram: probe failed at addr 0x%06X "
             "(wrote 0xDE, read 0x%02X)\n", 0u, (unsigned)val);
      return -1;
    }

  /* Test pattern at address 0x100 */

  psram_write8(dev, 0x100, 0xAD);
  val = psram_read8(dev, 0x100);
  if (val != 0xAD)
    {
      syslog(LOG_ERR, "psram: probe failed at addr 0x000100 "
             "(wrote 0xAD, read 0x%02X)\n", val);
      return -1;
    }

  /* Verify original address wasn't corrupted */

  val = psram_read8(dev, 0);
  if (val != 0xDE)
    {
      syslog(LOG_ERR, "psram: address isolation test failed\n");
      return -1;
    }

  /* Test at end of memory (8 MB - 1) */

  uint32_t end_addr = BOARD_PSRAM_SIZE - 1;
  psram_write8(dev, end_addr, 0xBE);
  val = psram_read8(dev, end_addr);
  if (val != 0xBE)
    {
      syslog(LOG_ERR, "psram: probe failed at addr 0x%06lX "
             "(wrote 0xBE, read 0x%02lX)\n",
             (unsigned long)end_addr, (unsigned long)val);
      return -1;
    }

  /* Quick bulk test: write 16 bytes, read back */

  uint8_t test_write[16];
  uint8_t test_read[16];

  for (int i = 0; i < 16; i++)
    {
      test_write[i] = i ^ 0xA5;
    }

  psram_write_bulk(dev, 0x200, test_write, 16);
  psram_read_bulk(dev, 0x200, test_read, 16);

  for (int i = 0; i < 16; i++)
    {
      if (test_read[i] != test_write[i])
        {
          syslog(LOG_ERR, "psram: bulk test failed at offset %d "
                 "(0x%02X != 0x%02X)\n", i, test_read[i], test_write[i]);
          return -1;
        }
    }

  syslog(LOG_INFO, "psram: probe passed (single byte + bulk 16B verified)\n");
  return 0;
}

/****************************************************************************
 * PSRAM-backed heap implementation
 *
 * Since the PSRAM is not memory-mapped (accessed via PIO SPI), we maintain
 * a simple block allocator. Heap metadata is in SRAM; actual data is
 * transferred to/from PSRAM on demand.
 *
 * For large allocations (framebuffers, audio buffers), the allocation
 * returns a PSRAM address handle. The caller uses psram_read/psram_write
 * to access the data. This is transparent for most use cases.
 *
 * For simplicity in this implementation, we use a bump allocator
 * with free list for the PSRAM address space.
 *
 ****************************************************************************/

#define PSRAM_BLOCK_MAGIC 0x50535200  /* "PSR\0" */
#define PSRAM_MAX_BLOCKS  512

struct psram_block_s
{
  uint32_t addr;       /* PSRAM address */
  uint32_t size;       /* Block size */
  bool     used;       /* In use */
};

static struct psram_block_s g_psram_blocks[PSRAM_MAX_BLOCKS];
static uint32_t g_psram_next_addr = 0;  /* Next free address (bump) */
static int g_psram_block_count = 0;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_psram_init
 *
 * Description:
 *   Initialize the PIO-driven PSRAM interface and verify the chip.
 *   Sets up PIO program, DMA channels, performs reset sequence,
 *   and runs a quick hardware test.
 *
 ****************************************************************************/

int rp23xx_psram_init(void)
{
  struct psram_dev_s *dev = &g_psramdev;

  if (dev->initialized)
    {
      return 0;
    }

  syslog(LOG_INFO, "psram: initializing SPI interface on PIO%d...\n",
         BOARD_PSRAM_PIO_INST);

  nxmutex_init(&dev->lock);

  dev->pio_base = PIO_BASE;
  dev->sm = BOARD_PSRAM_PIO_SM;
  dev->dma_write = DMA_CH_WRITE;
  dev->dma_read = DMA_CH_READ;
  dev->dma_async = DMA_CH_ASYNC;
  dev->fudge = BOARD_PSRAM_USE_FUDGE;

  /* Load SPI PIO program and configure state machine */

  const uint16_t *program = dev->fudge ?
        g_psram_pio_fudge_program :
        g_psram_pio_program;
  size_t prog_len = dev->fudge ?
        PSRAM_PIO_FUDGE_PROGRAM_LEN :
        PSRAM_PIO_PROGRAM_LEN;

  pio_load_program(dev->pio_base, program, prog_len,
                   &dev->prog_offset);

  syslog(LOG_DEBUG, "psram: SPI PIO program loaded at offset %d "
         "(%d instructions)\n",
         dev->prog_offset, (int)prog_len);

  pio_sm_configure_spi(dev);

  dma_configure(dev);

  /* Reset PSRAM chip after transport is active */

  psram_reset(dev);

  syslog(LOG_INFO, "psram: GPIO CS=GP%d SCK=GP%d "
         "MOSI=GP%d MISO=GP%d (SPI)\n",
         BOARD_PSRAM_PIN_CS, BOARD_PSRAM_PIN_SCK,
         BOARD_PSRAM_PIN_MOSI, BOARD_PSRAM_PIN_MISO);

  /* Verify PSRAM is accessible (retry up to 3 times) */

  int probe_ok = -1;
  for (int attempt = 0; attempt < 3; attempt++)
    {
      if (attempt > 0)
        {
          syslog(LOG_WARNING, "psram: retry %d/3...\n", attempt + 1);
          psram_reset(dev);
        }

      if (psram_test(dev) == 0)
        {
          probe_ok = 0;
          break;
        }
    }

  if (probe_ok != 0)
    {
      syslog(LOG_ERR, "psram: SPI probe failed after 3 attempts\n");
      return -EIO;
    }

  memset(g_psram_blocks, 0, sizeof(g_psram_blocks));
  g_psram_next_addr = 0;
  g_psram_block_count = 0;

  dev->initialized = true;

  syslog(LOG_INFO, "psram: APS6404L %d KB SPI ready "
         "(PIO%d SM%d, clkdiv=%.1f, fudge=%s)\n",
         BOARD_PSRAM_SIZE / 1024,
         BOARD_PSRAM_PIO_INST, BOARD_PSRAM_PIO_SM,
         (double)BOARD_PSRAM_CLKDIV,
         dev->fudge ? "yes" : "no");

  return 0;
}

/****************************************************************************
 * Name: psram_malloc / psram_free / psram_realloc
 *
 * Description:
 *   PSRAM allocation functions. Returns a pointer-sized handle that
 *   encodes the PSRAM address. Use psram_read_data / psram_write_data
 *   to access the actual data.
 *
 *   For compatibility with existing code expecting memory-mapped access,
 *   we return a handle that looks like a pointer in the PSRAM address range
 *   (0x11000000+). Code that dereferences this directly will fault —
 *   callers must use psram_memcpy_to/from for non-mapped access,
 *   or the NuttX mm layer for managed access.
 *
 ****************************************************************************/

/* PSRAM handle base: encodes PSRAM addr as a "pointer" for API compat */

#define PSRAM_HANDLE_BASE  0x11000000

void *psram_malloc(size_t size)
{
  struct psram_dev_s *dev = &g_psramdev;

  if (!dev->initialized || size == 0)
    {
      return NULL;
    }

  nxmutex_lock(&dev->lock);

  /* Align to 4 bytes */

  size = (size + 3) & ~3;

  /* First-fit in free list */

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (!g_psram_blocks[i].used && g_psram_blocks[i].size >= size)
        {
          g_psram_blocks[i].used = true;

          /* If block is significantly larger, split it */

          if (g_psram_blocks[i].size > size + 64 &&
              g_psram_block_count < PSRAM_MAX_BLOCKS)
            {
              int j = g_psram_block_count++;
              g_psram_blocks[j].addr = g_psram_blocks[i].addr + size;
              g_psram_blocks[j].size = g_psram_blocks[i].size - size;
              g_psram_blocks[j].used = false;
              g_psram_blocks[i].size = size;
            }

          nxmutex_unlock(&dev->lock);
          return (void *)(uintptr_t)(PSRAM_HANDLE_BASE +
                                     g_psram_blocks[i].addr);
        }
    }

  /* Bump allocate from end */

  if (g_psram_next_addr + size > BOARD_PSRAM_SIZE)
    {
      nxmutex_unlock(&dev->lock);
      syslog(LOG_ERR, "psram: out of memory (requested %zu, "
             "used %lu of %d)\n", size,
             (unsigned long)g_psram_next_addr, BOARD_PSRAM_SIZE);
      return NULL;
    }

  if (g_psram_block_count >= PSRAM_MAX_BLOCKS)
    {
      nxmutex_unlock(&dev->lock);
      syslog(LOG_ERR, "psram: block table full\n");
      return NULL;
    }

  int idx = g_psram_block_count++;
  g_psram_blocks[idx].addr = g_psram_next_addr;
  g_psram_blocks[idx].size = size;
  g_psram_blocks[idx].used = true;

  g_psram_next_addr += size;

  nxmutex_unlock(&dev->lock);

  return (void *)(uintptr_t)(PSRAM_HANDLE_BASE +
                             g_psram_blocks[idx].addr);
}

void *psram_realloc(void *ptr, size_t size)
{
  if (ptr == NULL)
    {
      return psram_malloc(size);
    }

  if (size == 0)
    {
      psram_free(ptr);
      return NULL;
    }

  /* Find existing block */

  uint32_t old_addr = (uint32_t)(uintptr_t)ptr - PSRAM_HANDLE_BASE;

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (g_psram_blocks[i].addr == old_addr && g_psram_blocks[i].used)
        {
          if (g_psram_blocks[i].size >= size)
            {
              return ptr;  /* Already large enough */
            }

          /* Allocate new block, copy data, free old */

          void *new_ptr = psram_malloc(size);
          if (new_ptr == NULL)
            {
              return NULL;
            }

          /* Copy via PSRAM read/write */

          psram_memcpy(new_ptr, ptr, g_psram_blocks[i].size);
          psram_free(ptr);
          return new_ptr;
        }
    }

  return NULL;
}

void psram_free(void *ptr)
{
  struct psram_dev_s *dev = &g_psramdev;

  if (!dev->initialized || ptr == NULL)
    {
      return;
    }

  uint32_t addr = (uint32_t)(uintptr_t)ptr - PSRAM_HANDLE_BASE;

  nxmutex_lock(&dev->lock);

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (g_psram_blocks[i].addr == addr && g_psram_blocks[i].used)
        {
          g_psram_blocks[i].used = false;

          /* Coalesce with adjacent free blocks.
           * We do multiple passes until no more merges happen,
           * which handles chains of freed blocks.
           */

          bool merged;
          do
            {
              merged = false;

              for (int j = 0; j < g_psram_block_count; j++)
                {
                  if (j == i || g_psram_blocks[j].size == 0)
                    {
                      continue;
                    }

                  if (!g_psram_blocks[j].used)
                    {
                      /* Check if j is immediately after i */

                      if (g_psram_blocks[j].addr ==
                          g_psram_blocks[i].addr +
                          g_psram_blocks[i].size)
                        {
                          g_psram_blocks[i].size +=
                            g_psram_blocks[j].size;

                          /* Remove block j by shifting */

                          for (int k = j; k < g_psram_block_count - 1;
                               k++)
                            {
                              g_psram_blocks[k] = g_psram_blocks[k + 1];
                            }

                          g_psram_block_count--;

                          /* Adjust index i if it moved */

                          if (j < i)
                            {
                              i--;
                            }

                          merged = true;
                          break;  /* Restart scan */
                        }

                      /* Check if j is immediately before i */

                      if (g_psram_blocks[i].addr ==
                          g_psram_blocks[j].addr +
                          g_psram_blocks[j].size)
                        {
                          g_psram_blocks[j].size +=
                            g_psram_blocks[i].size;

                          /* Remove block i by shifting */

                          for (int k = i; k < g_psram_block_count - 1;
                               k++)
                            {
                              g_psram_blocks[k] = g_psram_blocks[k + 1];
                            }

                          g_psram_block_count--;

                          /* Block i is now removed; j is the merged
                           * free block. Switch to track j as "i".
                           */

                          i = j;
                          merged = true;
                          break;  /* Restart scan */
                        }
                    }
                }
            }
          while (merged);

          break;
        }
    }

  nxmutex_unlock(&dev->lock);
}

size_t psram_available(void)
{
  struct psram_dev_s *dev = &g_psramdev;

  if (!dev->initialized)
    {
      return 0;
    }

  /* Sum of free blocks + unallocated space */

  size_t free_space = BOARD_PSRAM_SIZE - g_psram_next_addr;

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (!g_psram_blocks[i].used && g_psram_blocks[i].size > 0)
        {
          free_space += g_psram_blocks[i].size;
        }
    }

  return free_space;
}

/****************************************************************************
 * Name: psram_memcpy_to / psram_memcpy_from / psram_memcpy
 *
 * Description:
 *   Copy data to/from PSRAM using PIO SPI transfers.
 *   These are the primary data access functions for non-mapped PSRAM.
 *
 ****************************************************************************/

void psram_memcpy_to(void *psram_handle, const void *src, size_t len)
{
  struct psram_dev_s *dev = &g_psramdev;
  uint32_t addr = (uint32_t)(uintptr_t)psram_handle - PSRAM_HANDLE_BASE;

  nxmutex_lock(&dev->lock);
  psram_write_bulk(dev, addr, (const uint8_t *)src, len);
  nxmutex_unlock(&dev->lock);
}

void psram_memcpy_from(void *dst, const void *psram_handle, size_t len)
{
  struct psram_dev_s *dev = &g_psramdev;
  uint32_t addr = (uint32_t)(uintptr_t)psram_handle - PSRAM_HANDLE_BASE;

  nxmutex_lock(&dev->lock);
  psram_read_bulk(dev, addr, (uint8_t *)dst, len);
  nxmutex_unlock(&dev->lock);
}

void psram_memcpy(void *dst_handle, const void *src_handle, size_t len)
{
  /* PSRAM-to-PSRAM copy via SRAM staging buffer */

  uint8_t staging[256];

  while (len > 0)
    {
      size_t chunk = (len < sizeof(staging)) ? len : sizeof(staging);

      psram_memcpy_from(staging, src_handle, chunk);
      psram_memcpy_to(dst_handle, staging, chunk);

      dst_handle = (void *)((uintptr_t)dst_handle + chunk);
      src_handle = (void *)((uintptr_t)src_handle + chunk);
      len -= chunk;
    }
}

void psram_memset(void *psram_handle, int value, size_t len)
{
  struct psram_dev_s *dev = &g_psramdev;
  uint32_t addr = (uint32_t)(uintptr_t)psram_handle - PSRAM_HANDLE_BASE;
  uint8_t staging[256];
  uint8_t val8 = (uint8_t)value;

  memset(staging, val8, sizeof(staging));

  nxmutex_lock(&dev->lock);

  while (len > 0)
    {
      size_t chunk = (len < sizeof(staging)) ? len : sizeof(staging);
      psram_write_bulk(dev, addr, staging, chunk);
      addr += chunk;
      len -= chunk;
    }

  nxmutex_unlock(&dev->lock);
}
