/****************************************************************************
 * apps/pcaudio/minimp3.h
 *
 * Minimal stub for minimp3 library types and functions.
 * Provides API compatibility so pcaudio_decoder.c compiles.
 * Audio decoding is a no-op; real minimp3 can replace this later.
 *
 ****************************************************************************/

#ifndef MINIMP3_H
#define MINIMP3_H

#include <stdint.h>
#include <string.h>

#define MINIMP3_MAX_SAMPLES_PER_FRAME 1152

typedef struct
{
  int bitrate_kbps;
  int frame_bytes;
  int channels;
  int hz;
  int layer;
  int frame_offset;
} mp3dec_frame_info_t;

typedef struct
{
  float mdct_overlap[2][9 * 32];
  float qmf_state[15 * 2 * 32];
  int   reserv;
  int   free_format_bytes;
  unsigned char header[4];
  unsigned char reserv_buf[511];
} mp3dec_t;

#ifdef MINIMP3_IMPLEMENTATION

static inline void mp3dec_init(mp3dec_t *dec)
{
  if (dec)
    {
      memset(dec, 0, sizeof(*dec));
    }
}

static inline int mp3dec_decode_frame(mp3dec_t *dec,
                                      const uint8_t *mp3,
                                      int mp3_bytes,
                                      int16_t *pcm,
                                      mp3dec_frame_info_t *info)
{
  (void)dec;
  (void)mp3;
  (void)mp3_bytes;
  (void)pcm;

  if (info)
    {
      memset(info, 0, sizeof(*info));
    }

  /* Stub: no frames decoded */

  return 0;
}

#endif /* MINIMP3_IMPLEMENTATION */

#endif /* MINIMP3_H */
