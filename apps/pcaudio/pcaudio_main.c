/****************************************************************************
 * apps/pcaudio/pcaudio_main.c
 *
 * Audio player with MP3/WAV support.
 * Features: minimp3 decoder, Core 1 playback pipeline, playlists.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * External References — pcaudio modules
 ****************************************************************************/

/* pcaudio_ui.c */

extern void pcaudio_ui_create(lv_obj_t *parent);
extern void pcaudio_ui_set_title(const char *title);
extern void pcaudio_ui_set_artist(const char *artist);
extern void pcaudio_ui_set_progress(int current_ms, int total_ms);

/* pcaudio_playlist.c */

typedef struct playlist_s playlist_t;

extern playlist_t  *playlist_create(void);
extern void         playlist_destroy(playlist_t *pl);
extern int          playlist_scan_dir(playlist_t *pl, const char *dir_path);
extern int          playlist_load_m3u(playlist_t *pl, const char *m3u_path);
extern const char  *playlist_current(const playlist_t *pl);
extern const char  *playlist_next(playlist_t *pl);
extern const char  *playlist_prev(playlist_t *pl);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static playlist_t  *g_playlist   = NULL;
static lv_timer_t  *g_timer      = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcaudio_update_timer
 *
 * Description:
 *   LVGL timer callback: polls audio status and updates the UI.
 *
 ****************************************************************************/

static void pcaudio_update_timer(lv_timer_t *timer)
{
  pc_audio_status_t status;

  (void)timer;

  if (pc_audio_get_status(&status) < 0)
    {
      return;
    }

  pcaudio_ui_set_progress(status.position_ms, status.duration_ms);

  if (status.playing && status.title[0])
    {
      pcaudio_ui_set_title(status.title);
      pcaudio_ui_set_artist(status.artist);
    }
}

/****************************************************************************
 * Name: pcaudio_play_current
 *
 * Description:
 *   Play the current playlist entry.
 *
 ****************************************************************************/

static void pcaudio_play_current(void)
{
  const char *path = playlist_current(g_playlist);

  if (path == NULL)
    {
      return;
    }

  pc_audio_play(path);

  /* Extract filename for display */

  const char *base = strrchr(path, '/');
  pcaudio_ui_set_title(base ? base + 1 : path);
  pcaudio_ui_set_artist("");
}

/****************************************************************************
 * Name: pcaudio_key_handler
 *
 * Description:
 *   Handle keyboard input for audio player controls.
 *
 ****************************************************************************/

static void pcaudio_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  switch (key)
    {
      case ' ':
        {
          pc_audio_status_t st;
          pc_audio_get_status(&st);

          if (st.paused)
            {
              pc_audio_resume();
            }
          else if (st.playing)
            {
              pc_audio_pause();
            }
          else if (g_playlist != NULL)
            {
              pcaudio_play_current();
            }
        }
        break;

      case 'n':
      case 'N':
        {
          const char *next = playlist_next(g_playlist);
          if (next != NULL)
            {
              pc_audio_play(next);
              const char *base = strrchr(next, '/');
              pcaudio_ui_set_title(base ? base + 1 : next);
            }
        }
        break;

      case 'p':
      case 'P':
        {
          const char *prev = playlist_prev(g_playlist);
          if (prev != NULL)
            {
              pc_audio_play(prev);
              const char *base = strrchr(prev, '/');
              pcaudio_ui_set_title(base ? base + 1 : prev);
            }
        }
        break;

      case '+':
      case '=':
        {
          pc_audio_status_t st;
          pc_audio_get_status(&st);
          pc_audio_set_volume(st.volume < 95 ? st.volume + 5 : 100);
        }
        break;

      case '-':
      case '_':
        {
          pc_audio_status_t st;
          pc_audio_get_status(&st);
          pc_audio_set_volume(st.volume > 5 ? st.volume - 5 : 0);
        }
        break;

      case 'q':
      case 'Q':
        pc_audio_stop();
        pc_app_exit(0);
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: pcaudio_main
 ****************************************************************************/

static int pcaudio_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Create the player UI (album art, progress, controls) */

  pcaudio_ui_create(screen);

  /* Scan /mnt/sd/music/ for audio files */

  g_playlist = playlist_create();
  if (g_playlist != NULL)
    {
      int count = playlist_scan_dir(g_playlist, "/mnt/sd/music");
      if (count > 0)
        {
          pcaudio_play_current();
        }
      else
        {
          pcaudio_ui_set_title("No music files found");
          pcaudio_ui_set_artist("Add .mp3/.wav to /mnt/sd/music/");
        }
    }

  /* Register keyboard handler */

  lv_obj_add_event_cb(screen, pcaudio_key_handler, LV_EVENT_KEY, NULL);
  lv_group_t *grp = lv_group_get_default();
  if (grp)
    {
      lv_group_add_obj(grp, screen);
    }

  /* Start periodic UI update timer (250ms) */

  g_timer = lv_timer_create(pcaudio_update_timer, 250, NULL);

  return 0;
}

/****************************************************************************
 * Name: pcaudio_save / pcaudio_restore
 *
 * Description:
 *   Save/restore current playback position for app switching.
 *
 ****************************************************************************/

static int pcaudio_save(void *buf, size_t *len)
{
  pc_audio_status_t st;
  pc_audio_get_status(&st);

  if (*len < sizeof(st))
    {
      *len = sizeof(st);
      return -1;
    }

  memcpy(buf, &st, sizeof(st));
  *len = sizeof(st);
  return 0;
}

static int pcaudio_restore(const void *buf, size_t len)
{
  /* Audio service persists across app switches so nothing to do here */

  (void)buf;
  (void)len;
  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcaudio_app = {
  .info = {
    .name         = "pcaudio",
    .display_name = "Audio Player",
    .version      = "1.0.0",
    .category     = "entertainment",
    .icon         = LV_SYMBOL_AUDIO,
    .min_ram      = 262144,   /* 256 KB for decode buffer */
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_BACKGROUND,
  },
  .main    = pcaudio_main,
  .save    = pcaudio_save,
  .restore = pcaudio_restore,
};
