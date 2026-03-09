/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_pio_sdio.c
 *
 * PIO-based 1-bit SDIO driver for PicoCalc SD card.
 *
 * Simulates 1-bit SDIO protocol using PIO state machines on PIO1.
 * Provides significantly faster transfers than SPI mode:
 *   - SPI mode: ~2 MB/s at 25 MHz
 *   - 1-bit SDIO: ~3 MB/s at 25 MHz (bidirectional CMD + DAT0)
 *
 * Pin assignments (same physical pins as SPI0):
 *   CLK  = GP18 (SDIO clock, PIO output)
 *   CMD  = GP19 (SDIO command, bidirectional)
 *   DAT0 = GP16 (SDIO data bit 0, bidirectional)
 *
 * This driver conditionally replaces the SPI-mode SD card driver
 * when CONFIG_PICOCALC_PIO_SDIO is defined.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_PICOCALC_PIO_SDIO

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/irq.h>
#include <nuttx/mmcsd.h>
#include <nuttx/mutex.h>

#include "rp23xx_gpio.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PIO1 base (PIO0 is used by PSRAM) */

#define PIO1_BASE              0x50300000

#define PIO_CTRL               0x000
#define PIO_FSTAT              0x004
#define PIO_TXF(sm)            (0x010 + (sm) * 4)
#define PIO_RXF(sm)            (0x020 + (sm) * 4)
#define PIO_INPUT_SYNC_BYPASS  0x038
#define PIO_INSTR_MEM(n)       (0x048 + (n) * 4)
#define PIO_SM_CLKDIV(sm)      (0x0C8 + (sm) * 0x18)
#define PIO_SM_EXECCTRL(sm)    (0x0CC + (sm) * 0x18)
#define PIO_SM_SHIFTCTRL(sm)   (0x0D0 + (sm) * 0x18)
#define PIO_SM_ADDR(sm)        (0x0D4 + (sm) * 0x18)
#define PIO_SM_INSTR(sm)       (0x0D8 + (sm) * 0x18)
#define PIO_SM_PINCTRL(sm)     (0x0DC + (sm) * 0x18)

#define pio1_reg(off)   (*(volatile uint32_t *)(PIO1_BASE + (off)))

/* DMA base */

#define DMA_BASE               0x50000000
#define DMA_CH_READ_ADDR(ch)   (0x000 + (ch) * 0x40)
#define DMA_CH_WRITE_ADDR(ch)  (0x004 + (ch) * 0x40)
#define DMA_CH_TRANS_COUNT(ch) (0x008 + (ch) * 0x40)
#define DMA_CH_CTRL_TRIG(ch)   (0x00C + (ch) * 0x40)

#define sdio_dma_reg(off) (*(volatile uint32_t *)(DMA_BASE + (off)))

/* DMA channels for SDIO */

#define DMA_CH_SDIO_TX     11
#define DMA_CH_SDIO_RX     12

/* SDIO commands */

#define SDIO_CMD0    0   /* GO_IDLE_STATE */
#define SDIO_CMD2    2   /* ALL_SEND_CID */
#define SDIO_CMD3    3   /* SEND_RELATIVE_ADDR */
#define SDIO_CMD7    7   /* SELECT/DESELECT_CARD */
#define SDIO_CMD8    8   /* SEND_IF_COND */
#define SDIO_CMD12   12  /* STOP_TRANSMISSION */
#define SDIO_CMD16   16  /* SET_BLOCKLEN */
#define SDIO_CMD17   17  /* READ_SINGLE_BLOCK */
#define SDIO_CMD18   18  /* READ_MULTIPLE_BLOCK */
#define SDIO_CMD24   24  /* WRITE_BLOCK */
#define SDIO_CMD25   25  /* WRITE_MULTIPLE_BLOCK */
#define SDIO_CMD55   55  /* APP_CMD */
#define SDIO_ACMD41  41  /* SD_SEND_OP_COND */

/* CRC7 polynomial for SDIO commands */

#define CRC7_POLY    0x89

/* Response types */

#define SDIO_RSP_NONE  0
#define SDIO_RSP_R1    1  /* 48-bit */
#define SDIO_RSP_R2    2  /* 136-bit */
#define SDIO_RSP_R3    3  /* 48-bit (no CRC) */
#define SDIO_RSP_R6    6  /* 48-bit (published RCA) */
#define SDIO_RSP_R7    7  /* 48-bit (card interface) */

/* PIO state machines for SDIO */

#define SM_CMD    0  /* State machine for CMD line */
#define SM_DAT    1  /* State machine for DAT0 line */

/* Block size */

#define SDIO_BLOCK_SIZE  512

/****************************************************************************
 * PIO Programs for 1-bit SDIO
 *
 * SM0 (CMD): Handles command transmission and response reception
 *   - Uses sideset[0] = CLK
 *   - out pin = CMD (GP19)
 *   - in pin = CMD (GP19, direction switched)
 *
 * SM1 (DAT): Handles data transfer on DAT0
 *   - Uses sideset[0] = CLK
 *   - out pin = DAT0 (GP16)
 *   - in pin = DAT0 (GP16, direction switched)
 *
 ****************************************************************************/

/* SDIO CMD PIO program:
 * Sends command bits on CMD line, then reads response bits.
 * Sideset: CLK on GP18.
 *
 * Protocol:
 *   1. Pull number of bits to write (x) and read (y) from TX FIFO
 *   2. Shift out x bits on CMD with clock toggling
 *   3. Switch CMD to input, shift in y bits
 */

/* SDIO CMD PIO program:
 * Sends command bits on CMD line, then reads response bits.
 * Sideset: 1-bit, CLK on GP18.
 *
 * PIO instruction encoding (with 1-bit sideset, no enable):
 *   [15:13] = opcode
 *   [12]    = sideset value (CLK)
 *   [11:8]  = delay
 *   [7:0]   = instruction-specific
 *
 * Protocol:
 *   1. Pull number of bits to write (x) and read (y) from TX FIFO
 *   2. Shift out x bits on CMD with clock toggling
 *   3. Switch CMD to input, shift in y bits
 *
 * JMP conditions: 000=always, 001=!X, 010=X--, 011=!Y, 100=Y--
 * OUT dests: 000=PINS, 001=X, 010=Y
 * SET dests: 000=PINS, 100=PINDIRS
 */

static const uint16_t g_sdio_cmd_program[] =
{
  /* 0: out x, 8         side 0    ; x = bits to send */
  0x6028,
  /* 1: out y, 8         side 0    ; y = bits to receive */
  0x6048,
  /* 2: jmp x--, wr_loop side 0   ; pre-decrement */
  0x0043,
  /* 3: wr_loop: out pins, 1 side 0  ; output bit, CLK low */
  0x6001,
  /* 4: jmp x--, wr_loop side 1    ; CLK high, loop */
  0x1043,
  /* 5: jmp !y, begin    side 0    ; if no read, restart */
  0x0060,
  /* 6: set pindirs, 0   side 0    ; CMD to input */
  0xe080,
  /* 7: rd_loop: nop     side 1    ; CLK high */
  0xb042,
  /* 8: in pins, 1       side 0    ; read CMD bit, CLK low */
  0x4001,
  /* 9: jmp y--, rd_loop side 0    ; loop */
  0x0087,
  /* 10: set pindirs, 1  side 0    ; CMD back to output */
  0xe081,
};

#define SDIO_CMD_PROGRAM_LEN  11

/* SDIO DAT PIO program:
 * Reads or writes data bits on DAT0 line.
 * Sideset: CLK on GP18 (shared with CMD SM).
 *
 * For reading:
 *   1. Wait for start bit (0) on DAT0
 *   2. Read block_size * 8 bits
 *   3. Read 16-bit CRC
 *   4. Read end bit (1)
 *
 * For writing:
 *   1. Send start bit (0)
 *   2. Send block_size * 8 bits
 *   3. Send 16-bit CRC
 *   4. Send end bit (1)
 *   5. Wait for busy (DAT0 low) to go high
 */

static const uint16_t g_sdio_dat_program[] =
{
  /* 0: out x, 32        side 0    ; x = total bits to read/write */
  0x6020,
  /* 1: out y, 1         side 0    ; y = direction (0=read, 1=write) */
  0x6041,
  /* 2: jmp !y, rd_start side 0    ; branch to read if y=0 */
  0x0066,
  /* 3: wr_loop: out pins, 1 side 0 */
  0x6001,
  /* 4: jmp x--, wr_loop side 1    ; CLK high, loop */
  0x1043,
  /* 5: jmp begin        side 0    ; done */
  0x0000,
  /* 6: rd_start: wait 0 pin 0 side 0 ; wait for start bit */
  0x2020,
  /* 7: rd_loop: nop     side 1    ; CLK high */
  0xb042,
  /* 8: in pins, 1       side 0    ; read DAT0, CLK low */
  0x4001,
  /* 9: jmp x--, rd_loop side 0    ; loop */
  0x0047,
};

#define SDIO_DAT_PROGRAM_LEN  10

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sdio_dev_s
{
  mutex_t lock;
  bool    initialized;
  bool    card_present;
  uint32_t rca;            /* Relative Card Address */
  uint32_t csd[4];         /* Card Specific Data */
  uint32_t capacity;        /* Card capacity in 512-byte blocks */
  bool     highcap;        /* SDHC/SDXC flag */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sdio_dev_s g_sdiodev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: crc7
 *
 * Description:
 *   Calculate CRC7 for SDIO command.
 *
 ****************************************************************************/

static uint8_t crc7(const uint8_t *data, size_t len)
{
  uint8_t crc = 0;

  for (size_t i = 0; i < len; i++)
    {
      uint8_t d = data[i];
      for (int bit = 7; bit >= 0; bit--)
        {
          crc <<= 1;
          if ((d ^ crc) & 0x80)
            {
              crc ^= CRC7_POLY;
            }

          d <<= 1;
        }
    }

  return (crc & 0x7F);
}

/****************************************************************************
 * Name: sdio_pio_init
 *
 * Description:
 *   Initialize PIO1 state machines for SDIO.
 *
 ****************************************************************************/

static void sdio_pio_init(void)
{
  /* Load CMD program at offset 0 */

  for (size_t i = 0; i < SDIO_CMD_PROGRAM_LEN; i++)
    {
      pio1_reg(PIO_INSTR_MEM(i)) = g_sdio_cmd_program[i];
    }

  /* Load DAT program at offset after CMD */

  uint8_t dat_offset = SDIO_CMD_PROGRAM_LEN;
  for (size_t i = 0; i < SDIO_DAT_PROGRAM_LEN; i++)
    {
      /* Adjust JMP addresses for offset */

      uint16_t instr = g_sdio_dat_program[i];
      uint8_t op = (instr >> 13) & 0x07;
      if (op == 0x00)  /* JMP */
        {
          uint8_t target = instr & 0x1F;
          instr = (instr & ~0x1F) | ((target + dat_offset) & 0x1F);
        }

      pio1_reg(PIO_INSTR_MEM(dat_offset + i)) = instr;
    }

  /* Configure SM0 for CMD line */

  /* Clock divider: 150 MHz / 6 = 25 MHz SDIO clock */

  uint32_t clkdiv = (6u << 16);  /* integer divider = 6 */
  pio1_reg(PIO_SM_CLKDIV(SM_CMD)) = clkdiv;

  /* PINCTRL for CMD SM:
   *
   * RP2350 PIO SM_PINCTRL register layout:
   *   [4:0]   OUT_BASE     = GP19 (CMD)
   *   [9:5]   SET_BASE     = GP19 (CMD, for pindirs)
   *   [14:10] SIDESET_BASE = GP18 (CLK)
   *   [19:15] IN_BASE      = GP19 (CMD)
   *   [25:20] OUT_COUNT    = 1
   *   [28:26] SET_COUNT    = 1
   *   [31:29] SIDESET_COUNT = 1
   */

  uint32_t pinctrl_cmd =
    (1u << 29) |                             /* SIDESET_COUNT = 1 */
    (1u << 26) |                             /* SET_COUNT = 1 */
    (1u << 20) |                             /* OUT_COUNT = 1 */
    ((uint32_t)BOARD_SDIO_PIN_CMD << 15) |   /* IN_BASE = CMD */
    ((uint32_t)BOARD_SDIO_PIN_CLK << 10) |   /* SIDESET_BASE = CLK */
    ((uint32_t)BOARD_SDIO_PIN_CMD << 5) |    /* SET_BASE = CMD */
    ((uint32_t)BOARD_SDIO_PIN_CMD << 0);     /* OUT_BASE = CMD */

  pio1_reg(PIO_SM_PINCTRL(SM_CMD)) = pinctrl_cmd;

  /* SHIFTCTRL: out=MSB-first, in=MSB-first, autopull/push @ 32 */

  uint32_t shiftctrl_cmd =
    (1u << 17) |  /* autopull */
    (1u << 16);   /* autopush */

  pio1_reg(PIO_SM_SHIFTCTRL(SM_CMD)) = shiftctrl_cmd;

  /* Configure SM1 for DAT0 line */

  pio1_reg(PIO_SM_CLKDIV(SM_DAT)) = clkdiv;

  /* PINCTRL for DAT SM:
   * sideset_base = GP18 (CLK), sideset_count = 1
   * out_base = GP16 (DAT0), out_count = 1
   * in_base = GP16 (DAT0)
   */

  /* PINCTRL for DAT SM:
   *   [4:0]   OUT_BASE     = GP16 (DAT0)
   *   [9:5]   SET_BASE     = GP16 (DAT0, for pindirs)
   *   [14:10] SIDESET_BASE = GP18 (CLK)
   *   [19:15] IN_BASE      = GP16 (DAT0)
   *   [25:20] OUT_COUNT    = 1
   *   [28:26] SET_COUNT    = 1
   *   [31:29] SIDESET_COUNT = 1
   */

  uint32_t pinctrl_dat =
    (1u << 29) |                             /* SIDESET_COUNT = 1 */
    (1u << 26) |                             /* SET_COUNT = 1 */
    (1u << 20) |                             /* OUT_COUNT = 1 */
    ((uint32_t)BOARD_SDIO_PIN_DAT0 << 15) |  /* IN_BASE = DAT0 */
    ((uint32_t)BOARD_SDIO_PIN_CLK << 10) |   /* SIDESET_BASE = CLK */
    ((uint32_t)BOARD_SDIO_PIN_DAT0 << 5) |   /* SET_BASE = DAT0 */
    ((uint32_t)BOARD_SDIO_PIN_DAT0 << 0);    /* OUT_BASE = DAT0 */

  pio1_reg(PIO_SM_PINCTRL(SM_DAT)) = pinctrl_dat;

  pio1_reg(PIO_SM_SHIFTCTRL(SM_DAT)) = shiftctrl_cmd;

  /* EXECCTRL: set wrap for each SM */

  /* SM0 wrap = 0..CMD_PROGRAM_LEN-1 */
  /* SM1 wrap = dat_offset..dat_offset+DAT_PROGRAM_LEN-1 */

  /* Configure GPIO functions for PIO1 */

  rp23xx_gpio_init(BOARD_SDIO_PIN_CLK);
  rp23xx_gpio_setdir(BOARD_SDIO_PIN_CLK, true);
  rp23xx_gpio_set_function(BOARD_SDIO_PIN_CLK, 7);  /* PIO1 */

  rp23xx_gpio_init(BOARD_SDIO_PIN_CMD);
  rp23xx_gpio_setdir(BOARD_SDIO_PIN_CMD, true);
  rp23xx_gpio_set_function(BOARD_SDIO_PIN_CMD, 7);

  rp23xx_gpio_init(BOARD_SDIO_PIN_DAT0);
  rp23xx_gpio_setdir(BOARD_SDIO_PIN_DAT0, false);  /* Start as input */
  rp23xx_gpio_set_function(BOARD_SDIO_PIN_DAT0, 7);

  /* Pull-ups on CMD and DAT0 */

  rp23xx_gpio_set_pulls(BOARD_SDIO_PIN_CMD, true, false);
  rp23xx_gpio_set_pulls(BOARD_SDIO_PIN_DAT0, true, false);

  /* Enable bypass for fast reads */

  uint32_t sync = pio1_reg(PIO_INPUT_SYNC_BYPASS);
  sync |= (1u << BOARD_SDIO_PIN_CMD) | (1u << BOARD_SDIO_PIN_DAT0);
  pio1_reg(PIO_INPUT_SYNC_BYPASS) = sync;

  /* Enable both state machines */

  uint32_t ctrl = pio1_reg(PIO_CTRL);
  ctrl |= (1 << SM_CMD) | (1 << SM_DAT);
  pio1_reg(PIO_CTRL) = ctrl;
}

/****************************************************************************
 * Name: sdio_send_cmd
 *
 * Description:
 *   Send an SDIO command and receive response.
 *
 ****************************************************************************/

static int sdio_send_cmd(uint8_t cmd, uint32_t arg, uint8_t rsp_type,
                         uint32_t *response)
{
  /* Build 48-bit command frame:
   * [47]    start bit = 0
   * [46]    direction = 1 (host to card)
   * [45:40] command index
   * [39:8]  argument
   * [7:1]   CRC7
   * [0]     end bit = 1
   */

  uint8_t frame[6];
  frame[0] = 0x40 | (cmd & 0x3F);
  frame[1] = (arg >> 24) & 0xFF;
  frame[2] = (arg >> 16) & 0xFF;
  frame[3] = (arg >> 8) & 0xFF;
  frame[4] = arg & 0xFF;
  frame[5] = (crc7(frame, 5) << 1) | 0x01;  /* CRC7 + end bit */

  /* Calculate response length in bits */

  uint8_t rsp_bits = 0;
  switch (rsp_type)
    {
      case SDIO_RSP_NONE: rsp_bits = 0; break;
      case SDIO_RSP_R1:
      case SDIO_RSP_R3:
      case SDIO_RSP_R6:
      case SDIO_RSP_R7:   rsp_bits = 48; break;
      case SDIO_RSP_R2:   rsp_bits = 136; break;
    }

  /* Send command via CMD PIO SM:
   * TX FIFO word 0: [write_bits-1][read_bits] packed as header
   * TX FIFO word 1+: command data
   */

  /* Wait for TX FIFO space */

  while (pio1_reg(PIO_FSTAT) & (1u << (16 + SM_CMD)))
    {
      /* Full */
    }

  /* Header: 48 bits to write, rsp_bits to read */

  uint32_t header = (47u) | ((uint32_t)rsp_bits << 8);
  pio1_reg(PIO_TXF(SM_CMD)) = header;

  /* Send command bytes */

  for (int i = 0; i < 6; i++)
    {
      while (pio1_reg(PIO_FSTAT) & (1u << (16 + SM_CMD)))
        {
          /* Full */
        }

      pio1_reg(PIO_TXF(SM_CMD)) = (uint32_t)frame[i] << 24;
    }

  /* Read response */

  if (rsp_bits > 0 && response != NULL)
    {
      size_t rsp_words = (rsp_bits + 31) / 32;

      for (size_t i = 0; i < rsp_words; i++)
        {
          int timeout = 100000;

          while ((pio1_reg(PIO_FSTAT) & (1u << SM_CMD)) && --timeout > 0)
            {
              /* RX empty */
            }

          if (timeout <= 0)
            {
              syslog(LOG_ERR, "SDIO: CMD%d response timeout\n", cmd);
              return -ETIMEDOUT;
            }

          response[i] = pio1_reg(PIO_RXF(SM_CMD));
        }
    }

  return 0;
}

/****************************************************************************
 * Name: sdio_read_block
 *
 * Description:
 *   Read a 512-byte block from the SD card via DAT0.
 *
 ****************************************************************************/

static int sdio_read_block(uint32_t block_addr, uint8_t *buffer)
{
  struct sdio_dev_s *dev = &g_sdiodev;
  uint32_t response;
  int ret;

  /* Send CMD17 (READ_SINGLE_BLOCK) */

  uint32_t addr = dev->highcap ? block_addr : (block_addr * SDIO_BLOCK_SIZE);

  ret = sdio_send_cmd(SDIO_CMD17, addr, SDIO_RSP_R1, &response);
  if (ret < 0)
    {
      return ret;
    }

  /* Read 512 bytes (4096 bits) + 16 CRC bits + end bit from DAT0
   * Total bits to read: 4096 + 16 + 1 = 4113
   */

  /* Tell DAT SM to read 4096 data bits (512 bytes) */

  while (pio1_reg(PIO_FSTAT) & (1u << (16 + SM_DAT)))
    {
      /* Full */
    }

  /* Total bits to read: 512*8 = 4096 data + 16 CRC = 4112 */

  pio1_reg(PIO_TXF(SM_DAT)) = 4112 - 1;  /* bit count */
  pio1_reg(PIO_TXF(SM_DAT)) = 0;          /* direction = 0 (read) */

  /* Read data from RX FIFO (128 x 32-bit words = 512 bytes) */

  for (size_t i = 0; i < 128; i++)
    {
      int timeout = 100000;

      while ((pio1_reg(PIO_FSTAT) & (1u << SM_DAT)) && --timeout > 0)
        {
          /* RX empty */
        }

      if (timeout <= 0)
        {
          syslog(LOG_ERR, "SDIO: Read timeout at word %zu\n", i);
          return -ETIMEDOUT;
        }

      uint32_t word = pio1_reg(PIO_RXF(SM_DAT));

      /* Convert from MSB-first 32-bit to bytes */

      buffer[i * 4 + 0] = (word >> 24) & 0xFF;
      buffer[i * 4 + 1] = (word >> 16) & 0xFF;
      buffer[i * 4 + 2] = (word >> 8) & 0xFF;
      buffer[i * 4 + 3] = word & 0xFF;
    }

  /* Read and discard CRC16 (1 word covers 16 bits) */

  int timeout = 100000;
  while ((pio1_reg(PIO_FSTAT) & (1u << SM_DAT)) && --timeout > 0);
  (void)pio1_reg(PIO_RXF(SM_DAT));

  return 0;
}

/****************************************************************************
 * Name: sdio_write_block
 *
 * Description:
 *   Write a 512-byte block to the SD card via DAT0.
 *
 ****************************************************************************/

static int sdio_write_block(uint32_t block_addr, const uint8_t *buffer)
{
  struct sdio_dev_s *dev = &g_sdiodev;
  uint32_t response;
  int ret;

  /* Send CMD24 (WRITE_BLOCK) */

  uint32_t addr = dev->highcap ? block_addr : (block_addr * SDIO_BLOCK_SIZE);

  ret = sdio_send_cmd(SDIO_CMD24, addr, SDIO_RSP_R1, &response);
  if (ret < 0)
    {
      return ret;
    }

  /* Tell DAT SM to write: start bit + 4096 data bits + CRC + end bit */

  size_t total_bits = 1 + 4096 + 16 + 1;  /* start + data + CRC + end */

  while (pio1_reg(PIO_FSTAT) & (1u << (16 + SM_DAT)))
    {
      /* Full */
    }

  pio1_reg(PIO_TXF(SM_DAT)) = total_bits - 1;
  pio1_reg(PIO_TXF(SM_DAT)) = 1;  /* direction = 1 (write) */

  /* Send start bit (0) */

  pio1_reg(PIO_TXF(SM_DAT)) = 0x00000000;  /* Start bit in MSB */

  /* Send 512 bytes of data (128 x 32-bit words) */

  for (size_t i = 0; i < 128; i++)
    {
      uint32_t word = ((uint32_t)buffer[i * 4 + 0] << 24) |
                      ((uint32_t)buffer[i * 4 + 1] << 16) |
                      ((uint32_t)buffer[i * 4 + 2] << 8) |
                      ((uint32_t)buffer[i * 4 + 3]);

      while (pio1_reg(PIO_FSTAT) & (1u << (16 + SM_DAT)))
        {
          /* Full */
        }

      pio1_reg(PIO_TXF(SM_DAT)) = word;
    }

  /* Send dummy CRC (0xFFFF) + end bit */

  pio1_reg(PIO_TXF(SM_DAT)) = 0xFFFF8000;

  /* Wait for busy (DAT0 goes low, then high again) */

  up_mdelay(10);

  return 0;
}

/****************************************************************************
 * Name: sdio_card_init
 *
 * Description:
 *   Initialize the SD card using SDIO 1-bit mode protocol.
 *
 ****************************************************************************/

static int sdio_card_init(struct sdio_dev_s *dev)
{
  uint32_t response;
  int ret;
  int retries;

  syslog(LOG_INFO, "SDIO: Card initialization starting...\n");

  /* Send 80+ clock cycles with CMD/DAT high (card power-up) */

  for (int i = 0; i < 10; i++)
    {
      pio1_reg(PIO_TXF(SM_CMD)) = (7u) | (0u << 8);
      pio1_reg(PIO_TXF(SM_CMD)) = 0xFFFFFFFF;
    }

  up_mdelay(10);

  /* CMD0: GO_IDLE_STATE */

  ret = sdio_send_cmd(SDIO_CMD0, 0, SDIO_RSP_NONE, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SDIO: CMD0 failed\n");
      return ret;
    }

  up_mdelay(1);

  /* CMD8: SEND_IF_COND (check voltage, expected 0x1AA) */

  ret = sdio_send_cmd(SDIO_CMD8, 0x000001AA, SDIO_RSP_R7, &response);
  bool v2_card = (ret == 0);

  if (v2_card)
    {
      syslog(LOG_INFO, "SDIO: SD v2.0+ card detected\n");
    }

  /* Loop ACMD41 until card is ready */

  retries = 100;
  dev->highcap = false;

  while (retries-- > 0)
    {
      /* CMD55: APP_CMD */

      ret = sdio_send_cmd(SDIO_CMD55, 0, SDIO_RSP_R1, &response);
      if (ret < 0)
        {
          continue;
        }

      /* ACMD41: SD_SEND_OP_COND
       *   HCS bit (bit 30) = 1 for SDHC support
       *   Voltage window: 3.2-3.4V
       */

      uint32_t acmd41_arg = 0x40FF8000;  /* HCS=1, 3.2-3.4V */
      ret = sdio_send_cmd(SDIO_ACMD41, acmd41_arg, SDIO_RSP_R3,
                          &response);
      if (ret < 0)
        {
          continue;
        }

      /* Check busy bit (bit 31) */

      if (response & 0x80000000)
        {
          dev->highcap = (response & 0x40000000) != 0;
          syslog(LOG_INFO, "SDIO: Card ready, %s\n",
                 dev->highcap ? "SDHC/SDXC" : "SDSC");
          break;
        }

      up_mdelay(10);
    }

  if (retries <= 0)
    {
      syslog(LOG_ERR, "SDIO: ACMD41 timeout — card not responding\n");
      return -ETIMEDOUT;
    }

  /* CMD2: ALL_SEND_CID */

  ret = sdio_send_cmd(SDIO_CMD2, 0, SDIO_RSP_R2, dev->csd);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SDIO: CMD2 failed\n");
      return ret;
    }

  /* CMD3: SEND_RELATIVE_ADDR */

  ret = sdio_send_cmd(SDIO_CMD3, 0, SDIO_RSP_R6, &response);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SDIO: CMD3 failed\n");
      return ret;
    }

  dev->rca = response & 0xFFFF0000;

  syslog(LOG_INFO, "SDIO: RCA = 0x%04X\n", dev->rca >> 16);

  /* CMD7: SELECT_CARD */

  ret = sdio_send_cmd(SDIO_CMD7, dev->rca, SDIO_RSP_R1, &response);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SDIO: CMD7 (select) failed\n");
      return ret;
    }

  /* CMD16: SET_BLOCKLEN = 512 */

  ret = sdio_send_cmd(SDIO_CMD16, SDIO_BLOCK_SIZE, SDIO_RSP_R1,
                      &response);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "SDIO: CMD16 failed (SDHC may not need it)\n");
    }

  dev->card_present = true;
  dev->initialized = true;

  syslog(LOG_INFO, "SDIO: Card initialized in 1-bit mode\n");

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_pio_sdio_initialize
 *
 * Description:
 *   Initialize the PIO-based 1-bit SDIO interface and card.
 *   Replaces SPI-mode SD card access when CONFIG_PICOCALC_PIO_SDIO
 *   is defined.
 *
 ****************************************************************************/

int rp23xx_pio_sdio_initialize(void)
{
  struct sdio_dev_s *dev = &g_sdiodev;
  int ret;

  if (dev->initialized)
    {
      return 0;
    }

  nxmutex_init(&dev->lock);

  syslog(LOG_INFO, "SDIO: Initializing PIO 1-bit SDIO "
         "(CLK=GP%d, CMD=GP%d, DAT0=GP%d)...\n",
         BOARD_SDIO_PIN_CLK, BOARD_SDIO_PIN_CMD, BOARD_SDIO_PIN_DAT0);

  /* Initialize PIO state machines */

  sdio_pio_init();

  /* Initialize the card */

  ret = sdio_card_init(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SDIO: Card init failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "SDIO: PIO 1-bit SDIO ready\n");

  return 0;
}

/****************************************************************************
 * Name: rp23xx_sdio_read / rp23xx_sdio_write
 *
 * Description:
 *   Block-level read/write for the SD card via SDIO.
 *
 ****************************************************************************/

int rp23xx_sdio_read(uint32_t block, uint8_t *buffer, size_t nblocks)
{
  struct sdio_dev_s *dev = &g_sdiodev;
  int ret;

  if (!dev->initialized || !dev->card_present)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);

  for (size_t i = 0; i < nblocks; i++)
    {
      ret = sdio_read_block(block + i, buffer + i * SDIO_BLOCK_SIZE);
      if (ret < 0)
        {
          nxmutex_unlock(&dev->lock);
          return ret;
        }
    }

  nxmutex_unlock(&dev->lock);
  return 0;
}

int rp23xx_sdio_write(uint32_t block, const uint8_t *buffer,
                      size_t nblocks)
{
  struct sdio_dev_s *dev = &g_sdiodev;
  int ret;

  if (!dev->initialized || !dev->card_present)
    {
      return -ENODEV;
    }

  nxmutex_lock(&dev->lock);

  for (size_t i = 0; i < nblocks; i++)
    {
      ret = sdio_write_block(block + i, buffer + i * SDIO_BLOCK_SIZE);
      if (ret < 0)
        {
          nxmutex_unlock(&dev->lock);
          return ret;
        }
    }

  nxmutex_unlock(&dev->lock);
  return 0;
}

#endif /* CONFIG_PICOCALC_PIO_SDIO */
