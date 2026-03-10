/****************************************************************************
 * boards/arm/rp23xx/picocalc-rp2350b/src/rp23xx_audio.c
 *
 * PWM audio driver for PicoCalc dual speakers.
 * Provides a simple audio output interface for the OS audio service.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <syslog.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include "arm_internal.h"
#include "rp23xx_gpio.h"
#include "hardware/rp23xx_pwm.h"
#include <arch/board/board.h>

/* Ring buffer in SRAM (directly addressable for ISR access at 44.1 kHz).
 * PSRAM handles are not dereferenceable, so the ring buffer must be in SRAM.
 * 8192 * 2 bytes = 16 KB — affordable in 520 KB SRAM.
 */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PWM configuration for audio output
 *
 * RP2350 PWM clock = 150 MHz system clock
 * Target: 10-bit resolution at ~44.1 kHz
 * PWM freq = 150 MHz / (2^10) = ~146 kHz wrap rate
 * Actual audio sample rate controlled by PWM wrap interrupt
 *
 * GP26 → PWM slice 5 channel A (left)
 * GP27 → PWM slice 5 channel B (right)
 *
 * PWM slice = (gpio_pin / 2) % 8
 *   GP26: (26/2) % 8 = 13 % 8 = 5, channel A
 *   GP27: (27/2) % 8 = 13 % 8 = 5, channel B
 *
 * For 44.1 kHz sample rate with 150 MHz clock:
 *   divider = 150 MHz / (44100 * 1024) ≈ 3.32
 *   Use integer divider = 3, frac = 5 → effective 3.3125
 *   Actual rate = 150 MHz / (3.3125 * 1024) ≈ 44,208 Hz (close enough)
 */

#define AUDIO_PWM_WRAP     (1 << BOARD_AUDIO_PWM_BITS)  /* 1024 */
#define AUDIO_PWM_SLICE    4     /* GP26/GP27 → slice 5 */
#define AUDIO_PWM_DIV_INT  3     /* Integer part of clock divider */
#define AUDIO_PWM_DIV_FRAC 5     /* Fractional part (1/16ths) */
#define AUDIO_RING_SAMPLES 8192  /* Ring buffer size (power of 2) */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct audio_dev_s
{
  bool initialized;
  bool playing;
  uint8_t volume;          /* 0-100 */

  /* Ring buffer for audio samples */
  int16_t *ring_buffer;
  size_t   ring_size;       /* Total slots */
  size_t   ring_head;       /* Write position */
  size_t   ring_tail;       /* Read position (ISR) */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct audio_dev_s g_audiodev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: audio_pwm_isr
 *
 * Description:
 *   PWM wrap interrupt handler. Called at the audio sample rate (~44.1 kHz).
 *   Reads the next stereo sample pair from the ring buffer and sets the
 *   PWM duty cycle for left (channel A) and right (channel B).
 *
 ****************************************************************************/

static int audio_pwm_isr(int irq, void *context, void *arg)
{
  struct audio_dev_s *dev = &g_audiodev;

  /* Clear the PWM interrupt for our slice */

  putreg32(1 << AUDIO_PWM_SLICE, RP23XX_PWM_INTR);

  if (!dev->playing || dev->ring_buffer == NULL)
    {
      /* Output silence (midpoint) */

      putreg32((AUDIO_PWM_WRAP / 2) |
               ((AUDIO_PWM_WRAP / 2) << RP23XX_PWM_CC_B_SHIFT),
               RP23XX_PWM_CC(AUDIO_PWM_SLICE));
      return 0;
    }

  /* Read stereo pair from ring buffer (interleaved: L, R, L, R...) */

  int16_t left  = 0;
  int16_t right = 0;

  if (dev->ring_tail != dev->ring_head)
    {
      left = dev->ring_buffer[dev->ring_tail];
      dev->ring_tail = (dev->ring_tail + 1) % dev->ring_size;
    }

  if (dev->ring_tail != dev->ring_head)
    {
      right = dev->ring_buffer[dev->ring_tail];
      dev->ring_tail = (dev->ring_tail + 1) % dev->ring_size;
    }

  /* Apply volume (0-100) */

  int32_t lsamp = ((int32_t)left  * dev->volume) / 100;
  int32_t rsamp = ((int32_t)right * dev->volume) / 100;

  /* Convert signed 16-bit (-32768..32767) to unsigned 10-bit (0..1023) */

  uint32_t lpwm = (uint32_t)((lsamp + 32768) >> 6);
  uint32_t rpwm = (uint32_t)((rsamp + 32768) >> 6);

  if (lpwm >= AUDIO_PWM_WRAP) lpwm = AUDIO_PWM_WRAP - 1;
  if (rpwm >= AUDIO_PWM_WRAP) rpwm = AUDIO_PWM_WRAP - 1;

  /* Set both channels in one register write */

  putreg32(lpwm | (rpwm << RP23XX_PWM_CC_B_SHIFT),
           RP23XX_PWM_CC(AUDIO_PWM_SLICE));

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rp23xx_audio_initialize
 *
 * Description:
 *   Initialize PWM pins for audio output and set up the sample timer.
 *
 ****************************************************************************/

int rp23xx_audio_initialize(void)
{
  struct audio_dev_s *dev = &g_audiodev;

  if (dev->initialized)
    {
      return 0;
    }

  syslog(LOG_INFO, "audio: initializing PWM audio on slice %d...\n",
         AUDIO_PWM_SLICE);

  /* Step 1: Allocate ring buffer in SRAM (directly addressable for ISR) */

  dev->ring_buffer = (int16_t *)kmm_zalloc(
    AUDIO_RING_SAMPLES * sizeof(int16_t));
  if (dev->ring_buffer == NULL)
    {
      syslog(LOG_ERR, "audio: ring buffer allocation failed (%d bytes)\n",
             (int)(AUDIO_RING_SAMPLES * sizeof(int16_t)));
      return -ENOMEM;
    }

  dev->ring_size = AUDIO_RING_SAMPLES;

  syslog(LOG_DEBUG, "audio: ring buffer %d KB allocated\n",
         (int)(AUDIO_RING_SAMPLES * sizeof(int16_t) / 1024));

  /* Step 2: Assign GP26/GP27 to PWM function (function 4 on RP2350).
   * Without this, the pins remain in default SIO mode and produce
   * no PWM output.
   */

  rp23xx_gpio_set_function(BOARD_AUDIO_PIN_LEFT,
                           RP23XX_GPIO_FUNC_PWM);
  rp23xx_gpio_set_function(BOARD_AUDIO_PIN_RIGHT,
                           RP23XX_GPIO_FUNC_PWM);

  /* Step 3: Configure PWM slice 5 for audio output
   * GP26 → slice 5, channel A (left speaker)
   * GP27 → slice 5, channel B (right speaker)
   *
   * Set wrap (TOP) = 1023 for 10-bit resolution.
   * Set clock divider for ~44.1 kHz sample rate.
   */

  /* Disable the slice while configuring */

  putreg32(0, RP23XX_PWM_CSR(AUDIO_PWM_SLICE));

  /* Set wrap value (TOP register) */

  putreg32(AUDIO_PWM_WRAP - 1, RP23XX_PWM_TOP(AUDIO_PWM_SLICE));

  /* Set clock divider: integer.frac (8.4 fixed point)
   * DIV register = (int << 4) | frac
   */

  putreg32((AUDIO_PWM_DIV_INT << RP23XX_PWM_DIV_INT_SHIFT) |
           (AUDIO_PWM_DIV_FRAC << RP23XX_PWM_DIV_FRAC_SHIFT),
           RP23XX_PWM_DIV(AUDIO_PWM_SLICE));

  /* Set initial duty cycle to midpoint (silence) */

  putreg32((AUDIO_PWM_WRAP / 2) |
           ((AUDIO_PWM_WRAP / 2) << RP23XX_PWM_CC_B_SHIFT),
           RP23XX_PWM_CC(AUDIO_PWM_SLICE));

  /* Reset counter */

  putreg32(0, RP23XX_PWM_CTR(AUDIO_PWM_SLICE));

  syslog(LOG_DEBUG, "audio: PWM slice %d configured\n",
         AUDIO_PWM_SLICE);

  /* Step 4: Clear any pending PWM interrupt from prior boot/reset,
   * then attach the wrap interrupt handler for sample-rate ISR.
   * All PWM slices share a single IRQ (RP23XX_PWM_IRQ_WRAP_0).
   *
   * We attach the handler but do NOT enable the interrupt or PWM yet.
   * The ISR fires at 44.1 kHz — only activate when audio is playing
   * to avoid wasting ~3% CPU on silence output.  The PWM channel and
   * interrupt are enabled lazily in rp23xx_audio_write().
   */

  /* Ensure no stale interrupt is pending from warm reset */

  putreg32(1 << AUDIO_PWM_SLICE, RP23XX_PWM_INTR);
  modreg32(0, 1 << AUDIO_PWM_SLICE, RP23XX_PWM_IRQ0_INTE);

  irq_attach(RP23XX_PWM_IRQ_WRAP_0,
             (xcpt_t)audio_pwm_isr, NULL);

  /* Do NOT enable the PWM interrupt or channel here.
   * They will be enabled on first audio write (see rp23xx_audio_write).
   */

  dev->volume = 80;       /* Default 80% */
  dev->playing = false;
  dev->ring_head = 0;
  dev->ring_tail = 0;
  dev->initialized = true;

  syslog(LOG_INFO, "audio: ring buffer %d KB in SRAM "
         "(%d samples)\n",
         (int)(AUDIO_RING_SAMPLES * sizeof(int16_t) / 1024),
         AUDIO_RING_SAMPLES);
  syslog(LOG_INFO, "audio: PWM slice %d: GP%d(L) GP%d(R), "
         "%d-bit @ %d Hz\n",
         AUDIO_PWM_SLICE,
         BOARD_AUDIO_PIN_LEFT, BOARD_AUDIO_PIN_RIGHT,
         BOARD_AUDIO_PWM_BITS, BOARD_AUDIO_PWM_FREQ);
  syslog(LOG_INFO, "audio: ISR attached (lazy-start, "
         "idle until first write)\n");

  return 0;
}

/****************************************************************************
 * Name: rp23xx_audio_write
 *
 * Description:
 *   Write PCM samples to the audio ring buffer.
 *   Samples are 16-bit signed, mono or interleaved stereo.
 *
 *   Returns: Number of samples actually written (may be less if buffer full)
 *
 ****************************************************************************/

int rp23xx_audio_write(const int16_t *samples, size_t count)
{
  struct audio_dev_s *dev = &g_audiodev;

  if (!dev->initialized || dev->ring_buffer == NULL)
    {
      return -ENODEV;
    }

  /* Lazy-start: enable PWM + ISR on first audio write */

  if (!dev->playing)
    {
      dev->playing = true;

      /* Enable PWM wrap interrupt for our slice */

      modreg32(1 << AUDIO_PWM_SLICE, 1 << AUDIO_PWM_SLICE,
               RP23XX_PWM_IRQ0_INTE);
      up_enable_irq(RP23XX_PWM_IRQ_WRAP_0);

      /* Enable the PWM channel */

      putreg32(RP23XX_PWM_CSR_EN, RP23XX_PWM_CSR(AUDIO_PWM_SLICE));
    }

  size_t written = 0;

  while (written < count)
    {
      size_t next = (dev->ring_head + 1) % dev->ring_size;
      if (next == dev->ring_tail)
        {
          break;  /* Buffer full */
        }

      dev->ring_buffer[dev->ring_head] = samples[written];
      dev->ring_head = next;
      written++;
    }

  return (int)written;
}

/****************************************************************************
 * Name: rp23xx_audio_set_volume
 ****************************************************************************/

void rp23xx_audio_set_volume(uint8_t volume)
{
  if (volume > 100)
    {
      volume = 100;
    }

  g_audiodev.volume = volume;
}

uint8_t rp23xx_audio_get_volume(void)
{
  return g_audiodev.volume;
}
