#!/usr/bin/env python3
"""Patch rp23xx_psram.c: convert from SPI to QSPI mode.

Run from WSL: python3 tools/patch_psram_qspi.py
"""

import os
import sys

# Support both absolute path and relative path from workspace root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WORKSPACE_ROOT = os.path.dirname(SCRIPT_DIR)
PSRAM_FILE = os.path.join(WORKSPACE_ROOT,
    'boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram.c')

with open(PSRAM_FILE, 'r') as f:
    content = f.read()

changes = 0

def do_replace(old, new, desc):
    global content, changes
    if old in content:
        content = content.replace(old, new, 1)
        changes += 1
        print(f"  [{changes}] {desc}")
    else:
        print(f"  SKIP (not found): {desc}")


# ---- 1. Replace PSRAM command defines ----
do_replace(
    '''/* PSRAM commands */

#define PSRAM_CMD_WRITE        0x02  /* SPI Write */
#define PSRAM_CMD_FAST_READ    0x0B  /* SPI Fast Read (+ dummy byte) */
#define PSRAM_CMD_RESET_EN     0x66  /* Reset Enable */
#define PSRAM_CMD_RESET        0x99  /* Reset */''',

    '''/* PSRAM commands (SPI mode - used during bitbang init) */

#define PSRAM_CMD_RESET_EN     0x66  /* Reset Enable */
#define PSRAM_CMD_RESET        0x99  /* Reset */
#define PSRAM_CMD_ENTER_QPI    0x35  /* Enter Quad I/O mode */

/* PSRAM commands (QPI mode - used after entering quad mode) */

#define PSRAM_QCMD_WRITE       0x02  /* QPI Write */
#define PSRAM_QCMD_FAST_READ   0xEB  /* QPI Fast Read (+ 6 dummy clocks) */''',
    "Command defines"
)


# ---- 2. Replace FIFO format comment ----
do_replace(
    '''/* PIO SPI byte-stream format (matching reference rp2040-psram):
 * Byte 0: number of bits to write (actual count, PIO pre-decrements)
 * Byte 1: number of bits to read
 * Byte 2..N: data to write (MSB first)
 *
 * Uses autopull=8 with byte-sized FIFO writes.  On RP2040/RP2350
 * the bus fabric replicates narrow writes across all byte lanes,
 * so a byte write of 0xAB \xe2\x86\x92 FIFO entry 0xABABABAB.  With shift-left
 * the PIO's `out x, 8` extracts bits [31:24] = 0xAB correctly.
 *
 * For PSRAM write:  cmd(8) + addr(24) + data(N)
 * For PSRAM read:   cmd(8) + addr(24) + dummy(8), then read N bits
 */''',

    '''/* PIO QSPI nibble-stream format:
 *
 * Byte 0: number of nibbles to write (PIO pre-decrements X)
 * Byte 1: number of nibbles to read
 * Byte 2..N: data bytes (each byte -> 2 nibbles, MSB nibble first)
 *
 * Uses autopull=8 / autopush=8 with byte-sized FIFO accesses.
 * On RP2040/RP2350, byte writes to the TX FIFO are replicated across
 * all 4 byte lanes by the bus fabric.  With shift-left and `out x, 8`,
 * the PIO extracts bits [31:24] which is the replicated byte.
 * For reads, `in pins, 4` accumulates in the ISR; autopush at 8 bits
 * places the byte in bits[7:0] of the RX FIFO entry.
 *
 * QPI Write:  cmd(2 nibbles) + addr(6) + data(2N)
 * QPI Read:   cmd(2) + addr(6) + dummy(6) -> then read 2N nibbles
 */''',
    "FIFO format comment"
)


# ---- 3. Replace PIO programs with QSPI program ----
# Find the start and end of the PIO program section
pio_start = content.find('/**' + '*' * 60 + '****\n * PIO Program: SPI PSRAM')
if pio_start == -1:
    pio_start = content.find(' * PIO Program: SPI PSRAM')
    if pio_start != -1:
        # Back up to the /*** line
        pio_start = content.rfind('/**', 0, pio_start)

pio_end = content.find("#define PSRAM_PIO_FUDGE_PROGRAM_LEN  10")
if pio_end != -1:
    pio_end = content.find('\n', pio_end) + 1  # Include the newline

if pio_start != -1 and pio_end != -1:
    old_pio = content[pio_start:pio_end]
    new_pio = '''/****************************************************************************
 * PIO Program: QSPI PSRAM
 *
 * Translated from psram_spi.pio (qspi_psram program) by Ian Scott.
 * Uses 2-bit sideset: bit0=CS, bit1=SCK.
 * Uses 4-bit out/in/set on SIO0-SIO3 for QSPI transfers.
 *
 * Protocol:
 *   1. Pull output nibble count (x) and input nibble count (y)
 *   2. Write 'x' nibbles (4 bits each) from TX FIFO via autopull
 *   3. If y>0, switch SIO0-3 to input, read 'y' nibbles
 *   4. Switch SIO0-3 back to output, deassert CS
 *
 * The program includes a built-in fudge cycle (extra clock between
 * write and read phases) for timing margin at high frequencies.
 *
 ****************************************************************************/

static const uint16_t g_qspi_program[] =
{
  /* begin: */
  0x6828,   /*  0: out x, 8          side 0b01  ; x = nibbles to write */
  0x6848,   /*  1: out y, 8          side 0b01  ; y = nibbles to read */
  0x0843,   /*  2: jmp x--, 3        side 0b01  ; pre-decrement */
  /* writeloop: */
  0x6004,   /*  3: out pins, 4       side 0b00  ; write nibble, CS low */
  0x1043,   /*  4: jmp x--, 3        side 0b10  ; SCK high, loop */
  0x0060,   /*  5: jmp !y, 0         side 0b00  ; no read? restart */
  0xf080,   /*  6: set pindirs, 0    side 0b10  ; SIO0-3->input + fudge */
  0xa042,   /*  7: nop               side 0b00  ; extra delay */
  /* readloop: */
  0x5004,   /*  8: in pins, 4        side 0b10  ; read nibble, SCK high */
  /* readloop_mid: */
  0x0088,   /*  9: jmp y--, 8        side 0b00  ; SCK low, loop */
  0xe88f,   /* 10: set pindirs, 0xF  side 0b01  ; SIO0-3->output, CS high */
};

#define QSPI_PROGRAM_LEN   11
'''
    content = content[:pio_start] + new_pio + content[pio_end:]
    changes += 1
    print(f"  [{changes}] PIO programs replaced with QSPI")
else:
    print(f"  SKIP: PIO programs section not found (start={pio_start}, end={pio_end})")


# ---- 4. Remove 'fudge' field from struct ----
do_replace(
    '  bool     fudge;              /* Using fudge factor program */\n  bool     initialized;',
    '  bool     initialized;',
    "Remove fudge field from struct"
)


# ---- 5. Replace pio_sm_configure with QSPI version ----
sm_start = content.find(' * Name: pio_sm_configure\n')
if sm_start != -1:
    sm_start = content.rfind('/**', 0, sm_start)

sm_end_marker = "  ctrl |= (1 << sm);\n  putreg32(ctrl, base + PIO_CTRL);\n}"
sm_end = content.find(sm_end_marker, sm_start if sm_start != -1 else 0)
if sm_end != -1:
    sm_end = sm_end + len(sm_end_marker)

if sm_start != -1 and sm_end != -1:
    new_sm = '''/****************************************************************************
 * Name: pio_sm_configure_qspi
 *
 * Description:
 *   Configure a PIO state machine for QSPI PSRAM.
 *   Sets up 4-bit out/in/set pin mapping, shift registers, clock divider,
 *   configures GPIO for PIO function, and enables SM.
 *
 ****************************************************************************/

static void pio_sm_configure_qspi(struct psram_dev_s *dev)
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

  /* PINCTRL for QSPI: 4-bit out/in/set on SIO0-SIO3
   *
   *   [4:0]   OUT_BASE     = SIO0 (GP2)
   *   [9:5]   SET_BASE     = SIO0 (GP2) -- for pindirs switching
   *   [14:10] SIDESET_BASE = CS (GP20)
   *   [19:15] IN_BASE      = SIO0 (GP2)
   *   [25:20] OUT_COUNT    = 4
   *   [28:26] SET_COUNT    = 4
   *   [31:29] SIDESET_COUNT = 2
   */

  uint32_t pinctrl =
    (2u << 29) |                               /* SIDESET_COUNT = 2 */
    (4u << 26) |                               /* SET_COUNT = 4 */
    (4u << 20) |                               /* OUT_COUNT = 4 */
    ((uint32_t)BOARD_PSRAM_PIN_SIO0 << 15) |   /* IN_BASE = SIO0 */
    ((uint32_t)BOARD_PSRAM_PIN_CS << 10) |     /* SIDESET_BASE = CS */
    ((uint32_t)BOARD_PSRAM_PIN_SIO0 << 5) |    /* SET_BASE = SIO0 */
    ((uint32_t)BOARD_PSRAM_PIN_SIO0 << 0);     /* OUT_BASE = SIO0 */

  putreg32(pinctrl, base + PIO_SM_PINCTRL(sm));

  /* EXECCTRL: wrap around entire QSPI program */

  uint32_t execctrl = getreg32(base + PIO_SM_EXECCTRL(sm));
  execctrl &= ~(0x1F << 7);   /* Clear wrap_bottom */
  execctrl &= ~(0x1F << 12);  /* Clear wrap_top */
  execctrl |= ((dev->prog_offset) << 7);                        /* wrap_bottom */
  execctrl |= ((dev->prog_offset + QSPI_PROGRAM_LEN - 1) << 12); /* wrap_top */
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

  /* SIO0-SIO3 (GP2-GP5): PIO function, initially output */

  for (int i = 0; i < 4; i++)
    {
      rp23xx_gpio_init(BOARD_PSRAM_PIN_SIO0 + i);
      rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SIO0 + i, true);
      rp23xx_gpio_set_function(BOARD_PSRAM_PIN_SIO0 + i, pio_func);
    }

  /* CS (GP20) and SCK (GP21): PIO function, output */

  rp23xx_gpio_init(BOARD_PSRAM_PIN_CS);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_CS, true);
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_CS, pio_func);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SCK);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SCK, true);
  rp23xx_gpio_set_function(BOARD_PSRAM_PIN_SCK, pio_func);

  /* Enable input synchronizer bypass for SIO0-SIO3 (faster reads) */

  uint32_t sync = getreg32(base + PIO_INPUT_SYNC_BYPASS);
  sync |= (0xFu << BOARD_PSRAM_PIN_SIO0);
  putreg32(sync, base + PIO_INPUT_SYNC_BYPASS);

  /* Set initial PC to program start */

  putreg32(dev->prog_offset, base + PIO_SM_INSTR(sm));

  /* Enable state machine */

  ctrl = getreg32(base + PIO_CTRL);
  ctrl |= (1 << sm);
  putreg32(ctrl, base + PIO_CTRL);
}'''
    content = content[:sm_start] + new_sm + content[sm_end:]
    changes += 1
    print(f"  [{changes}] pio_sm_configure replaced with QSPI version")
else:
    print(f"  SKIP: pio_sm_configure not found")


# ---- 6. Replace psram_pio_write_read with qspi_write_read ----
wr_start = content.find(' * Name: psram_pio_write_read\n')
if wr_start != -1:
    wr_start = content.rfind('/**', 0, wr_start)

wr_end_marker = "          read_data[i] = (uint8_t)(*rxfifo >> 24);\n        }\n    }\n}"
if wr_end_marker not in content:
    wr_end_marker = "          read_data[i] = (uint8_t)(*rxfifo & 0xFF);\n        }\n    }\n}"

wr_end = content.find(wr_end_marker, wr_start if wr_start != -1 else 0)
if wr_end != -1:
    wr_end = wr_end + len(wr_end_marker)

if wr_start != -1 and wr_end != -1:
    new_wr = '''/****************************************************************************
 * Name: qspi_write_read
 *
 * Description:
 *   Perform a PIO QSPI write-then-read transaction.
 *   Uses byte-sized FIFO accesses matching the reference rp2040-psram.
 *
 *   TX sequence:
 *     byte[0] = number of nibbles to write
 *     byte[1] = number of nibbles to read
 *     byte[2..N] = data bytes (each becomes 2 nibbles via out pins, 4)
 *
 ****************************************************************************/

static void qspi_write_read(struct psram_dev_s *dev,
                             const uint8_t *src, size_t src_len,
                             uint8_t *dst, size_t dst_len)
{
  uint32_t base = dev->pio_base;
  uint8_t sm = dev->sm;

  /* Use byte-sized FIFO pointers for correct bus fabric replication */

  volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));
  volatile uint8_t *rxfifo = (volatile uint8_t *)(base + PIO_RXF(sm));

  /* Feed all TX bytes (header + data) */

  size_t tx_remain = src_len;
  const uint8_t *sp = src;

  while (tx_remain > 0)
    {
      if (!(getreg32(base + PIO_FSTAT) & (1u << (16 + sm))))
        {
          *txfifo = *sp++;
          --tx_remain;
        }
    }

  /* Read RX bytes */

  size_t rx_remain = dst_len;

  while (rx_remain > 0)
    {
      if (!(getreg32(base + PIO_FSTAT) & (1u << sm)))
        {
          *dst++ = *rxfifo;
          --rx_remain;
        }
    }
}'''
    content = content[:wr_start] + new_wr + content[wr_end:]
    changes += 1
    print(f"  [{changes}] psram_pio_write_read replaced with qspi_write_read")
else:
    print(f"  SKIP: psram_pio_write_read not found (start={wr_start})")


# ---- 7. Replace psram_reset with bitbang SPI + enter QPI ----
rst_start = content.find(' * Name: psram_reset\n')
if rst_start != -1:
    rst_start = content.rfind('/**', 0, rst_start)

rst_end_marker = "  up_udelay(100);\n}"
rst_end = content.find(rst_end_marker, rst_start if rst_start != -1 else 0)
if rst_end != -1:
    rst_end = rst_end + len(rst_end_marker)

if rst_start != -1 and rst_end != -1:
    new_rst = '''/****************************************************************************
 * Name: spi_bitbang_byte
 *
 * Description:
 *   Send one byte via bitbang SPI (mode 0) on MOSI/SCK/CS pins.
 *   Used only during init before PIO QSPI is active.
 *
 ****************************************************************************/

static void spi_bitbang_byte(uint8_t byte)
{
  for (int i = 7; i >= 0; i--)
    {
      rp23xx_gpio_put(BOARD_PSRAM_PIN_SIO0,
                      (byte >> i) & 1);          /* MOSI = data bit */
      rp23xx_gpio_put(BOARD_PSRAM_PIN_SCK, 0);   /* SCK low (setup) */
      up_udelay(1);
      rp23xx_gpio_put(BOARD_PSRAM_PIN_SCK, 1);   /* SCK high (latch) */
      up_udelay(1);
    }

  rp23xx_gpio_put(BOARD_PSRAM_PIN_SCK, 0);       /* SCK low at end */
}

/****************************************************************************
 * Name: spi_bitbang_cmd
 *
 * Description:
 *   Send a single-byte SPI command to PSRAM with CS framing.
 *
 ****************************************************************************/

static void spi_bitbang_cmd(uint8_t cmd)
{
  rp23xx_gpio_put(BOARD_PSRAM_PIN_CS, 0);  /* CS assert */
  up_udelay(1);

  spi_bitbang_byte(cmd);

  rp23xx_gpio_put(BOARD_PSRAM_PIN_CS, 1);  /* CS deassert */
  up_udelay(1);
}

/****************************************************************************
 * Name: psram_spi_init_pins
 *
 * Description:
 *   Configure GPIO pins for bitbang SPI (used during PSRAM reset
 *   and Enter QPI command, before PIO takes over).
 *
 ****************************************************************************/

static void psram_spi_init_pins(void)
{
  rp23xx_gpio_init(BOARD_PSRAM_PIN_CS);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_CS, true);
  rp23xx_gpio_put(BOARD_PSRAM_PIN_CS, 1);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SCK);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SCK, true);
  rp23xx_gpio_put(BOARD_PSRAM_PIN_SCK, 0);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SIO0);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SIO0, true);
  rp23xx_gpio_put(BOARD_PSRAM_PIN_SIO0, 0);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SIO1);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SIO1, false);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SIO2);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SIO2, false);

  rp23xx_gpio_init(BOARD_PSRAM_PIN_SIO3);
  rp23xx_gpio_setdir(BOARD_PSRAM_PIN_SIO3, false);
}

/****************************************************************************
 * Name: psram_reset_and_enter_qpi
 *
 * Description:
 *   Reset PSRAM chip via bitbang SPI, then enter QPI (quad I/O) mode.
 *   After this, all subsequent commands must use 4-bit QSPI.
 *
 ****************************************************************************/

static void psram_reset_and_enter_qpi(void)
{
  psram_spi_init_pins();

  spi_bitbang_cmd(PSRAM_CMD_RESET_EN);
  up_udelay(50);

  spi_bitbang_cmd(PSRAM_CMD_RESET);
  up_udelay(200);

  spi_bitbang_cmd(PSRAM_CMD_ENTER_QPI);
  up_udelay(50);
}'''
    content = content[:rst_start] + new_rst + content[rst_end:]
    changes += 1
    print(f"  [{changes}] psram_reset replaced with bitbang SPI + enter QPI")
else:
    print(f"  SKIP: psram_reset not found")


# ---- 8. Replace psram_write8 ----
do_replace(
    '''static void psram_write8(struct psram_dev_s *dev, uint32_t addr,
                         uint8_t value)
{
  uint8_t cmd[5];

  cmd[0] = PSRAM_CMD_WRITE;
  cmd[1] = (addr >> 16) & 0xFF;
  cmd[2] = (addr >> 8) & 0xFF;
  cmd[3] = addr & 0xFF;
  cmd[4] = value;

  psram_pio_write_read(dev, cmd, 40, NULL, 0);
}''',

    '''static void psram_write8(struct psram_dev_s *dev, uint32_t addr,
                         uint8_t value)
{
  uint8_t buf[7];

  buf[0] = 10;                     /* 10 nibbles to write */
  buf[1] = 0;                      /* 0 nibbles to read */
  buf[2] = PSRAM_QCMD_WRITE;       /* 0x02 */
  buf[3] = (addr >> 16) & 0xFF;
  buf[4] = (addr >> 8) & 0xFF;
  buf[5] = addr & 0xFF;
  buf[6] = value;

  qspi_write_read(dev, buf, 7, NULL, 0);
}''',
    "psram_write8 for QSPI"
)


# ---- 9. Replace psram_read8 ----
do_replace(
    '''static uint8_t psram_read8(struct psram_dev_s *dev, uint32_t addr)
{
  uint8_t cmd[5];
  uint8_t result;

  cmd[0] = PSRAM_CMD_FAST_READ;
  cmd[1] = (addr >> 16) & 0xFF;
  cmd[2] = (addr >> 8) & 0xFF;
  cmd[3] = addr & 0xFF;
  cmd[4] = 0x00;  /* Dummy byte */

  psram_pio_write_read(dev, cmd, 40, &result, 8);

  return result;
}''',

    '''static uint8_t psram_read8(struct psram_dev_s *dev, uint32_t addr)
{
  uint8_t buf[9];
  uint8_t result;

  buf[0] = 14;                      /* 14 nibbles to write */
  buf[1] = 2;                       /* 2 nibbles to read (1 byte) */
  buf[2] = PSRAM_QCMD_FAST_READ;    /* 0xEB */
  buf[3] = (addr >> 16) & 0xFF;
  buf[4] = (addr >> 8) & 0xFF;
  buf[5] = addr & 0xFF;
  buf[6] = 0x00;                    /* dummy byte 1 */
  buf[7] = 0x00;                    /* dummy byte 2 */
  buf[8] = 0x00;                    /* dummy byte 3 */

  qspi_write_read(dev, buf, 9, &result, 1);

  return result;
}''',
    "psram_read8 for QSPI"
)


# ---- 10. Replace psram_write_bulk ----
# Find and replace entire function
wb_start = content.find(' * Name: psram_write_bulk\n')
if wb_start != -1:
    wb_start = content.rfind('/**', 0, wb_start)

# Find end: the closing brace of the outer while loop
wb_end_search = content.find('      len  -= chunk;\n    }\n}', wb_start if wb_start != -1 else 0)
if wb_end_search != -1:
    wb_end = wb_end_search + len('      len  -= chunk;\n    }\n}')

if wb_start != -1 and wb_end_search != -1:
    new_wb = '''/****************************************************************************
 * Name: psram_write_bulk
 *
 * Description:
 *   Write a block of bytes to PSRAM via QPI starting at 'addr'.
 *   QSPI PIO uses 8-bit nibble count, max 255 nibbles per transaction.
 *
 ****************************************************************************/

#define QSPI_MAX_WRITE_DATA  123  /* Max data bytes per write transaction */
#define QSPI_MAX_READ_DATA   120  /* Max data bytes per read transaction */

static void psram_write_bulk(struct psram_dev_s *dev, uint32_t addr,
                             const uint8_t *data, size_t len)
{
  while (len > 0)
    {
      size_t page_remain = 1024 - (addr & 0x3FF);
      size_t chunk = (len < page_remain) ? len : page_remain;
      if (chunk > QSPI_MAX_WRITE_DATA)
        {
          chunk = QSPI_MAX_WRITE_DATA;
        }

      uint8_t write_nibbles = 8 + chunk * 2;

      uint32_t base = dev->pio_base;
      uint8_t sm = dev->sm;
      volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = write_nibbles;

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = 0;

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = PSRAM_QCMD_WRITE;

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = (addr >> 16) & 0xFF;

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = (addr >> 8) & 0xFF;

      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = addr & 0xFF;

      for (size_t i = 0; i < chunk; i++)
        {
          while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
          *txfifo = data[i];
        }

      addr += chunk;
      data += chunk;
      len  -= chunk;
    }
}'''
    content = content[:wb_start] + new_wb + content[wb_end:]
    changes += 1
    print(f"  [{changes}] psram_write_bulk replaced for QSPI")
else:
    print(f"  SKIP: psram_write_bulk not found")


# ---- 11. Replace psram_read_bulk ----
rb_start = content.find(' * Name: psram_read_bulk\n')
if rb_start != -1:
    rb_start = content.rfind('/**', 0, rb_start)

# Find: psram_pio_write_read(dev, header, 40, data, len * 8);
rb_end_marker = "  psram_pio_write_read(dev, header, 40, data, len * 8);\n}"
rb_end = content.find(rb_end_marker, rb_start if rb_start != -1 else 0)
if rb_end != -1:
    rb_end = rb_end + len(rb_end_marker)

if rb_start != -1 and rb_end != -1:
    new_rb = '''/****************************************************************************
 * Name: psram_read_bulk
 *
 * Description:
 *   Read a block of bytes from PSRAM via QPI starting at 'addr'.
 *   QSPI PIO uses 8-bit nibble count, read max 120 bytes per transaction.
 *
 ****************************************************************************/

static void psram_read_bulk(struct psram_dev_s *dev, uint32_t addr,
                            uint8_t *data, size_t len)
{
  while (len > 0)
    {
      size_t chunk = (len < QSPI_MAX_READ_DATA) ? len : QSPI_MAX_READ_DATA;

      uint8_t hdr[9];

      hdr[0] = 14;                      /* write nibbles */
      hdr[1] = chunk * 2;               /* read nibbles */
      hdr[2] = PSRAM_QCMD_FAST_READ;    /* 0xEB */
      hdr[3] = (addr >> 16) & 0xFF;
      hdr[4] = (addr >> 8) & 0xFF;
      hdr[5] = addr & 0xFF;
      hdr[6] = 0x00;
      hdr[7] = 0x00;
      hdr[8] = 0x00;

      qspi_write_read(dev, hdr, 9, data, chunk);

      addr += chunk;
      data += chunk;
      len  -= chunk;
    }
}'''
    content = content[:rb_start] + new_rb + content[rb_end:]
    changes += 1
    print(f"  [{changes}] psram_read_bulk replaced for QSPI")
else:
    print(f"  SKIP: psram_read_bulk not found")


# ---- 12. Replace rp23xx_psram_init ----
init_start = content.find('int rp23xx_psram_init(void)\n{')
if init_start == -1:
    init_start = content.find('int rp23xx_psram_init(void)')

# Find end: the "return 0;\n}" at end of init function
# Look for the last syslog about "ready" then the return
init_search_start = init_start if init_start != -1 else 0
init_end_marker = '         BOARD_PSRAM_USE_FUDGE ? "yes" : "no");\n\n  return 0;\n}'
init_end = content.find(init_end_marker, init_search_start)
if init_end != -1:
    init_end = init_end + len(init_end_marker)

if init_start != -1 and init_end != -1:
    new_init = '''int rp23xx_psram_init(void)
{
  struct psram_dev_s *dev = &g_psramdev;

  if (dev->initialized)
    {
      return 0;
    }

  syslog(LOG_INFO, "psram: initializing QSPI interface on PIO%d...\\n",
         BOARD_PSRAM_PIO_INST);

  nxmutex_init(&dev->lock);

  dev->pio_base = PIO_BASE;
  dev->sm = BOARD_PSRAM_PIO_SM;
  dev->dma_write = DMA_CH_WRITE;
  dev->dma_read = DMA_CH_READ;
  dev->dma_async = DMA_CH_ASYNC;

  /* Phase 1: Bitbang SPI to reset PSRAM and enter QPI mode */

  psram_reset_and_enter_qpi();

  syslog(LOG_INFO, "psram: reset + enter QPI done, "
         "configuring PIO QSPI...\\n");

  /* Phase 2: Load QSPI PIO program and configure state machine */

  pio_load_program(dev->pio_base, g_qspi_program, QSPI_PROGRAM_LEN,
                   &dev->prog_offset);

  syslog(LOG_DEBUG, "psram: QSPI PIO program loaded at offset %d "
         "(%d instructions)\\n",
         dev->prog_offset, QSPI_PROGRAM_LEN);

  pio_sm_configure_qspi(dev);

  dma_configure(dev);

  syslog(LOG_INFO, "psram: GPIO CS=GP%d SCK=GP%d "
         "SIO0-3=GP%d-%d (QSPI)\\n",
         BOARD_PSRAM_PIN_CS, BOARD_PSRAM_PIN_SCK,
         BOARD_PSRAM_PIN_SIO0, BOARD_PSRAM_PIN_SIO3);

  /* Phase 3: Verify PSRAM is accessible via QSPI */

  if (psram_test(dev) != 0)
    {
      syslog(LOG_ERR, "psram: QSPI probe failed\\n");
      return -EIO;
    }

  memset(g_psram_blocks, 0, sizeof(g_psram_blocks));
  g_psram_next_addr = 0;
  g_psram_block_count = 0;

  dev->initialized = true;

  syslog(LOG_INFO, "psram: APS6404L %d KB QSPI ready "
         "(PIO%d SM%d, clkdiv=%.1f)\\n",
         BOARD_PSRAM_SIZE / 1024,
         BOARD_PSRAM_PIO_INST, BOARD_PSRAM_PIO_SM,
         (double)BOARD_PSRAM_CLKDIV);

  return 0;
}'''
    content = content[:init_start] + new_init + content[init_end:]
    changes += 1
    print(f"  [{changes}] rp23xx_psram_init replaced for QSPI")
else:
    print(f"  SKIP: rp23xx_psram_init not found (start={init_start}, end={init_end})")
    # Debug: show what we found
    if init_start != -1:
        print(f"    init starts at offset {init_start}")
        print(f"    Looking for end marker...")
        # Try to find the end differently
        idx = content.find('return 0;\n}', init_start)
        if idx != -1:
            print(f"    Found 'return 0;' at {idx}")


# ---- 13. Update psram_test comment ----
do_replace(
    'psram: initialization failed',
    'psram: QSPI probe failed',
    "psram_test error message (cosmetic)"
)


# ---- Write result ----
print(f"\nTotal changes: {changes}")

with open(PSRAM_FILE, 'w') as f:
    f.write(content)

print(f"Written {len(content)} bytes to {PSRAM_FILE}")
