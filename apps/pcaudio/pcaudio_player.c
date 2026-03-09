/****************************************************************************
 * apps/pcaudio/pcaudio_player.c
 *
 * Playback engine: reads decoded PCM and feeds to PWM audio driver.
 * Designed to run on Core 1 (Cortex-M33 #1) for uninterrupted playback.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AUDIO_RING_SIZE     8192    /* Ring buffer samples */
#define AUDIO_CHUNK_FRAMES  256     /* Frames per decode pass */
#define PLAYBACK_DEVICE     "/dev/audio/pcm0"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  PLAYER_STOPPED,
  PLAYER_PLAYING,
  PLAYER_PAUSED,
} player_state_t;

typedef struct audio_player_s
{
  player_state_t state;
  int            volume;       /* 0-100 */
  pthread_t      thread;
  bool           thread_exit;
  int            audio_fd;

  /* Ring buffer between decoder and audio output */

  int16_t       *ring_buf;
  volatile int   ring_write;
  volatile int   ring_read;
} audio_player_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ring_available(audio_player_t *p)
{
  int avail = p->ring_write - p->ring_read;
  if (avail < 0) avail += AUDIO_RING_SIZE;
  return avail;
}

static int ring_free(audio_player_t *p)
{
  return AUDIO_RING_SIZE - ring_available(p) - 1;
}

/**
 * Audio output thread — runs continuously, draining the ring buffer
 * to the PWM audio device.
 */
static void *player_thread(void *arg)
{
  audio_player_t *p = (audio_player_t *)arg;

  syslog(LOG_INFO, "AUDIO: Playback thread started\n");

  while (!p->thread_exit)
    {
      if (p->state != PLAYER_PLAYING)
        {
          usleep(10000);  /* 10ms idle when paused */
          continue;
        }

      int avail = ring_available(p);
      if (avail == 0)
        {
          usleep(1000);  /* 1ms wait for data */
          continue;
        }

      /* Write samples to audio device */

      int to_write = (avail > AUDIO_CHUNK_FRAMES)
                     ? AUDIO_CHUNK_FRAMES : avail;

      /* Apply volume scaling */

      int16_t scaled_buf[AUDIO_CHUNK_FRAMES];

      for (int i = 0; i < to_write; i++)
        {
          int idx = (p->ring_read + i) % AUDIO_RING_SIZE;
          int32_t sample = p->ring_buf[idx];
          sample = (sample * p->volume) / 100;

          if (sample > 32767) sample = 32767;
          if (sample < -32768) sample = -32768;
          scaled_buf[i] = (int16_t)sample;
        }

      /* Write scaled samples to audio driver ring buffer */

      extern int rp23xx_audio_write(const int16_t *samples, size_t count);
      rp23xx_audio_write(scaled_buf, to_write);

      p->ring_read = (p->ring_read + to_write) % AUDIO_RING_SIZE;

      usleep(1000);  /* ~1ms between writes */
    }

  syslog(LOG_INFO, "AUDIO: Playback thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

audio_player_t *audio_player_create(void)
{
  audio_player_t *p;

  p = (audio_player_t *)pc_app_psram_alloc(sizeof(audio_player_t));
  if (p == NULL) return NULL;

  memset(p, 0, sizeof(audio_player_t));

  p->ring_buf = (int16_t *)pc_app_psram_alloc(
    AUDIO_RING_SIZE * sizeof(int16_t));
  if (p->ring_buf == NULL)
    {
      pc_app_psram_free(p);
      return NULL;
    }

  p->state  = PLAYER_STOPPED;
  p->volume = 80;

  /* Start playback thread */

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 4096);

  p->thread_exit = false;
  pthread_create(&p->thread, &attr, player_thread, p);
  pthread_attr_destroy(&attr);

  return p;
}

void audio_player_destroy(audio_player_t *p)
{
  if (p == NULL) return;

  p->thread_exit = true;
  pthread_join(p->thread, NULL);

  if (p->ring_buf) pc_app_psram_free(p->ring_buf);
  pc_app_psram_free(p);
}

/**
 * Feed decoded PCM samples into the ring buffer.
 */
int audio_player_feed(audio_player_t *p, const int16_t *samples, int count)
{
  int space = ring_free(p);
  if (count > space) count = space;

  for (int i = 0; i < count; i++)
    {
      p->ring_buf[p->ring_write] = samples[i];
      p->ring_write = (p->ring_write + 1) % AUDIO_RING_SIZE;
    }

  return count;
}

void audio_player_play(audio_player_t *p)
{
  if (p) p->state = PLAYER_PLAYING;
}

void audio_player_pause(audio_player_t *p)
{
  if (p) p->state = PLAYER_PAUSED;
}

void audio_player_stop(audio_player_t *p)
{
  if (p)
    {
      p->state = PLAYER_STOPPED;
      p->ring_read = p->ring_write = 0;
    }
}

void audio_player_set_volume(audio_player_t *p, int volume)
{
  if (p) p->volume = (volume < 0) ? 0 : (volume > 100) ? 100 : volume;
}

player_state_t audio_player_get_state(const audio_player_t *p)
{
  return p ? p->state : PLAYER_STOPPED;
}
