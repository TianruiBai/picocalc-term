/****************************************************************************
 * apps/pcvideo/pcvideo_main.c
 *
 * PicoCalc Video (.pcv) player.
 * Features: .pcv container playback, A/V sync, playback controls.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"
#include "pcterm/config.h"

/****************************************************************************
 * External References — pcvideo modules
 ****************************************************************************/

/* pcvideo_pcv.c */

typedef struct pcv_file_s pcv_file_t;

extern pcv_file_t *pcv_open(const char *path);
extern int         pcv_read_frame(pcv_file_t *pcv,
                                  uint16_t *framebuf,
                                  int16_t *audiobuf,
                                  int *audio_samples);
extern int         pcv_seek(pcv_file_t *pcv, uint32_t frame);
extern void        pcv_close(pcv_file_t *pcv);

/* pcvideo_playback.c */

typedef struct video_playback_s video_playback_t;

extern video_playback_t *playback_create(lv_obj_t *parent,
                                         int width, int height);
extern void              playback_destroy(video_playback_t *pb);
extern void              playback_show_frame(video_playback_t *pb,
                                             const uint16_t *rgb565,
                                             int width, int height);
extern bool              playback_frame_due(video_playback_t *pb);
extern void              playback_play(video_playback_t *pb);
extern void              playback_pause(video_playback_t *pb);
extern void              playback_stop(video_playback_t *pb);

/* pcvideo_ui.c */

extern void pcvideo_ui_create(lv_obj_t *parent);
extern void pcvideo_ui_show(void);
extern void pcvideo_ui_hide(void);
extern bool pcvideo_ui_visible(void);
extern void pcvideo_ui_set_title(const char *title);
extern void pcvideo_ui_update(int current_frame, int total_frames, int fps);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pcv_file_t        *g_pcv       = NULL;
static video_playback_t  *g_playback  = NULL;
static lv_timer_t        *g_timer     = NULL;
static uint16_t          *g_framebuf  = NULL;
static int16_t           *g_audiobuf  = NULL;
static bool               g_playing   = false;
static uint32_t           g_cur_frame = 0;
static uint32_t           g_tot_frame = 0;
static int                g_fps       = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: find_first_pcv
 *
 * Description:
 *   Scan /mnt/sd/video/ for the first .pcv file.
 *
 ****************************************************************************/

static int find_first_pcv(char *buf, size_t buflen)
{
  DIR           *dir;
  struct dirent *entry;

  dir = opendir(EUX_VIDEO_DIR);
  if (dir == NULL)
    {
      return -1;
    }

  while ((entry = readdir(dir)) != NULL)
    {
      const char *ext = strrchr(entry->d_name, '.');
      if (ext && strcasecmp(ext, ".pcv") == 0)
        {
          snprintf(buf, buflen, EUX_VIDEO_DIR "/%s", entry->d_name);
          closedir(dir);
          return 0;
        }
    }

  closedir(dir);
  return -1;
}

/****************************************************************************
 * Name: pcvideo_frame_timer
 *
 * Description:
 *   LVGL timer callback: drives the frame decode/display pipeline.
 *
 ****************************************************************************/

static void pcvideo_frame_timer(lv_timer_t *timer)
{
  (void)timer;

  if (!g_playing || g_pcv == NULL || g_playback == NULL)
    {
      return;
    }

  /* Check if it's time for the next frame */

  if (!playback_frame_due(g_playback))
    {
      return;
    }

  /* Decode next frame */

  int audio_samples = 0;
  int ret = pcv_read_frame(g_pcv, g_framebuf, g_audiobuf, &audio_samples);

  if (ret <= 0)
    {
      /* End of video */

      g_playing = false;
      playback_stop(g_playback);
      pcvideo_ui_show();
      return;
    }

  g_cur_frame++;

  /* Display the video frame */

  playback_show_frame(g_playback, g_framebuf, 320, 240);

  /* Feed interleaved audio to audio driver */

  if (audio_samples > 0)
    {
      extern int rp23xx_audio_write(const int16_t *samples, size_t count);
      rp23xx_audio_write(g_audiobuf, audio_samples);
    }

  /* Update OSD */

  pcvideo_ui_update(g_cur_frame, g_tot_frame, g_fps);
}

/****************************************************************************
 * Name: pcvideo_key_handler
 ****************************************************************************/

static void pcvideo_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  switch (key)
    {
      case ' ':
        if (g_playing)
          {
            g_playing = false;
            playback_pause(g_playback);
            pcvideo_ui_show();
          }
        else if (g_pcv != NULL)
          {
            g_playing = true;
            playback_play(g_playback);
            pcvideo_ui_hide();
          }
        break;

      case LV_KEY_LEFT:
        /* Seek back 30 frames (~1 sec at 30fps) */
        if (g_pcv && g_cur_frame > 30)
          {
            g_cur_frame -= 30;
            pcv_seek(g_pcv, g_cur_frame);
          }
        break;

      case LV_KEY_RIGHT:
        /* Seek forward 30 frames */
        if (g_pcv && g_cur_frame + 30 < g_tot_frame)
          {
            g_cur_frame += 30;
            pcv_seek(g_pcv, g_cur_frame);
          }
        break;

      case 'q':
      case 'Q':
        g_playing = false;
        if (g_pcv)
          {
            pcv_close(g_pcv);
            g_pcv = NULL;
          }
        pc_app_exit(0);
        break;

      case 'i':
      case 'I':
        /* Toggle OSD */
        if (pcvideo_ui_visible())
          {
            pcvideo_ui_hide();
          }
        else
          {
            pcvideo_ui_show();
          }
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: pcvideo_main
 ****************************************************************************/

static int pcvideo_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();
  char filepath[256];

  /* Create UI overlay */

  pcvideo_ui_create(screen);

  /* Find a .pcv file to play */

  if (argc > 1)
    {
      strncpy(filepath, argv[1], sizeof(filepath) - 1);
      filepath[sizeof(filepath) - 1] = '\0';
    }
  else if (find_first_pcv(filepath, sizeof(filepath)) < 0)
    {
      pcvideo_ui_set_title("No .pcv files in ~/video/");
      return 0;
    }

  /* Open the PCV file */

  g_pcv = pcv_open(filepath);
  if (g_pcv == NULL)
    {
      pcvideo_ui_set_title("Cannot open video file");
      return 0;
    }

  /* Extract title from filename */

  const char *base = strrchr(filepath, '/');
  pcvideo_ui_set_title(base ? base + 1 : filepath);

  /* Allocate frame and audio buffers in PSRAM */

  g_framebuf = (uint16_t *)pc_app_psram_alloc(320 * 240 * 2);
  g_audiobuf = (int16_t *)pc_app_psram_alloc(8192 * sizeof(int16_t));

  if (g_framebuf == NULL || g_audiobuf == NULL)
    {
      pcvideo_ui_set_title("Out of memory");
      pcv_close(g_pcv);
      g_pcv = NULL;
      return 0;
    }

  /* Create playback engine (LVGL canvas for video display) */

  g_playback = playback_create(screen, 320, 240);
  g_cur_frame = 0;
  g_playing   = true;

  /* Start frame timer — run at screen refresh rate (~33ms for 30fps) */

  g_fps = 30;  /* Default, may be overridden by PCV header */
  g_timer = lv_timer_create(pcvideo_frame_timer,
                            1000 / g_fps, NULL);

  playback_play(g_playback);
  pcvideo_ui_hide();

  /* Register keyboard handler */

  lv_obj_add_event_cb(screen, pcvideo_key_handler, LV_EVENT_KEY, NULL);
  lv_group_t *grp = lv_group_get_default();
  if (grp)
    {
      lv_group_add_obj(grp, screen);
    }

  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcvideo_app = {
  .info = {
    .name         = "pcvideo",
    .display_name = "Video Player",
    .version      = "1.0.0",
    .category     = "entertainment",
    .icon         = LV_SYMBOL_VIDEO,
    .min_ram      = 524288,   /* 512 KB for frame buffer + decode */
    .flags        = PC_APP_FLAG_BUILTIN,
  },
  .main    = pcvideo_main,
  .save    = NULL,
  .restore = NULL,
};
