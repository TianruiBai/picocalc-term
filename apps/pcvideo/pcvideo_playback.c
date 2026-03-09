/****************************************************************************
 * apps/pcvideo/pcvideo_playback.c
 *
 * Video playback pipeline: frame timing, audio sync, display output.
 * Coordinates SD read → decode → SPI DMA pipeline.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include <lvgl/lvgl.h>
#include "pcterm/app.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  PLAYBACK_IDLE,
  PLAYBACK_PLAYING,
  PLAYBACK_PAUSED,
  PLAYBACK_FINISHED,
} playback_state_t;

typedef struct video_playback_s
{
  playback_state_t state;
  uint32_t         frame_interval_us;  /* Microseconds per frame */
  uint32_t         frames_played;
  uint32_t         frames_dropped;
  struct timespec  last_frame_time;

  /* Frame display */

  lv_obj_t        *canvas;
  lv_color_t      *canvas_buf;
  uint16_t         width;
  uint16_t         height;
} video_playback_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t timespec_to_us(const struct timespec *ts)
{
  return (uint64_t)ts->tv_sec * 1000000 + ts->tv_nsec / 1000;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

video_playback_t *playback_create(lv_obj_t *parent,
                                  uint16_t width, uint16_t height,
                                  uint8_t fps)
{
  video_playback_t *pb;

  pb = (video_playback_t *)pc_app_psram_alloc(sizeof(video_playback_t));
  if (pb == NULL) return NULL;

  memset(pb, 0, sizeof(video_playback_t));

  pb->width  = width;
  pb->height = height;
  pb->frame_interval_us = 1000000 / fps;
  pb->state  = PLAYBACK_IDLE;

  /* Create LVGL canvas for frame display */

  size_t buf_size = sizeof(lv_color_t) * width * height;
  pb->canvas_buf = (lv_color_t *)pc_app_psram_alloc(buf_size);
  if (pb->canvas_buf == NULL)
    {
      pc_app_psram_free(pb);
      return NULL;
    }

  pb->canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(pb->canvas, pb->canvas_buf,
                       width, height, LV_COLOR_FORMAT_RGB565);
  lv_obj_center(pb->canvas);

  return pb;
}

void playback_destroy(video_playback_t *pb)
{
  if (pb == NULL) return;

  if (pb->canvas) lv_obj_delete(pb->canvas);
  if (pb->canvas_buf) pc_app_psram_free(pb->canvas_buf);
  pc_app_psram_free(pb);
}

/**
 * Display a decoded frame on the canvas.
 * The pixel data is already in RGB565 format (from pcv_read_frame).
 */
void playback_show_frame(video_playback_t *pb, const uint16_t *pixels)
{
  if (pb == NULL || pb->canvas == NULL) return;

  /* Copy RGB565 pixels to LVGL canvas buffer */

  memcpy(pb->canvas_buf, pixels,
         sizeof(lv_color_t) * pb->width * pb->height);

  lv_obj_invalidate(pb->canvas);
  pb->frames_played++;
}

/**
 * Check if it's time to display the next frame.
 * Implements frame pacing to maintain target FPS.
 *
 * @return true if a new frame should be displayed
 */
bool playback_frame_due(video_playback_t *pb)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (pb->state != PLAYBACK_PLAYING)
    {
      return false;
    }

  uint64_t now_us   = timespec_to_us(&now);
  uint64_t last_us  = timespec_to_us(&pb->last_frame_time);
  uint64_t elapsed  = now_us - last_us;

  if (elapsed >= pb->frame_interval_us)
    {
      /* Check for frame drop */

      if (elapsed > pb->frame_interval_us * 2)
        {
          pb->frames_dropped++;
        }

      pb->last_frame_time = now;
      return true;
    }

  return false;
}

void playback_play(video_playback_t *pb)
{
  if (pb)
    {
      pb->state = PLAYBACK_PLAYING;
      clock_gettime(CLOCK_MONOTONIC, &pb->last_frame_time);
    }
}

void playback_pause(video_playback_t *pb)
{
  if (pb) pb->state = PLAYBACK_PAUSED;
}

void playback_stop(video_playback_t *pb)
{
  if (pb) pb->state = PLAYBACK_IDLE;
}

playback_state_t playback_get_state(const video_playback_t *pb)
{
  return pb ? pb->state : PLAYBACK_IDLE;
}
