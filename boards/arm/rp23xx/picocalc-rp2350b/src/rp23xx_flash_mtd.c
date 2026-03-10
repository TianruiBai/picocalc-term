/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_flash_mtd.c
 *
 * Flash MTD driver for RP2350B — read/write access to the internal QSPI
 * flash region beyond the firmware binary image.
 *
 * The RP2350's ROM provides flash erase/program primitives that work
 * with interrupts disabled.  Since PicoCalc-Term runs entirely from
 * RAM (the linker places all .text/.rodata in SRAM), XIP is only used
 * for filesystem reads and the interrupt-off window is short.
 *
 * The filesystem partition starts at the first 4 KB-aligned address
 * after __flash_binary_end and extends to the end of the physical
 * flash chip (CONFIG_RP23XX_FLASH_LENGTH bytes from 0x10000000).
 *
 * SMP Safety:
 *   When CONFIG_SMP=y (dual Cortex-M33), flash erase/program operations
 *   require ALL cores to stop executing from XIP flash.  We use the same
 *   SMP isolation pattern as the RP2040 flash MTD driver: before entering
 *   the critical flash section, all other CPUs are forced into a RAM-based
 *   busy-wait loop via nxsched_smp_call_single_async().  After the flash
 *   operation completes and XIP is re-enabled, the other CPUs are released.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>

#ifdef CONFIG_SMP
#include <nuttx/spinlock.h>
#include <nuttx/sched.h>
#endif

#include "rp23xx_flash_mtd.h"
#include "rp23xx_rom.h"

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* XIP base address — flash is memory-mapped here */

#define XIP_BASE                 0x10000000

/* Total physical flash size in bytes.
 * PicoCalc board has 16 MB; override via Kconfig if needed.
 */

#ifndef CONFIG_RP23XX_FLASH_LENGTH
#  define CONFIG_RP23XX_FLASH_LENGTH  (16 * 1024 * 1024)
#endif

/* Erase block = 4 KB (smallest sector-erase on W25Q128) */

#define FLASH_BLOCK_SIZE         (4 * 1024)

/* Write page = 256 bytes (standard QSPI page-program size) */

#define FLASH_PAGE_SIZE          256

/* Partition start: first 4 KB boundary after the firmware image.
 * __flash_binary_end is provided by the linker script.
 */

extern const uint8_t __flash_binary_end[];

#define FLASH_FS_START_ADDR \
    (((uintptr_t)__flash_binary_end + FLASH_BLOCK_SIZE - 1) & \
     ~((uintptr_t)FLASH_BLOCK_SIZE - 1))

#define FLASH_FS_START_OFFSET    (FLASH_FS_START_ADDR - XIP_BASE)
#define FLASH_FS_LENGTH          (CONFIG_RP23XX_FLASH_LENGTH - FLASH_FS_START_OFFSET)

#define FLASH_FS_BLOCK_COUNT     (FLASH_FS_LENGTH / FLASH_BLOCK_SIZE)
#define FLASH_FS_PAGE_COUNT      (FLASH_FS_LENGTH / FLASH_PAGE_SIZE)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* ROM function prototypes — same ABI as RP2040 */

typedef void (*connect_internal_flash_fn)(void);
typedef void (*flash_exit_xip_fn)(void);
typedef void (*flash_range_erase_fn)(uint32_t addr, size_t count,
                                     uint32_t block_size, uint8_t block_cmd);
typedef void (*flash_range_program_fn)(uint32_t addr,
                                       const uint8_t *data, size_t count);
typedef void (*flash_flush_cache_fn)(void);
typedef void (*flash_enter_cmd_xip_fn)(void);

typedef struct rp23xx_flash_dev_s
{
  struct mtd_dev_s  mtd;
  mutex_t           lock;
  connect_internal_flash_fn rom_connect;
  flash_exit_xip_fn         rom_exit_xip;
  flash_range_erase_fn      rom_erase;
  flash_range_program_fn    rom_program;
  flash_flush_cache_fn      rom_flush;
  flash_enter_cmd_xip_fn    rom_enter_xip;
} rp23xx_flash_dev_t;

/****************************************************************************
 * SMP Isolation — pause all other CPUs during flash erase/program
 *
 * When SMP is enabled, the flash erase/program cycle disables XIP.
 * If the other core tries to read from flash (even to fetch code that
 * hasn't been copied to RAM), the system will hard-fault.  We force
 * every other CPU into a RAM-resident busy-loop before touching flash.
 ****************************************************************************/

#ifdef CONFIG_SMP
struct smp_isolation_data_s
{
  volatile spinlock_t cpu_wait;
  volatile spinlock_t cpu_pause;
  volatile spinlock_t cpu_resume;
  struct smp_call_data_s call_data;
};

struct smp_isolation_s
{
  int isolated_cpuid;
  struct smp_isolation_data_s cpu_data[CONFIG_SMP_NCPUS];
};
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     flash_mtd_erase(struct mtd_dev_s *dev,
                               off_t startblock, size_t nblocks);
static ssize_t flash_mtd_bread(struct mtd_dev_s *dev,
                               off_t startblock, size_t nblocks,
                               uint8_t *buffer);
static ssize_t flash_mtd_bwrite(struct mtd_dev_s *dev,
                                off_t startblock, size_t nblocks,
                                const uint8_t *buffer);
static ssize_t flash_mtd_read(struct mtd_dev_s *dev,
                              off_t offset, size_t nbytes,
                              uint8_t *buffer);
static int     flash_mtd_ioctl(struct mtd_dev_s *dev,
                               int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static rp23xx_flash_dev_t g_flash_dev;
static bool g_initialized = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SMP
/****************************************************************************
 * Name: pause_cpu_handler
 *
 * Description:
 *   Executed on each non-isolated CPU via SMP call.  Busy-waits in RAM
 *   until leave_smp_isolation() releases cpu_wait.
 *
 ****************************************************************************/

static int pause_cpu_handler(void *context)
{
  struct smp_isolation_data_s *cpu_data =
    (struct smp_isolation_data_s *)context;

  /* Reserve the resumption lock (released after we resume). */

  spin_lock(&cpu_data->cpu_resume);

  /* Signal enter_smp_isolation() that we are now paused. */

  spin_unlock(&cpu_data->cpu_pause);

  /* Spin here in RAM until leave_smp_isolation() unlocks cpu_wait. */

  spin_lock(&cpu_data->cpu_wait);
  spin_unlock(&cpu_data->cpu_wait);

  /* Signal leave_smp_isolation() that we have resumed. */

  spin_unlock(&cpu_data->cpu_resume);

  return OK;
}

/****************************************************************************
 * Name: init_smp_isolation
 ****************************************************************************/

static void init_smp_isolation(struct smp_isolation_s *data)
{
  struct smp_isolation_data_s *cpu_data;

  for (int cpuid = 0; cpuid < CONFIG_SMP_NCPUS; cpuid++)
    {
      cpu_data = &data->cpu_data[cpuid];
      spin_lock_init(&cpu_data->cpu_wait);
      spin_lock_init(&cpu_data->cpu_pause);
      spin_lock_init(&cpu_data->cpu_resume);
    }
}

/****************************************************************************
 * Name: enter_smp_isolation
 *
 * Description:
 *   Force all CPUs except the current one into a RAM busy-loop.
 *   Must be called with the device mutex held.  Disables scheduling
 *   on the current CPU until leave_smp_isolation() is called.
 *
 ****************************************************************************/

static void enter_smp_isolation(struct smp_isolation_s *data)
{
  struct smp_isolation_data_s *cpu_data;

  sched_lock();

  data->isolated_cpuid = this_cpu();

  /* Send pause request to every other CPU. */

  for (int cpuid = 0; cpuid < CONFIG_SMP_NCPUS; cpuid++)
    {
      cpu_data = &data->cpu_data[cpuid];

      if (cpuid != data->isolated_cpuid)
        {
          spin_lock(&cpu_data->cpu_wait);
          spin_lock(&cpu_data->cpu_pause);
          spin_unlock(&cpu_data->cpu_resume);

          nxsched_smp_call_init(&cpu_data->call_data,
                                pause_cpu_handler, cpu_data);
          nxsched_smp_call_single_async(cpuid, &cpu_data->call_data);
        }
    }

  /* Wait until all other CPUs have entered the pause handler. */

  for (int cpuid = 0; cpuid < CONFIG_SMP_NCPUS; cpuid++)
    {
      cpu_data = &data->cpu_data[cpuid];

      if (cpuid != data->isolated_cpuid)
        {
          spin_lock(&cpu_data->cpu_pause);
          spin_unlock(&cpu_data->cpu_pause);
        }
    }
}

/****************************************************************************
 * Name: leave_smp_isolation
 *
 * Description:
 *   Release all blocked CPUs after flash operation completes.
 *
 ****************************************************************************/

static void leave_smp_isolation(struct smp_isolation_s *data)
{
  struct smp_isolation_data_s *cpu_data;

  /* Tell all other CPUs to resume. */

  for (int cpuid = 0; cpuid < CONFIG_SMP_NCPUS; cpuid++)
    {
      cpu_data = &data->cpu_data[cpuid];

      if (cpuid != data->isolated_cpuid)
        {
          spin_unlock(&cpu_data->cpu_wait);
        }
    }

  /* Wait until all other CPUs have actually resumed. */

  for (int cpuid = 0; cpuid < CONFIG_SMP_NCPUS; cpuid++)
    {
      cpu_data = &data->cpu_data[cpuid];

      if (cpuid != data->isolated_cpuid)
        {
          spin_lock(&cpu_data->cpu_resume);
          spin_unlock(&cpu_data->cpu_resume);
        }
    }

  sched_unlock();
}
#endif /* CONFIG_SMP */

/****************************************************************************
 * Name: do_flash_erase
 *
 * Description:
 *   Erase flash with interrupts disabled.  Must be called with the
 *   device mutex held.
 *
 ****************************************************************************/

static void do_flash_erase(rp23xx_flash_dev_t *dev,
                           uint32_t flash_offs, size_t len)
{
  irqstate_t flags;

  __asm__ volatile ("" : : : "memory");

  flags = enter_critical_section();

  dev->rom_connect();
  dev->rom_exit_xip();
  dev->rom_erase(flash_offs, len, FLASH_BLOCK_SIZE, 0x20);
  dev->rom_flush();
  dev->rom_enter_xip();

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: do_flash_program
 *
 * Description:
 *   Program flash with interrupts disabled.  Must be called with the
 *   device mutex held.
 *
 ****************************************************************************/

static void do_flash_program(rp23xx_flash_dev_t *dev,
                             uint32_t flash_offs,
                             const uint8_t *data, size_t len)
{
  irqstate_t flags;

  __asm__ volatile ("" : : : "memory");

  flags = enter_critical_section();

  dev->rom_connect();
  dev->rom_exit_xip();
  dev->rom_program(flash_offs, data, len);
  dev->rom_flush();
  dev->rom_enter_xip();

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: flash_mtd_erase
 ****************************************************************************/

static int flash_mtd_erase(struct mtd_dev_s *mtd,
                           off_t startblock, size_t nblocks)
{
  rp23xx_flash_dev_t *dev = (rp23xx_flash_dev_t *)mtd;
  int ret;

  finfo("erase block=%lu count=%zu\n",
        (unsigned long)startblock, nblocks);

  ret = nxmutex_lock(&dev->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Erase one block at a time so that SMP isolation and the
   * interrupt-disabled window stay short (~45 ms per 4 KB sector).
   */

  for (size_t i = 0; i < nblocks; i++)
    {
#ifdef CONFIG_SMP
      struct smp_isolation_s smp_iso;
      init_smp_isolation(&smp_iso);
      enter_smp_isolation(&smp_iso);
#endif

      do_flash_erase(dev,
                     (uint32_t)(FLASH_FS_START_OFFSET +
                                (startblock + i) * FLASH_BLOCK_SIZE),
                     FLASH_BLOCK_SIZE);

#ifdef CONFIG_SMP
      leave_smp_isolation(&smp_iso);
#endif
    }

  nxmutex_unlock(&dev->lock);
  return (int)nblocks;
}

/****************************************************************************
 * Name: flash_mtd_bread
 ****************************************************************************/

static ssize_t flash_mtd_bread(struct mtd_dev_s *mtd,
                               off_t startblock, size_t nblocks,
                               uint8_t *buffer)
{
  rp23xx_flash_dev_t *dev = (rp23xx_flash_dev_t *)mtd;
  int ret;

  ret = nxmutex_lock(&dev->lock);
  if (ret < 0)
    {
      return ret;
    }

  finfo("bread block=%lu count=%zu\n",
        (unsigned long)startblock, nblocks);

  memcpy(buffer,
         (const void *)(FLASH_FS_START_ADDR +
                        startblock * FLASH_PAGE_SIZE),
         nblocks * FLASH_PAGE_SIZE);

  nxmutex_unlock(&dev->lock);
  return (ssize_t)nblocks;
}

/****************************************************************************
 * Name: flash_mtd_bwrite
 ****************************************************************************/

static ssize_t flash_mtd_bwrite(struct mtd_dev_s *mtd,
                                off_t startblock, size_t nblocks,
                                const uint8_t *buffer)
{
  rp23xx_flash_dev_t *dev = (rp23xx_flash_dev_t *)mtd;
  int ret;

  finfo("bwrite block=%lu count=%zu\n",
        (unsigned long)startblock, nblocks);

#ifdef CONFIG_SMP
  struct smp_isolation_s smp_isolation;
  init_smp_isolation(&smp_isolation);
#endif

  ret = nxmutex_lock(&dev->lock);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_SMP
  enter_smp_isolation(&smp_isolation);
#endif

  do_flash_program(dev,
                   (uint32_t)(FLASH_FS_START_OFFSET +
                              startblock * FLASH_PAGE_SIZE),
                   buffer,
                   nblocks * FLASH_PAGE_SIZE);

#ifdef CONFIG_SMP
  leave_smp_isolation(&smp_isolation);
#endif

  nxmutex_unlock(&dev->lock);
  return (ssize_t)nblocks;
}

/****************************************************************************
 * Name: flash_mtd_read
 ****************************************************************************/

static ssize_t flash_mtd_read(struct mtd_dev_s *mtd,
                              off_t offset, size_t nbytes,
                              uint8_t *buffer)
{
  rp23xx_flash_dev_t *dev = (rp23xx_flash_dev_t *)mtd;
  int ret;

  ret = nxmutex_lock(&dev->lock);
  if (ret < 0)
    {
      return ret;
    }

  finfo("read offset=%lu count=%zu\n",
        (unsigned long)offset, nbytes);

  memcpy(buffer,
         (const void *)(FLASH_FS_START_ADDR + offset),
         nbytes);

  nxmutex_unlock(&dev->lock);
  return (ssize_t)nbytes;
}

/****************************************************************************
 * Name: flash_mtd_ioctl
 ****************************************************************************/

static int flash_mtd_ioctl(struct mtd_dev_s *mtd,
                           int cmd, unsigned long arg)
{
  switch (cmd)
    {
      case MTDIOC_GEOMETRY:
        {
          struct mtd_geometry_s *geo = (struct mtd_geometry_s *)arg;

          if (geo == NULL)
            {
              return -EINVAL;
            }

          memset(geo, 0, sizeof(*geo));

          geo->blocksize    = FLASH_PAGE_SIZE;
          geo->erasesize    = FLASH_BLOCK_SIZE;
          geo->neraseblocks = FLASH_FS_BLOCK_COUNT;

          finfo("geometry: blocksize=%lu erasesize=%lu neraseblocks=%lu\n",
                (unsigned long)geo->blocksize,
                (unsigned long)geo->erasesize,
                (unsigned long)geo->neraseblocks);
          return OK;
        }

      case MTDIOC_BULKERASE:
        {
          /* Erase all blocks with progress logging.
           * Each 4 KB sector erase takes ~45 ms → ~170 s for 15 MB.
           * Report progress every 5% so the user can see activity.
           */

          size_t total = FLASH_FS_BLOCK_COUNT;
          size_t step  = total / 20;  /* 5 % granularity */
          size_t next_report = step;

          if (step == 0)
            {
              step = 1;
              next_report = 1;
            }

          syslog(LOG_INFO,
                 "flash: erasing %lu blocks (%lu KB)...\n",
                 (unsigned long)total,
                 (unsigned long)(total * FLASH_BLOCK_SIZE / 1024));

          rp23xx_flash_dev_t *bdev = (rp23xx_flash_dev_t *)mtd;
          int bret;

          bret = nxmutex_lock(&bdev->lock);
          if (bret < 0)
            {
              return bret;
            }

          for (size_t b = 0; b < total; b++)
            {
#ifdef CONFIG_SMP
              struct smp_isolation_s smp_iso;
              init_smp_isolation(&smp_iso);
              enter_smp_isolation(&smp_iso);
#endif

              do_flash_erase(bdev,
                             (uint32_t)(FLASH_FS_START_OFFSET +
                                        b * FLASH_BLOCK_SIZE),
                             FLASH_BLOCK_SIZE);

#ifdef CONFIG_SMP
              leave_smp_isolation(&smp_iso);
#endif

              if (b + 1 >= next_report)
                {
                  unsigned pct = (unsigned)((b + 1) * 100 / total);
                  syslog(LOG_INFO,
                         "flash: erase %u%% (%lu/%lu)\n",
                         pct, (unsigned long)(b + 1),
                         (unsigned long)total);
                  next_report += step;
                }
            }

          nxmutex_unlock(&bdev->lock);

          syslog(LOG_INFO, "flash: erase complete\n");
          return OK;
        }

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_flash_mtd_initialize
 ****************************************************************************/

struct mtd_dev_s *rp23xx_flash_mtd_initialize(void)
{
  rp23xx_flash_dev_t *dev = &g_flash_dev;

  if (g_initialized)
    {
      set_errno(EBUSY);
      return NULL;
    }

  /* Sanity: need at least a few erase blocks for the filesystem */

  if (FLASH_FS_BLOCK_COUNT < 4)
    {
      syslog(LOG_ERR, "flash_mtd: not enough space (%u blocks)\n",
             (unsigned)FLASH_FS_BLOCK_COUNT);
      set_errno(ENOMEM);
      return NULL;
    }

  /* Look up ROM flash functions */

  dev->rom_connect  = (connect_internal_flash_fn)
                      rom_func_lookup(ROM_FUNC_CONNECT_INTERNAL_FLASH);
  dev->rom_exit_xip = (flash_exit_xip_fn)
                      rom_func_lookup(ROM_FUNC_FLASH_EXIT_XIP);
  dev->rom_erase    = (flash_range_erase_fn)
                      rom_func_lookup(ROM_FUNC_FLASH_RANGE_ERASE);
  dev->rom_program  = (flash_range_program_fn)
                      rom_func_lookup(ROM_FUNC_FLASH_RANGE_PROGRAM);
  dev->rom_flush    = (flash_flush_cache_fn)
                      rom_func_lookup(ROM_FUNC_FLASH_FLUSH_CACHE);
  dev->rom_enter_xip = (flash_enter_cmd_xip_fn)
                       rom_func_lookup(ROM_FUNC_FLASH_ENTER_CMD_XIP);

  if (dev->rom_connect  == NULL || dev->rom_exit_xip == NULL ||
      dev->rom_erase    == NULL || dev->rom_program  == NULL ||
      dev->rom_flush    == NULL || dev->rom_enter_xip == NULL)
    {
      syslog(LOG_ERR, "flash_mtd: ROM function lookup failed\n");
      set_errno(ENODEV);
      return NULL;
    }

  /* Initialize MTD ops */

  memset(&dev->mtd, 0, sizeof(dev->mtd));
  dev->mtd.erase  = flash_mtd_erase;
  dev->mtd.bread  = flash_mtd_bread;
  dev->mtd.bwrite = flash_mtd_bwrite;
  dev->mtd.read   = flash_mtd_read;
  dev->mtd.ioctl  = flash_mtd_ioctl;
  dev->mtd.name   = "rp23xx_flash";

  nxmutex_init(&dev->lock);

  g_initialized = true;

  syslog(LOG_INFO,
         "flash_mtd: partition offset=0x%08x size=%uKB (%u erase blocks)\n",
         (unsigned)FLASH_FS_START_OFFSET,
         (unsigned)(FLASH_FS_LENGTH / 1024),
         (unsigned)FLASH_FS_BLOCK_COUNT);

  return &dev->mtd;
}
