/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_clockmgr.c
 *
 * RP2350 core clock frequency management for PicoCalc.
 *
 * Power profiles adjust only the SYS PLL (core clock) while leaving
 * USB PLL, peripheral clock, and XOSC unchanged.  This means SPI,
 * I2C, UART, USB, and PIO clocks remain stable.
 *
 * Extended profiles (9 granular steps from 100–400 MHz):
 *   0 = Power Save    — 100 MHz (FBDIV=100, PD1=6, PD2=2)
 *   1 = Low           — 120 MHz (FBDIV=120, PD1=6, PD2=2)
 *   2 = Standard      — 150 MHz (FBDIV=125, PD1=5, PD2=2) [default]
 *   3 = Medium        — 180 MHz (FBDIV= 90, PD1=3, PD2=2)
 *   4 = High          — 200 MHz (FBDIV=100, PD1=3, PD2=2)
 *   5 = Very High     — 225 MHz (FBDIV= 75, PD1=2, PD2=2)
 *   6 = Performance   — 250 MHz (FBDIV=125, PD1=3, PD2=2)
 *   7 = Max Boost     — 300 MHz (FBDIV=150, PD1=3, PD2=2)
 *   8 = Overclock     — 400 MHz (FBDIV=100, PD1=3, PD2=1) **caution**
 *
 * PLL formula: freq = (XOSC / REFDIV) * FBDIV / (PD1 * PD2)
 *   XOSC = 12 MHz, REFDIV = 1
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <syslog.h>

#include "arm_internal.h"
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PLL_SYS registers (RP2350 datasheet section 8.4) */

#define PLL_SYS_BASE           0x40028000
#define PLL_SYS_CS             (PLL_SYS_BASE + 0x00)
#define PLL_SYS_PWR            (PLL_SYS_BASE + 0x04)
#define PLL_SYS_FBDIV_INT      (PLL_SYS_BASE + 0x08)
#define PLL_SYS_PRIM           (PLL_SYS_BASE + 0x0C)

/* CLOCKS block */

#define CLOCKS_BASE            0x40010000
#define CLK_SYS_CTRL           (CLOCKS_BASE + 0x3C)
#define CLK_SYS_DIV            (CLOCKS_BASE + 0x40)
#define CLK_SYS_SELECTED       (CLOCKS_BASE + 0x44)
#define CLK_REF_CTRL           (CLOCKS_BASE + 0x00)
#define CLK_PERI_CTRL          (CLOCKS_BASE + 0x48)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct power_profile_s
{
  const char *name;
  uint32_t    freq_mhz;
  uint16_t    fbdiv;
  uint8_t     pd1;
  uint8_t     pd2;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct power_profile_s g_profiles[] =
{
  /* idx  name               MHz  FBDIV PD1 PD2   VCO (MHz) */
  { "Power Save",          100, 100, 6, 2 },  /* 12*100 / 12 = 100, VCO=1200 */
  { "Low",                 120, 120, 6, 2 },  /* 12*120 / 12 = 120, VCO=1440 */
  { "Standard",            150, 125, 5, 2 },  /* 12*125 / 10 = 150, VCO=1500 */
  { "Medium",              180,  90, 3, 2 },  /* 12* 90 /  6 = 180, VCO=1080 */
  { "High",                200, 100, 3, 2 },  /* 12*100 /  6 = 200, VCO=1200 */
  { "Very High",           225,  75, 2, 2 },  /* 12* 75 /  4 = 225, VCO= 900 */
  { "Performance",         250, 125, 3, 2 },  /* 12*125 /  6 = 250, VCO=1500 */
  { "Max Boost",           300, 150, 3, 2 },  /* 12*150 /  6 = 300, VCO=1800 */
  { "Overclock (400)",     400, 100, 3, 1 },  /* 12*100 /  3 = 400, VCO=1200 */
};

#define NUM_PROFILES  (sizeof(g_profiles) / sizeof(g_profiles[0]))

static int g_current_profile = 2;  /* Standard 150 MHz */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pll_sys_reconfigure(uint16_t fbdiv, uint8_t pd1, uint8_t pd2)
{
  /* Step 1: Switch clk_sys to use XOSC directly (glitchless mux) */

  uint32_t ctrl = getreg32(CLK_SYS_CTRL);
  ctrl &= ~0x01;  /* SRC = ref clock (via glitchless mux) */
  putreg32(ctrl, CLK_SYS_CTRL);

  /* Wait for clock to switch */

  while ((getreg32(CLK_SYS_SELECTED) & 1) == 0);

  /* Step 2: Power down PLL */

  putreg32(0x2D, PLL_SYS_PWR);  /* PD | DSMPD | POSTDIVPD | VCOPD */

  /* Step 3: Set new FBDIV */

  putreg32(fbdiv, PLL_SYS_FBDIV_INT);

  /* Step 4: Power up PLL VCO */

  putreg32(0x20, PLL_SYS_PWR);  /* Only DSMPD set — VCO running */

  /* Wait for PLL lock */

  while ((getreg32(PLL_SYS_CS) & (1 << 31)) == 0);

  /* Step 5: Set post dividers */

  putreg32(((uint32_t)pd1 << 16) | ((uint32_t)pd2 << 12),
           PLL_SYS_PRIM);

  /* Step 6: Power up post dividers */

  putreg32(0x00, PLL_SYS_PWR);  /* All powered */

  /* Step 7: Switch clk_sys back to PLL */

  ctrl = getreg32(CLK_SYS_CTRL);
  ctrl |= 0x01;  /* SRC = aux (PLL_SYS via glitchless mux) */
  putreg32(ctrl, CLK_SYS_CTRL);

  while ((getreg32(CLK_SYS_SELECTED) & 2) == 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_set_power_profile
 *
 * Description:
 *   Switch the core clock to one of predefined power profiles.
 *   Peripherals (SPI, I2C, UART, USB) clocks are NOT changed.
 *
 * Input:
 *   profile — 0=Standard(150), 1=HighPerf(200), 2=PowerSave(100)
 *
 ****************************************************************************/

void rp23xx_set_power_profile(int profile)
{
  if (profile < 0 || profile >= (int)NUM_PROFILES)
    {
      return;
    }

  if (profile == g_current_profile)
    {
      return;
    }

  const struct power_profile_s *p = &g_profiles[profile];

  syslog(LOG_INFO, "clock: switching to %s (%lu MHz) — "
         "FBDIV=%u PD1=%u PD2=%u\n",
         p->name, (unsigned long)p->freq_mhz,
         (unsigned)p->fbdiv, (unsigned)p->pd1, (unsigned)p->pd2);

  irqstate_t flags = enter_critical_section();

  pll_sys_reconfigure(p->fbdiv, p->pd1, p->pd2);

  leave_critical_section(flags);

  g_current_profile = profile;

  syslog(LOG_INFO, "clock: now running at %lu MHz\n",
         (unsigned long)p->freq_mhz);
}

/****************************************************************************
 * Name: rp23xx_get_power_profile
 *
 * Description:
 *   Return the current power profile index.
 *
 ****************************************************************************/

int rp23xx_get_power_profile(void)
{
  return g_current_profile;
}

/****************************************************************************
 * Name: rp23xx_get_sys_freq_mhz
 *
 * Description:
 *   Return the current system clock frequency in MHz.
 *
 ****************************************************************************/

uint32_t rp23xx_get_sys_freq_mhz(void)
{
  return g_profiles[g_current_profile].freq_mhz;
}

/****************************************************************************
 * Name: rp23xx_get_num_profiles
 *
 * Description:
 *   Return the number of available power profiles.
 *
 ****************************************************************************/

int rp23xx_get_num_profiles(void)
{
  return (int)NUM_PROFILES;
}

/****************************************************************************
 * Name: rp23xx_get_profile_name
 *
 * Description:
 *   Return the name of a power profile.
 *
 ****************************************************************************/

const char *rp23xx_get_profile_name(int profile)
{
  if (profile < 0 || profile >= (int)NUM_PROFILES)
    {
      return "Unknown";
    }

  return g_profiles[profile].name;
}
