/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_psram_xip.c
 *
 * XIP-mapped PSRAM driver for RP2350B-Plus-W (8 MB).
 *
 * The PSRAM (APS6404L or compatible) is located on the Waveshare
 * RP2350B-Plus-W module and connected via the RP2350B's dedicated
 * QSPI1 controller. The hardware maps it into the XIP address space
 * at 0x11000000 - 0x117FFFFF, allowing direct pointer access.
 *
 * This driver provides:
 *   - Initialization and validation of XIP PSRAM
 *   - A block allocator (psram_malloc/free/realloc) for managed use
 *   - Direct memory access (memcpy/memset) — no PIO or DMA needed
 *
 * Migration from PIO driver: The previous rp23xx_psram.c used PIO
 * bit-banging over GP2-5/GP20-21. With PSRAM on the module's QSPI1,
 * those pins are freed and access is orders of magnitude faster.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/mutex.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PSRAM_BLOCK_MAGIC  0x50535200  /* "PSR\0" */
#define PSRAM_MAX_BLOCKS   512
#define PSRAM_ALIGN        4          /* Minimum allocation alignment */

/* Test patterns for XIP PSRAM validation */

#define TEST_PATTERN_A     0xDEADBEEF
#define TEST_PATTERN_B     0xCAFEBABE
#define TEST_PATTERN_C     0x55AA55AA

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct psram_block_s
{
  uint32_t offset;     /* Offset from PSRAM base */
  uint32_t size;       /* Block size in bytes */
  bool     used;       /* In use flag */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct psram_block_s g_psram_blocks[PSRAM_MAX_BLOCKS];
static uint32_t g_psram_next_offset = 0;
static int      g_psram_block_count = 0;
static bool     g_psram_initialized = false;
static mutex_t  g_psram_lock;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: psram_test_xip
 *
 * Description:
 *   Validate XIP PSRAM by writing and reading test patterns at several
 *   addresses. Tests beginning, middle, and end of the 8 MB range.
 *
 ****************************************************************************/

static int psram_test_xip(void)
{
  volatile uint32_t *base = (volatile uint32_t *)BOARD_PSRAM_XIP_BASE;
  volatile uint32_t *mid  = (volatile uint32_t *)
                            (BOARD_PSRAM_XIP_BASE + BOARD_PSRAM_SIZE / 2);
  volatile uint32_t *end  = (volatile uint32_t *)
                            (BOARD_PSRAM_XIP_BASE + BOARD_PSRAM_SIZE -
                             sizeof(uint32_t));

  /* Write test patterns */

  *base = TEST_PATTERN_A;
  *mid  = TEST_PATTERN_B;
  *end  = TEST_PATTERN_C;

  /* Read back and verify */

  if (*base != TEST_PATTERN_A)
    {
      syslog(LOG_ERR, "psram: XIP test FAIL at base (wrote 0x%08X, "
             "read 0x%08X)\n", TEST_PATTERN_A, *base);
      return -EIO;
    }

  if (*mid != TEST_PATTERN_B)
    {
      syslog(LOG_ERR, "psram: XIP test FAIL at mid (wrote 0x%08X, "
             "read 0x%08X)\n", TEST_PATTERN_B, *mid);
      return -EIO;
    }

  if (*end != TEST_PATTERN_C)
    {
      syslog(LOG_ERR, "psram: XIP test FAIL at end (wrote 0x%08X, "
             "read 0x%08X)\n", TEST_PATTERN_C, *end);
      return -EIO;
    }

  /* Walking bit test at base — catches stuck address/data lines */

  for (int i = 0; i < 32; i++)
    {
      uint32_t pattern = 1u << i;
      *base = pattern;
      if (*base != pattern)
        {
          syslog(LOG_ERR, "psram: walking bit test FAIL at bit %d\n", i);
          return -EIO;
        }
    }

  /* Restore base to zero */

  *base = 0;

  syslog(LOG_INFO, "psram: XIP validation passed "
         "(base=0x%08X, size=%d KB)\n",
         BOARD_PSRAM_XIP_BASE, BOARD_PSRAM_SIZE / 1024);

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_psram_init
 *
 * Description:
 *   Initialize XIP-mapped PSRAM. The RP2350B bootrom / second-stage
 *   bootloader configures QSPI1 for PSRAM during early boot, so by
 *   the time NuttX starts, the PSRAM is already accessible at
 *   BOARD_PSRAM_XIP_BASE. We just validate and set up the allocator.
 *
 ****************************************************************************/

int rp23xx_psram_init(void)
{
  if (g_psram_initialized)
    {
      return 0;
    }

  syslog(LOG_INFO, "psram: initializing XIP PSRAM at 0x%08X...\n",
         BOARD_PSRAM_XIP_BASE);

  nxmutex_init(&g_psram_lock);

  /* Validate PSRAM is accessible via XIP */

  int ret = psram_test_xip();
  if (ret < 0)
    {
      syslog(LOG_ERR, "psram: XIP PSRAM not responding at 0x%08X\n",
             BOARD_PSRAM_XIP_BASE);
      return ret;
    }

  /* Initialize block allocator */

  memset(g_psram_blocks, 0, sizeof(g_psram_blocks));
  g_psram_next_offset = 0;
  g_psram_block_count = 0;

  g_psram_initialized = true;

  syslog(LOG_INFO, "psram: %d KB XIP PSRAM ready at 0x%08X-0x%08X\n",
         BOARD_PSRAM_SIZE / 1024,
         BOARD_PSRAM_XIP_BASE,
         BOARD_PSRAM_XIP_END - 1);

  return 0;
}

/****************************************************************************
 * Name: psram_malloc
 *
 * Description:
 *   Allocate a block from the PSRAM heap. Returns a direct pointer
 *   into the XIP PSRAM address space — can be dereferenced directly.
 *
 ****************************************************************************/

void *psram_malloc(size_t size)
{
  if (!g_psram_initialized || size == 0)
    {
      return NULL;
    }

  nxmutex_lock(&g_psram_lock);

  /* Align to PSRAM_ALIGN bytes */

  size = (size + PSRAM_ALIGN - 1) & ~(PSRAM_ALIGN - 1);

  /* First-fit in free list */

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (!g_psram_blocks[i].used && g_psram_blocks[i].size >= size)
        {
          g_psram_blocks[i].used = true;

          /* Split if block is significantly larger */

          if (g_psram_blocks[i].size > size + 64 &&
              g_psram_block_count < PSRAM_MAX_BLOCKS)
            {
              int j = g_psram_block_count++;
              g_psram_blocks[j].offset = g_psram_blocks[i].offset + size;
              g_psram_blocks[j].size   = g_psram_blocks[i].size - size;
              g_psram_blocks[j].used   = false;
              g_psram_blocks[i].size   = size;
            }

          nxmutex_unlock(&g_psram_lock);
          return (void *)(uintptr_t)(BOARD_PSRAM_XIP_BASE +
                                     g_psram_blocks[i].offset);
        }
    }

  /* Bump allocate from end */

  if (g_psram_next_offset + size > BOARD_PSRAM_SIZE)
    {
      nxmutex_unlock(&g_psram_lock);
      syslog(LOG_ERR, "psram: OOM (requested %zu, used %lu/%d)\n",
             size, (unsigned long)g_psram_next_offset, BOARD_PSRAM_SIZE);
      return NULL;
    }

  if (g_psram_block_count >= PSRAM_MAX_BLOCKS)
    {
      nxmutex_unlock(&g_psram_lock);
      syslog(LOG_ERR, "psram: block table full (%d)\n", PSRAM_MAX_BLOCKS);
      return NULL;
    }

  int idx = g_psram_block_count++;
  g_psram_blocks[idx].offset = g_psram_next_offset;
  g_psram_blocks[idx].size   = size;
  g_psram_blocks[idx].used   = true;

  g_psram_next_offset += size;

  nxmutex_unlock(&g_psram_lock);

  return (void *)(uintptr_t)(BOARD_PSRAM_XIP_BASE +
                             g_psram_blocks[idx].offset);
}

/****************************************************************************
 * Name: psram_free
 *
 * Description:
 *   Free a previously allocated PSRAM block. Coalesces adjacent free
 *   blocks to reduce fragmentation.
 *
 ****************************************************************************/

void psram_free(void *ptr)
{
  if (!g_psram_initialized || ptr == NULL)
    {
      return;
    }

  uint32_t offset = (uint32_t)(uintptr_t)ptr - BOARD_PSRAM_XIP_BASE;

  /* Bounds check */

  if (offset >= BOARD_PSRAM_SIZE)
    {
      syslog(LOG_WARNING, "psram: free of invalid pointer %p\n", ptr);
      return;
    }

  nxmutex_lock(&g_psram_lock);

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (g_psram_blocks[i].offset == offset && g_psram_blocks[i].used)
        {
          g_psram_blocks[i].used = false;

          /* Coalesce adjacent free blocks */

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
                      /* j immediately after i? */

                      if (g_psram_blocks[j].offset ==
                          g_psram_blocks[i].offset +
                          g_psram_blocks[i].size)
                        {
                          g_psram_blocks[i].size +=
                            g_psram_blocks[j].size;

                          for (int k = j;
                               k < g_psram_block_count - 1; k++)
                            {
                              g_psram_blocks[k] = g_psram_blocks[k + 1];
                            }

                          g_psram_block_count--;
                          if (j < i) i--;
                          merged = true;
                          break;
                        }

                      /* j immediately before i? */

                      if (g_psram_blocks[i].offset ==
                          g_psram_blocks[j].offset +
                          g_psram_blocks[j].size)
                        {
                          g_psram_blocks[j].size +=
                            g_psram_blocks[i].size;

                          for (int k = i;
                               k < g_psram_block_count - 1; k++)
                            {
                              g_psram_blocks[k] = g_psram_blocks[k + 1];
                            }

                          g_psram_block_count--;
                          i = j;
                          merged = true;
                          break;
                        }
                    }
                }
            }
          while (merged);

          break;
        }
    }

  nxmutex_unlock(&g_psram_lock);
}

/****************************************************************************
 * Name: psram_realloc
 *
 * Description:
 *   Resize a PSRAM allocation. Since PSRAM is directly addressable,
 *   we can use memcpy for data transfer.
 *
 ****************************************************************************/

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

  uint32_t old_offset = (uint32_t)(uintptr_t)ptr - BOARD_PSRAM_XIP_BASE;

  nxmutex_lock(&g_psram_lock);

  for (int i = 0; i < g_psram_block_count; i++)
    {
      if (g_psram_blocks[i].offset == old_offset &&
          g_psram_blocks[i].used)
        {
          size_t old_size = g_psram_blocks[i].size;

          /* Already large enough? */

          size = (size + PSRAM_ALIGN - 1) & ~(PSRAM_ALIGN - 1);
          if (old_size >= size)
            {
              nxmutex_unlock(&g_psram_lock);
              return ptr;
            }

          nxmutex_unlock(&g_psram_lock);

          /* Allocate new, copy, free old */

          void *new_ptr = psram_malloc(size);
          if (new_ptr == NULL)
            {
              return NULL;
            }

          memcpy(new_ptr, ptr, old_size);
          psram_free(ptr);
          return new_ptr;
        }
    }

  nxmutex_unlock(&g_psram_lock);
  return NULL;
}

/****************************************************************************
 * Name: psram_available
 *
 * Description:
 *   Return total free PSRAM bytes (free blocks + unallocated space).
 *
 ****************************************************************************/

size_t psram_available(void)
{
  if (!g_psram_initialized)
    {
      return 0;
    }

  size_t free_space = BOARD_PSRAM_SIZE - g_psram_next_offset;

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
 * Name: psram_memcpy_to / psram_memcpy_from / psram_memcpy / psram_memset
 *
 * Description:
 *   Memory operations on PSRAM. With XIP mapping, these are just thin
 *   wrappers around standard memcpy/memset operating on the XIP address.
 *
 *   Kept for API compatibility with code written for the PIO driver.
 *
 ****************************************************************************/

void psram_memcpy_to(void *psram_ptr, const void *src, size_t len)
{
  memcpy(psram_ptr, src, len);
}

void psram_memcpy_from(void *dst, const void *psram_ptr, size_t len)
{
  memcpy(dst, psram_ptr, len);
}

void psram_memcpy(void *dst, const void *src, size_t len)
{
  memcpy(dst, src, len);
}

void psram_memset(void *psram_ptr, int value, size_t len)
{
  memset(psram_ptr, value, len);
}
