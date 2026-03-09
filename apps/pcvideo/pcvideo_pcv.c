/****************************************************************************
 * apps/pcvideo/pcvideo_pcv.c
 *
 * .pcv format parser.
 * See docs/PCV Format Spec.md for format details.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* PCV file header (32 bytes) */

typedef struct __attribute__((packed)) pcv_header_s
{
  char     magic[4];         /* "PCV1" */
  uint16_t width;
  uint16_t height;
  uint8_t  fps;
  uint8_t  audio_rate_div;   /* sample_rate = audio_rate_div * 1000 */
  uint8_t  audio_channels;
  uint8_t  flags;            /* bit0: RLE, bit1: has_audio, bit2: has_index */
  uint32_t frame_count;
  uint32_t index_offset;     /* File offset to index table (0 = no index) */
  uint8_t  reserved[12];
} pcv_header_t;

#define PCV_FLAG_RLE        (1 << 0)
#define PCV_FLAG_AUDIO      (1 << 1)
#define PCV_FLAG_INDEX      (1 << 2)

typedef struct pcv_file_s
{
  FILE        *fp;
  pcv_header_t header;
  uint32_t     current_frame;
  uint32_t     data_offset;      /* Offset past header to first frame */
  uint32_t    *index_table;      /* Frame offsets for seeking */
  uint16_t    *frame_buf;        /* Decoded frame buffer (PSRAM) */
  int16_t     *audio_buf;        /* Per-frame audio chunk (PSRAM) */
  size_t       frame_pixels;     /* width × height */
} pcv_file_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Decode an RLE-compressed frame.
 *
 * RLE encoding:
 *   Byte with bit7=0: run of (count+1) pixels, followed by 2-byte pixel
 *   Byte with bit7=1: (count+1) literal pixels, followed by count×2 bytes
 */
static int pcv_decode_rle(const uint8_t *src, size_t src_len,
                          uint16_t *dst, size_t max_pixels)
{
  size_t si = 0;
  size_t di = 0;

  while (si < src_len && di < max_pixels)
    {
      uint8_t tag = src[si++];
      int count = (tag & 0x7F) + 1;

      if (tag & 0x80)
        {
          /* Literal run */

          for (int i = 0; i < count && di < max_pixels && si + 1 < src_len; i++)
            {
              dst[di++] = src[si] | (src[si + 1] << 8);
              si += 2;
            }
        }
      else
        {
          /* Repeated pixel */

          if (si + 1 >= src_len) break;
          uint16_t pixel = src[si] | (src[si + 1] << 8);
          si += 2;

          for (int i = 0; i < count && di < max_pixels; i++)
            {
              dst[di++] = pixel;
            }
        }
    }

  return (int)di;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Open a .pcv video file.
 */
pcv_file_t *pcv_open(const char *path)
{
  pcv_file_t *pcv;

  pcv = (pcv_file_t *)pc_app_psram_alloc(sizeof(pcv_file_t));
  if (pcv == NULL) return NULL;

  memset(pcv, 0, sizeof(pcv_file_t));

  pcv->fp = fopen(path, "rb");
  if (pcv->fp == NULL)
    {
      pc_app_psram_free(pcv);
      return NULL;
    }

  /* Read header */

  if (fread(&pcv->header, 1, sizeof(pcv_header_t), pcv->fp)
      != sizeof(pcv_header_t))
    {
      fclose(pcv->fp);
      pc_app_psram_free(pcv);
      return NULL;
    }

  /* Validate magic */

  if (memcmp(pcv->header.magic, "PCV1", 4) != 0)
    {
      syslog(LOG_ERR, "PCVIDEO: Invalid PCV magic\n");
      fclose(pcv->fp);
      pc_app_psram_free(pcv);
      return NULL;
    }

  pcv->data_offset  = sizeof(pcv_header_t);
  pcv->frame_pixels = pcv->header.width * pcv->header.height;

  /* Allocate frame decode buffer */

  pcv->frame_buf = (uint16_t *)pc_app_psram_alloc(
    pcv->frame_pixels * sizeof(uint16_t));
  if (pcv->frame_buf == NULL)
    {
      fclose(pcv->fp);
      pc_app_psram_free(pcv);
      return NULL;
    }

  /* Allocate audio buffer if needed */

  if (pcv->header.flags & PCV_FLAG_AUDIO)
    {
      int audio_rate    = pcv->header.audio_rate_div * 1000;
      int samples_frame = audio_rate / pcv->header.fps;
      pcv->audio_buf = (int16_t *)pc_app_psram_alloc(
        samples_frame * pcv->header.audio_channels * sizeof(int16_t));
    }

  /* Load index table if available */

  if ((pcv->header.flags & PCV_FLAG_INDEX) && pcv->header.index_offset > 0)
    {
      pcv->index_table = (uint32_t *)pc_app_psram_alloc(
        pcv->header.frame_count * sizeof(uint32_t));

      if (pcv->index_table)
        {
          fseek(pcv->fp, pcv->header.index_offset, SEEK_SET);
          fread(pcv->index_table, sizeof(uint32_t),
                pcv->header.frame_count, pcv->fp);
          fseek(pcv->fp, pcv->data_offset, SEEK_SET);
        }
    }

  syslog(LOG_INFO, "PCVIDEO: Opened %dx%d @%dfps, %lu frames, "
         "flags=0x%02x\n",
         pcv->header.width, pcv->header.height,
         pcv->header.fps, (unsigned long)pcv->header.frame_count,
         pcv->header.flags);

  return pcv;
}

/**
 * Read the next frame. Returns pixel data in frame_buf.
 *
 * @param pcv        PCV file context
 * @param out_pixels Output: pointer to decoded pixel data (RGB565)
 * @param out_audio  Output: pointer to audio data (NULL if no audio)
 * @param out_audio_len Output: audio data length in samples
 * @return 0 on success, -1 on EOF or error
 */
int pcv_read_frame(pcv_file_t *pcv, uint16_t **out_pixels,
                   int16_t **out_audio, int *out_audio_len)
{
  if (pcv->current_frame >= pcv->header.frame_count)
    {
      return -1;  /* EOF */
    }

  /* Read frame size (4 bytes) */

  uint32_t frame_size;
  if (fread(&frame_size, 4, 1, pcv->fp) != 1)
    {
      return -1;
    }

  /* Read frame pixel data */

  if (pcv->header.flags & PCV_FLAG_RLE)
    {
      /* RLE compressed: read compressed data, then decode */

      uint8_t *comp_buf = (uint8_t *)pc_app_psram_alloc(frame_size);
      if (comp_buf == NULL) return -1;

      fread(comp_buf, 1, frame_size, pcv->fp);
      pcv_decode_rle(comp_buf, frame_size,
                     pcv->frame_buf, pcv->frame_pixels);
      pc_app_psram_free(comp_buf);
    }
  else
    {
      /* Raw RGB565 */

      fread(pcv->frame_buf, sizeof(uint16_t),
            pcv->frame_pixels, pcv->fp);
    }

  *out_pixels = pcv->frame_buf;

  /* Read audio chunk if present */

  if (pcv->header.flags & PCV_FLAG_AUDIO)
    {
      uint16_t audio_size;
      fread(&audio_size, 2, 1, pcv->fp);

      if (audio_size > 0 && pcv->audio_buf)
        {
          fread(pcv->audio_buf, 1, audio_size, pcv->fp);
          *out_audio = pcv->audio_buf;
          *out_audio_len = audio_size /
                           (pcv->header.audio_channels * sizeof(int16_t));
        }
      else
        {
          *out_audio = NULL;
          *out_audio_len = 0;
        }
    }
  else
    {
      *out_audio = NULL;
      *out_audio_len = 0;
    }

  pcv->current_frame++;
  return 0;
}

/**
 * Seek to a specific frame.
 */
int pcv_seek(pcv_file_t *pcv, uint32_t frame)
{
  if (frame >= pcv->header.frame_count) return -1;

  if (pcv->index_table)
    {
      fseek(pcv->fp, pcv->index_table[frame], SEEK_SET);
      pcv->current_frame = frame;
      return 0;
    }

  /* Without index, can only rewind to start */

  if (frame == 0)
    {
      fseek(pcv->fp, pcv->data_offset, SEEK_SET);
      pcv->current_frame = 0;
      return 0;
    }

  return -1;  /* Cannot seek without index */
}

/**
 * Close the PCV file and free resources.
 */
void pcv_close(pcv_file_t *pcv)
{
  if (pcv == NULL) return;

  if (pcv->fp)          fclose(pcv->fp);
  if (pcv->frame_buf)   pc_app_psram_free(pcv->frame_buf);
  if (pcv->audio_buf)   pc_app_psram_free(pcv->audio_buf);
  if (pcv->index_table) pc_app_psram_free(pcv->index_table);
  pc_app_psram_free(pcv);
}
