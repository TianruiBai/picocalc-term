/****************************************************************************
 * pcterm/src/audio_service.c
 *
 * System audio service — background playback engine.
 * Provides the pc_audio_* API declared in app.h.
 * Bridges between app-level controls and the board-level PWM audio driver
 * (rp23xx_audio.c) plus the pcaudio decoder/player modules.
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AUDIO_DECODE_FRAMES   256     /* Frames per decode pass */
#define AUDIO_SERVICE_STACK   4096

/****************************************************************************
 * External References
 *
 * Board-level PWM audio driver (rp23xx_audio.c)
 ****************************************************************************/

extern int  rp23xx_audio_initialize(void);
extern int  rp23xx_audio_write(const int16_t *samples, size_t count);
extern void rp23xx_audio_set_volume(uint8_t volume);
extern uint8_t rp23xx_audio_get_volume(void);

/****************************************************************************
 * Forward declarations — pcaudio decoder module
 ****************************************************************************/

typedef struct audio_decoder_s audio_decoder_t;

extern audio_decoder_t *audio_decoder_open(const char *path);
extern int              audio_decoder_read(audio_decoder_t *dec,
                                           int16_t *buf, int frames);
extern int              audio_decoder_seek(audio_decoder_t *dec,
                                           uint32_t sample);
extern void             audio_decoder_close(audio_decoder_t *dec);
extern uint32_t         audio_decoder_sample_rate(const audio_decoder_t *dec);
extern uint32_t         audio_decoder_total_samples(const audio_decoder_t *dec);
extern uint32_t         audio_decoder_current_sample(const audio_decoder_t *dec);

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef enum
{
  AUDIO_SVC_STOPPED,
  AUDIO_SVC_PLAYING,
  AUDIO_SVC_PAUSED,
} audio_svc_state_t;

struct audio_service_s
{
  bool             initialized;
  audio_svc_state_t state;
  uint8_t          volume;

  /* Current track */

  audio_decoder_t *decoder;
  char             filepath[256];
  char             title[64];
  char             artist[64];

  /* Decode buffer */

  int16_t         *decode_buf;

  /* Background decode/feed thread */

  pthread_t        thread;
  bool             thread_exit;
  pthread_mutex_t  lock;

  /* Playlist navigation */

  char           **playlist;
  int              pl_count;
  int              pl_index;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct audio_service_s g_audio_svc;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: extract_filename
 *
 * Description:
 *   Extract title from file path (strip dir and extension).
 *
 ****************************************************************************/

static void extract_filename(const char *path, char *title, size_t maxlen)
{
  const char *base = strrchr(path, '/');
  if (base)
    {
      base++;
    }
  else
    {
      base = path;
    }

  strncpy(title, base, maxlen - 1);
  title[maxlen - 1] = '\0';

  /* Strip extension */

  char *dot = strrchr(title, '.');
  if (dot)
    {
      *dot = '\0';
    }
}

/****************************************************************************
 * Name: audio_decode_thread
 *
 * Description:
 *   Background thread that continuously decodes audio frames and feeds
 *   them to the PWM audio driver ring buffer.
 *
 ****************************************************************************/

static void *audio_decode_thread(void *arg)
{
  struct audio_service_s *svc = (struct audio_service_s *)arg;

  syslog(LOG_INFO, "audiosvc: decode thread started\n");

  while (!svc->thread_exit)
    {
      pthread_mutex_lock(&svc->lock);

      if (svc->state != AUDIO_SVC_PLAYING || svc->decoder == NULL)
        {
          pthread_mutex_unlock(&svc->lock);
          usleep(10000);  /* 10ms idle sleep */
          continue;
        }

      /* Decode a chunk of frames */

      int frames = audio_decoder_read(svc->decoder, svc->decode_buf,
                                      AUDIO_DECODE_FRAMES);

      pthread_mutex_unlock(&svc->lock);

      if (frames <= 0)
        {
          /* End of track — try next in playlist, or stop */

          syslog(LOG_INFO, "audiosvc: track ended\n");

          pc_audio_next();

          if (svc->state == AUDIO_SVC_STOPPED)
            {
              continue;
            }
        }
      else
        {
          /* Feed decoded PCM to the hardware audio driver */

          int written = rp23xx_audio_write(svc->decode_buf, frames * 2);
          (void)written;

          /* Pace decode to not run too far ahead.
           * ~5.8ms per 256-frame chunk at 44100 Hz.
           */

          usleep(4000);
        }
    }

  syslog(LOG_INFO, "audiosvc: decode thread exiting\n");
  return NULL;
}

/****************************************************************************
 * Name: audio_service_init
 *
 * Description:
 *   Initialize the audio service singleton. Called once at boot.
 *
 ****************************************************************************/

static int audio_service_init(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (svc->initialized)
    {
      return 0;
    }

  memset(svc, 0, sizeof(*svc));

  /* Initialize the board-level PWM driver */

  int ret = rp23xx_audio_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "audiosvc: PWM init failed: %d\n", ret);
      return ret;
    }

  svc->volume = rp23xx_audio_get_volume();
  svc->state  = AUDIO_SVC_STOPPED;

  /* Allocate decode buffer in system heap so playback init does not depend
   * on PSRAM allocator availability.
   */

  svc->decode_buf = (int16_t *)malloc(
    AUDIO_DECODE_FRAMES * 2 * sizeof(int16_t));
  if (svc->decode_buf == NULL)
    {
      syslog(LOG_ERR, "audiosvc: decode buffer allocation failed\n");
      return -ENOMEM;
    }

  pthread_mutex_init(&svc->lock, NULL);

  /* Start background decode thread */

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, AUDIO_SERVICE_STACK);

  svc->thread_exit = false;
  pthread_create(&svc->thread, &attr, audio_decode_thread, svc);
  pthread_attr_destroy(&attr);

  svc->initialized = true;

  syslog(LOG_INFO, "audiosvc: audio service initialized\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pc_audio_play
 *
 * Description:
 *   Start playing an audio file. Stops any currently playing track.
 *
 ****************************************************************************/

int pc_audio_play(const char *filepath)
{
  struct audio_service_s *svc = &g_audio_svc;
  int ret;

  if (!svc->initialized)
    {
      ret = audio_service_init();
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Stop current playback if any */

  if (svc->decoder != NULL)
    {
      pc_audio_stop();
    }

  pthread_mutex_lock(&svc->lock);

  /* Open and decode the new file */

  svc->decoder = audio_decoder_open(filepath);
  if (svc->decoder == NULL)
    {
      pthread_mutex_unlock(&svc->lock);
      syslog(LOG_ERR, "audiosvc: cannot open: %s\n", filepath);
      return -ENOENT;
    }

  strncpy(svc->filepath, filepath, sizeof(svc->filepath) - 1);
  extract_filename(filepath, svc->title, sizeof(svc->title));
  strncpy(svc->artist, "Unknown", sizeof(svc->artist));

  svc->state = AUDIO_SVC_PLAYING;

  pthread_mutex_unlock(&svc->lock);

  syslog(LOG_INFO, "audiosvc: playing %s\n", svc->title);
  return 0;
}

/****************************************************************************
 * Name: pc_audio_pause
 ****************************************************************************/

int pc_audio_pause(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (svc->state != AUDIO_SVC_PLAYING)
    {
      return -EINVAL;
    }

  svc->state = AUDIO_SVC_PAUSED;
  syslog(LOG_INFO, "audiosvc: paused\n");
  return 0;
}

/****************************************************************************
 * Name: pc_audio_resume
 ****************************************************************************/

int pc_audio_resume(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (svc->state != AUDIO_SVC_PAUSED)
    {
      return -EINVAL;
    }

  svc->state = AUDIO_SVC_PLAYING;
  syslog(LOG_INFO, "audiosvc: resumed\n");
  return 0;
}

/****************************************************************************
 * Name: pc_audio_stop
 ****************************************************************************/

int pc_audio_stop(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  pthread_mutex_lock(&svc->lock);

  svc->state = AUDIO_SVC_STOPPED;

  if (svc->decoder != NULL)
    {
      audio_decoder_close(svc->decoder);
      svc->decoder = NULL;
    }

  svc->filepath[0] = '\0';
  svc->title[0]    = '\0';
  svc->artist[0]   = '\0';

  pthread_mutex_unlock(&svc->lock);

  syslog(LOG_INFO, "audiosvc: stopped\n");
  return 0;
}

/****************************************************************************
 * Name: pc_audio_next
 *
 * Description:
 *   Skip to the next track in the playlist. If no playlist, stop.
 *
 ****************************************************************************/

int pc_audio_next(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (svc->playlist == NULL || svc->pl_count == 0)
    {
      pc_audio_stop();
      return -ENOENT;
    }

  svc->pl_index++;
  if (svc->pl_index >= svc->pl_count)
    {
      svc->pl_index = 0;  /* Wrap around */
    }

  return pc_audio_play(svc->playlist[svc->pl_index]);
}

/****************************************************************************
 * Name: pc_audio_prev
 *
 * Description:
 *   Skip to the previous track in the playlist. If no playlist, restart.
 *
 ****************************************************************************/

int pc_audio_prev(void)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (svc->playlist == NULL || svc->pl_count == 0)
    {
      /* No playlist — restart current track from beginning */

      if (svc->decoder != NULL)
        {
          audio_decoder_seek(svc->decoder, 0);
          return 0;
        }

      return -ENOENT;
    }

  svc->pl_index--;
  if (svc->pl_index < 0)
    {
      svc->pl_index = svc->pl_count - 1;
    }

  return pc_audio_play(svc->playlist[svc->pl_index]);
}

/****************************************************************************
 * Name: pc_audio_set_volume
 ****************************************************************************/

int pc_audio_set_volume(uint8_t volume)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (!svc->initialized)
    {
      int ret = audio_service_init();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (volume > 100)
    {
      volume = 100;
    }

  svc->volume = volume;
  rp23xx_audio_set_volume(volume);
  return 0;
}

/****************************************************************************
 * Name: pc_audio_get_status
 *
 * Description:
 *   Snapshot the current playback state into the caller's struct.
 *
 ****************************************************************************/

int pc_audio_get_status(pc_audio_status_t *status)
{
  struct audio_service_s *svc = &g_audio_svc;

  if (status == NULL)
    {
      return -EINVAL;
    }

  memset(status, 0, sizeof(*status));

  status->playing = (svc->state == AUDIO_SVC_PLAYING);
  status->paused  = (svc->state == AUDIO_SVC_PAUSED);
  status->volume  = svc->volume;

  strncpy(status->title, svc->title, sizeof(status->title) - 1);
  strncpy(status->artist, svc->artist, sizeof(status->artist) - 1);

  if (svc->decoder != NULL)
    {
      uint32_t sr = audio_decoder_sample_rate(svc->decoder);
      uint32_t total = audio_decoder_total_samples(svc->decoder);
      uint32_t cur = audio_decoder_current_sample(svc->decoder);

      if (sr > 0)
        {
          status->position_ms = (uint32_t)((uint64_t)cur * 1000 / sr);
          status->duration_ms = (uint32_t)((uint64_t)total * 1000 / sr);
        }
    }

  return 0;
}
