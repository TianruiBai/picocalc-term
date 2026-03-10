import sys

with open("boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram.c", "r") as f:
    text = f.read()

old_func1 = """static void psram_pio_write_read(struct psram_dev_s *dev,
                                 const uint8_t *write_data,
                                 size_t write_bits,
                                 uint8_t *read_data,
                                 size_t read_bits)
{
  uint32_t base = dev->pio_base;
  uint8_t sm = dev->sm;

  /* Byte-sized pointer to TX FIFO (bus fabric replicates to all lanes) */

  volatile uint8_t *txfifo =
    (volatile uint8_t *)(base + PIO_TXF(sm));

  /* Byte-sized pointer to RX FIFO */

  volatile uint8_t *rxfifo =
    (volatile uint8_t *)(base + PIO_RXF(sm));

  /* Send header: write_bits, then read_bits (no -1; PIO pre-decrements) */

  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
  *txfifo = (uint8_t)(write_bits & 0xFF);

  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
  *txfifo = (uint8_t)(read_bits & 0xFF);

  /* Feed data bytes to TX FIFO */

  size_t write_bytes = (write_bits + 7) / 8;

  for (size_t i = 0; i < write_bytes; i++)
    {
      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = write_data[i];
    }

  /* Read data from RX FIFO (byte-sized reads) */

  if (read_bits > 0)
    {
      size_t read_bytes = (read_bits + 7) / 8;

      for (size_t i = 0; i < read_bytes; i++)
        {
          while (getreg32(base + PIO_FSTAT) & (1u << (8 + sm)));
          read_data[i] = *rxfifo;
        }
    }
}"""

new_func1 = """static void psram_pio_write_read(struct psram_dev_s *dev,
                                 const uint8_t *write_data,
                                 size_t write_bits,
                                 uint8_t *read_data,
                                 size_t read_bits)
{
  uint32_t base = dev->pio_base;
  uint8_t sm = dev->sm;

  volatile uint32_t *txfifo = (volatile uint32_t *)(base + PIO_TXF(sm));
  volatile uint32_t *rxfifo = (volatile uint32_t *)(base + PIO_RXF(sm));

  while ((getreg32(base + PIO_FSTAT) & (1u << (8 + sm))) == 0)
    {
      (void)*rxfifo;
    }

  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
  *txfifo = (uint32_t)write_bits;

  while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
  *txfifo = (uint32_t)read_bits;

  size_t write_bytes = (write_bits + 7) / 8;

  for (size_t i = 0; i < write_bytes; i++)
    {
      while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
      *txfifo = ((uint32_t)write_data[i]) << 24;
    }

  if (read_bits > 0)
    {
      size_t read_bytes = (read_bits + 7) / 8;

      for (size_t i = 0; i < read_bytes; i++)
        {
          while (getreg32(base + PIO_FSTAT) & (1u << (8 + sm)));
          read_data[i] = (uint8_t)(*rxfifo & 0xFF);
        }
    }
}"""

if old_func1 in text:
    text = text.replace(old_func1, new_func1)
    print("Patched psram_pio_write_read")
else:
    print("Could not find psram_pio_write_read")

old_bulk_w = """        uint32_t base = dev->pio_base;
        uint8_t sm = dev->sm;
        uint32_t total_write_bits = (4 + chunk) * 8;

        volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = (uint8_t)(total_write_bits & 0xFF);

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = 0; /* 0 read bits */

        /* Header (command + target addr) */

        uint8_t header[4];
        header[0] = PSRAM_CMD_WRITE;
        header[1] = (addr >> 16) & 0xFF;
        header[2] = (addr >> 8) & 0xFF;
        header[3] = addr & 0xFF;

        for (size_t i = 0; i < 4; i++)
          {
            while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
            *txfifo = header[i];
          }

        /* Bulk Data payload */

        for (size_t i = 0; i < chunk; i++)
          {
            while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
            *txfifo = data[i];
          }"""

new_bulk_w = """        uint32_t base = dev->pio_base;
        uint8_t sm = dev->sm;
        uint32_t total_write_bits = (4 + chunk) * 8;

        volatile uint32_t *txfifo = (volatile uint32_t *)(base + PIO_TXF(sm));

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = total_write_bits;

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = 0; /* 0 read bits */

        /* Header (command + target addr) */

        uint8_t header[4];
        header[0] = PSRAM_CMD_WRITE;
        header[1] = (addr >> 16) & 0xFF;
        header[2] = (addr >> 8) & 0xFF;
        header[3] = addr & 0xFF;

        for (size_t i = 0; i < 4; i++)
          {
            while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
            *txfifo = ((uint32_t)header[i]) << 24;
          }

        /* Bulk Data payload */

        for (size_t i = 0; i < chunk; i++)
          {
            while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
            *txfifo = ((uint32_t)data[i]) << 24;
          }"""

if old_bulk_w in text:
    text = text.replace(old_bulk_w, new_bulk_w)
    print("Patched psram_write_bulk")
else:
    print("Could not find psram_write_bulk")


old_bulk_r = """        uint32_t base = dev->pio_base;
        uint8_t sm = dev->sm;
        uint32_t total_write_bits = 4 * 8;
        uint32_t total_read_bits = chunk * 8;

        volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));
        volatile uint8_t *rxfifo = (volatile uint8_t *)(base + PIO_RXF(sm));

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = (uint8_t)(total_write_bits & 0xFF);

        while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
        *txfifo = (uint8_t)(total_read_bits & 0xFF);

        /* Header (command + target addr + dummy) */

        uint8_t header[4];
        header[0] = PSRAM_CMD_FAST_READ;
        header[1] = (addr >> 16) & 0xFF;
        header[2] = (addr >> 8) & 0xFF;
        header[3] = addr & 0xFF;

        for (size_t i = 0; i < 4; i++)
          {
            while (getreg32(base + PIO_FSTAT) & (1u << (16 + sm)));
            *txfifo = header[i];
          }

        /* Wait for read data (FAST_READ reads starting directly)
         * BUT wait! FAST_READ has a 1-byte dummy!
         * We used total_write_bits = 4 bytes:
         * CMD, A2, A1, A0. Wait, standard FAST_READ has 1 dummy byte AFTER addr.
         * The standard psram_read8 command is 5 bytes long! (40 bits).
         */

        /* Actually the previous code had a bug if read8 sends 5 bytes,
         * let's just make sure we replace what was there exactly. */"""

# Let's dynamically replace the txfifo accesses
text = text.replace("volatile uint8_t *txfifo = (volatile uint8_t *)(base + PIO_TXF(sm));", "volatile uint32_t *txfifo = (volatile uint32_t *)(base + PIO_TXF(sm));")
text = text.replace("volatile uint8_t *rxfifo = (volatile uint8_t *)(base + PIO_RXF(sm));", "volatile uint32_t *rxfifo = (volatile uint32_t *)(base + PIO_RXF(sm));")

text = text.replace("*txfifo = (uint8_t)(total_write_bits & 0xFF);", "*txfifo = total_write_bits;")
text = text.replace("*txfifo = (uint8_t)(total_read_bits & 0xFF);", "*txfifo = total_read_bits;")
text = text.replace("*txfifo = header[i];", "*txfifo = ((uint32_t)header[i]) << 24;")
text = text.replace("data[i] = *rxfifo;", "data[i] = (uint8_t)(*rxfifo & 0xFF);")

with open("boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram.c", "w") as f:
    f.write(text)

print("Done")
