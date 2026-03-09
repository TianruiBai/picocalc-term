/****************************************************************************
 * apps/pcaudio/pcaudio_decoder.c
 *
 * Audio decoder: minimp3 for MP3, built-in WAV parser.
 * Decodes to raw PCM samples for the PWM audio driver.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "pcterm/app.h"

/* minimp3 single-header decoder */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  AUDIO_FORMAT_UNKNOWN,
  AUDIO_FORMAT_MP3,
  AUDIO_FORMAT_WAV,
} audio_format_t;

typedef struct audio_decoder_s
{
  audio_format_t format;
  FILE          *file;
  uint32_t       sample_rate;
  uint8_t        channels;
  uint8_t        bits_per_sample;
  uint32_t       total_samples;
  uint32_t       current_sample;
  uint32_t       data_offset;     /* WAV: start of PCM data */
  uint32_t       data_size;       /* WAV: size of PCM data */

  /* MP3 decoder state */
  mp3dec_t       mp3d;
  uint8_t       *file_buf;
  size_t         file_buf_size;
  size_t         file_buf_pos;
} audio_decoder_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static audio_format_t detect_format(const char *path)
{
  const char *ext = strrchr(path, '.');
  if (ext == NULL) return AUDIO_FORMAT_UNKNOWN;

  if (strcasecmp(ext, ".mp3") == 0) return AUDIO_FORMAT_MP3;
  if (strcasecmp(ext, ".wav") == 0) return AUDIO_FORMAT_WAV;

  return AUDIO_FORMAT_UNKNOWN;
}

static int wav_parse_header(audio_decoder_t *dec)
{
  uint8_t hdr[44];

  if (fread(hdr, 1, 44, dec->file) != 44)
    {
      return -1;
    }

  /* Verify RIFF header */

  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    {
      return -1;
    }

  /* Parse fmt chunk (offset 20) */

  uint16_t audio_format = hdr[20] | (hdr[21] << 8);
  if (audio_format != 1)  /* PCM only */
    {
      syslog(LOG_ERR, "AUDIO: WAV not PCM (format=%d)\n", audio_format);
      return -1;
    }

  dec->channels        = hdr[22] | (hdr[23] << 8);
  dec->sample_rate     = hdr[24] | (hdr[25] << 8) |
                         (hdr[26] << 16) | (hdr[27] << 24);
  dec->bits_per_sample = hdr[34] | (hdr[35] << 8);

  /* Data chunk (offset 40) */

  dec->data_size   = hdr[40] | (hdr[41] << 8) |
                     (hdr[42] << 16) | (hdr[43] << 24);
  dec->data_offset = 44;

  dec->total_samples = dec->data_size /
                       (dec->channels * (dec->bits_per_sample / 8));

  syslog(LOG_INFO, "AUDIO: WAV %luHz %dch %dbit %lu samples\n",
         (unsigned long)dec->sample_rate, dec->channels,
         dec->bits_per_sample, (unsigned long)dec->total_samples);

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Open an audio file and initialize the decoder.
 */
audio_decoder_t *audio_decoder_open(const char *path)
{
  audio_decoder_t *dec;

  dec = (audio_decoder_t *)pc_app_psram_alloc(sizeof(audio_decoder_t));
  if (dec == NULL) return NULL;

  memset(dec, 0, sizeof(audio_decoder_t));
  dec->format = detect_format(path);

  if (dec->format == AUDIO_FORMAT_UNKNOWN)
    {
      syslog(LOG_ERR, "AUDIO: Unknown format: %s\n", path);
      pc_app_psram_free(dec);
      return NULL;
    }

  dec->file = fopen(path, "rb");
  if (dec->file == NULL)
    {
      pc_app_psram_free(dec);
      return NULL;
    }

  if (dec->format == AUDIO_FORMAT_WAV)
    {
      if (wav_parse_header(dec) < 0)
        {
          fclose(dec->file);
          pc_app_psram_free(dec);
          return NULL;
        }
    }
  else if (dec->format == AUDIO_FORMAT_MP3)
    {
      /* Initialize minimp3 decoder */

      mp3dec_init(&dec->mp3d);

      /* Read entire file into PSRAM buffer for streaming decode */

      fseek(dec->file, 0, SEEK_END);
      dec->file_buf_size = ftell(dec->file);
      fseek(dec->file, 0, SEEK_SET);

      dec->file_buf = (uint8_t *)pc_app_psram_alloc(dec->file_buf_size);
      if (dec->file_buf == NULL)
        {
          syslog(LOG_ERR, "AUDIO: Cannot alloc MP3 buf %zu\n",
                 dec->file_buf_size);
          fclose(dec->file);
          pc_app_psram_free(dec);
          return NULL;
        }

      size_t got = fread(dec->file_buf, 1, dec->file_buf_size, dec->file);
      if (got < dec->file_buf_size)
        {
          dec->file_buf_size = got;
        }

      dec->file_buf_pos = 0;

      /* Decode first frame to get sample rate and channel info */

      mp3dec_frame_info_t info;
      int16_t pcm_tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];

      int samples = mp3dec_decode_frame(&dec->mp3d,
                       dec->file_buf, dec->file_buf_size,
                       pcm_tmp, &info);

      if (info.frame_bytes > 0)
        {
          dec->sample_rate     = info.hz;
          dec->channels        = info.channels;
          dec->bits_per_sample = 16;

          /* Estimate total samples from bitrate */

          if (info.bitrate_kbps > 0)
            {
              dec->total_samples =
                (dec->file_buf_size * 8UL) /
                (info.bitrate_kbps * 1000UL) * dec->sample_rate;
            }
        }
      else
        {
          dec->sample_rate     = 44100;
          dec->channels        = 2;
          dec->bits_per_sample = 16;
        }

      /* Reset position — don't consume the first frame */

      dec->file_buf_pos = 0;
      mp3dec_init(&dec->mp3d);

      syslog(LOG_INFO, "AUDIO: MP3 %luHz %dch %zu bytes\n",
             (unsigned long)dec->sample_rate, dec->channels,
             dec->file_buf_size);
    }

  return dec;
}

/**
 * Decode the next chunk of audio samples.
 *
 * @param dec     Decoder context
 * @param buf     Output buffer (int16_t samples)
 * @param frames  Number of frames to decode
 * @return Number of frames actually decoded, 0 on EOF
 */
int audio_decoder_read(audio_decoder_t *dec, int16_t *buf, int frames)
{
  if (dec == NULL || dec->file == NULL) return 0;

  if (dec->format == AUDIO_FORMAT_WAV)
    {
      size_t frame_bytes = dec->channels * (dec->bits_per_sample / 8);
      size_t want = frames * frame_bytes;
      size_t remaining = dec->data_size -
                         (dec->current_sample * frame_bytes);

      if (want > remaining) want = remaining;

      size_t got = fread(buf, 1, want, dec->file);
      int decoded = got / frame_bytes;
      dec->current_sample += decoded;

      return decoded;
    }
  else if (dec->format == AUDIO_FORMAT_MP3)
    {
      /* Use minimp3 to decode frames */

      int total_decoded = 0;

      while (total_decoded < frames &&
             dec->file_buf_pos < dec->file_buf_size)
        {
          mp3dec_frame_info_t info;
          int samples = mp3dec_decode_frame(
                          &dec->mp3d,
                          dec->file_buf + dec->file_buf_pos,
                          dec->file_buf_size - dec->file_buf_pos,
                          buf + total_decoded * dec->channels,
                          &info);

          if (info.frame_bytes == 0)
            {
              break;  /* No more valid frames */
            }

          dec->file_buf_pos += info.frame_bytes;

          if (samples > 0)
            {
              total_decoded += samples;
              dec->current_sample += samples;
            }
        }

      return total_decoded;
    }

  return 0;
}

/**
 * Seek to a position in the audio file.
 *
 * @param dec     Decoder
 * @param sample  Target sample position
 */
int audio_decoder_seek(audio_decoder_t *dec, uint32_t sample)
{
  if (dec == NULL) return -1;

  if (dec->format == AUDIO_FORMAT_WAV)
    {
      size_t frame_bytes = dec->channels * (dec->bits_per_sample / 8);
      fseek(dec->file, dec->data_offset + sample * frame_bytes, SEEK_SET);
      dec->current_sample = sample;
      return 0;
    }

  /* MP3 seeking: reset decoder and scan frames to target */

  if (dec->format == AUDIO_FORMAT_MP3 && dec->file_buf != NULL)
    {
      mp3dec_init(&dec->mp3d);
      dec->file_buf_pos   = 0;
      dec->current_sample = 0;

      /* Skip frames until we reach the target sample */

      while (dec->current_sample < sample &&
             dec->file_buf_pos < dec->file_buf_size)
        {
          mp3dec_frame_info_t info;
          int16_t dummy[MINIMP3_MAX_SAMPLES_PER_FRAME];

          int samples = mp3dec_decode_frame(
                          &dec->mp3d,
                          dec->file_buf + dec->file_buf_pos,
                          dec->file_buf_size - dec->file_buf_pos,
                          dummy, &info);

          if (info.frame_bytes == 0) break;

          dec->file_buf_pos += info.frame_bytes;
          if (samples > 0)
            {
              dec->current_sample += samples;
            }
        }

      return 0;
    }

  return -1;
}

/**
 * Close the decoder and free resources.
 */
void audio_decoder_close(audio_decoder_t *dec)
{
  if (dec == NULL) return;

  if (dec->file) fclose(dec->file);
  if (dec->file_buf) pc_app_psram_free(dec->file_buf);
  pc_app_psram_free(dec);
}

/**
 * Get decoder info.
 */
uint32_t audio_decoder_sample_rate(const audio_decoder_t *dec)
{
  return dec ? dec->sample_rate : 0;
}

uint32_t audio_decoder_total_samples(const audio_decoder_t *dec)
{
  return dec ? dec->total_samples : 0;
}

uint32_t audio_decoder_current_sample(const audio_decoder_t *dec)
{
  return dec ? dec->current_sample : 0;
}
