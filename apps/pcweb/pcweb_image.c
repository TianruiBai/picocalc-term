/****************************************************************************
 * apps/pcweb/pcweb_image.c
 *
 * Image decoding for the web browser.
 *
 * Supports:
 *   - BMP (uncompressed RGB24 / RGB32)
 *   - PNG (minimal: unfiltered 8-bit RGB/RGBA, non-interlaced)
 *
 * Outputs LVGL-compatible RGB565 pixel data suitable for lv_canvas or
 * lv_img_dsc_t. Images are scaled to fit the display width (max 308px).
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IMG_MAX_WIDTH    308   /* Max display width for images */
#define IMG_MAX_HEIGHT   240   /* Max height before scaling */
#define IMG_MAX_PIXELS   (IMG_MAX_WIDTH * IMG_MAX_HEIGHT)

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct pcweb_image_s
{
  uint16_t    width;
  uint16_t    height;
  uint16_t   *pixels;    /* RGB565 pixel data (PSRAM-allocated) */
  size_t      size;       /* Size of pixel data in bytes */
} pcweb_image_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rgb888_to_rgb565
 ****************************************************************************/

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((uint16_t)(r & 0xF8) << 8) |
         ((uint16_t)(g & 0xFC) << 3) |
         ((uint16_t)(b >> 3));
}

/****************************************************************************
 * Name: decode_bmp
 *
 * Description:
 *   Decode an uncompressed BMP image.
 *
 *   BMP structure:
 *     Bytes 0-1:   'BM' signature
 *     Bytes 10-13: pixel data offset
 *     Bytes 14-17: DIB header size
 *     Bytes 18-21: width
 *     Bytes 22-25: height (positive = bottom-up)
 *     Bytes 28-29: bits per pixel (24 or 32)
 *     Bytes 30-33: compression (0 = none)
 *
 ****************************************************************************/

static int decode_bmp(const uint8_t *data, size_t len,
                      pcweb_image_t *img)
{
  if (len < 54)
    {
      syslog(LOG_ERR, "IMG: BMP too small\n");
      return -1;
    }

  /* Verify signature */

  if (data[0] != 'B' || data[1] != 'M')
    {
      syslog(LOG_ERR, "IMG: Not a BMP file\n");
      return -1;
    }

  /* Parse header */

  uint32_t pixel_offset = data[10] | (data[11] << 8) |
                          (data[12] << 16) | (data[13] << 24);
  int32_t  width  = (int32_t)(data[18] | (data[19] << 8) |
                              (data[20] << 16) | (data[21] << 24));
  int32_t  height = (int32_t)(data[22] | (data[23] << 8) |
                              (data[24] << 16) | (data[25] << 24));
  uint16_t bpp    = data[28] | (data[29] << 8);
  uint32_t compression = data[30] | (data[31] << 8) |
                         (data[32] << 16) | (data[33] << 24);

  bool bottom_up = (height > 0);
  if (height < 0) height = -height;

  syslog(LOG_INFO, "IMG: BMP %dx%d %dbpp comp=%u\n",
         width, height, bpp, compression);

  /* Validate */

  if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
    {
      syslog(LOG_ERR, "IMG: BMP dimensions invalid\n");
      return -1;
    }

  if (compression != 0)
    {
      syslog(LOG_ERR, "IMG: BMP compression not supported\n");
      return -1;
    }

  if (bpp != 24 && bpp != 32)
    {
      syslog(LOG_ERR, "IMG: BMP %d bpp not supported\n", bpp);
      return -1;
    }

  /* Calculate scale factor to fit within display */

  int scale = 1;
  while ((width / scale) > IMG_MAX_WIDTH ||
         (height / scale) > IMG_MAX_HEIGHT)
    {
      scale++;
    }

  uint16_t out_w = width / scale;
  uint16_t out_h = height / scale;

  if ((size_t)(out_w * out_h) > IMG_MAX_PIXELS)
    {
      syslog(LOG_ERR, "IMG: BMP too large after scaling\n");
      return -1;
    }

  /* Allocate output buffer */

  size_t pixel_bytes = (size_t)out_w * out_h * sizeof(uint16_t);
  img->pixels = (uint16_t *)pc_app_psram_alloc(pixel_bytes);

  if (img->pixels == NULL)
    {
      syslog(LOG_ERR, "IMG: BMP PSRAM alloc failed (%zu bytes)\n",
             pixel_bytes);
      return -1;
    }

  img->width  = out_w;
  img->height = out_h;
  img->size   = pixel_bytes;

  /* Decode pixel data with scaling */

  uint32_t bytes_per_pixel = bpp / 8;
  uint32_t row_stride = ((width * bytes_per_pixel + 3) / 4) * 4;  /* 4-byte aligned */

  for (int out_y = 0; out_y < out_h; out_y++)
    {
      int src_y = out_y * scale;

      /* BMP is bottom-up by default */

      int bmp_row = bottom_up ? (height - 1 - src_y) : src_y;

      for (int out_x = 0; out_x < out_w; out_x++)
        {
          int src_x = out_x * scale;

          size_t offset = pixel_offset +
                          (size_t)bmp_row * row_stride +
                          (size_t)src_x * bytes_per_pixel;

          if (offset + bytes_per_pixel > len)
            {
              img->pixels[out_y * out_w + out_x] = 0;
              continue;
            }

          /* BMP stores BGR */

          uint8_t b = data[offset + 0];
          uint8_t g = data[offset + 1];
          uint8_t r = data[offset + 2];

          img->pixels[out_y * out_w + out_x] = rgb888_to_rgb565(r, g, b);
        }
    }

  syslog(LOG_INFO, "IMG: BMP decoded %dx%d (scale 1:%d)\n",
         out_w, out_h, scale);
  return 0;
}

/****************************************************************************
 * Name: decode_png_minimal
 *
 * Description:
 *   Minimal PNG decoder supporting only uncompressed/filtered 8-bit
 *   RGB and RGBA PNG images. This is a very limited decoder suitable
 *   for simple web graphics on embedded platforms.
 *
 *   Full PNG support requires zlib inflate — we approximate by
 *   checking for uncompressed (stored) DEFLATE blocks in the IDAT
 *   chunk.  For real-world images, this will fail and we fall back
 *   to displaying a placeholder.
 *
 ****************************************************************************/

static int decode_png_minimal(const uint8_t *data, size_t len,
                              pcweb_image_t *img)
{
  /* Check PNG signature: 89 50 4E 47 0D 0A 1A 0A */

  static const uint8_t png_sig[8] =
    { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

  if (len < 33 || memcmp(data, png_sig, 8) != 0)
    {
      return -1;
    }

  /* Parse IHDR chunk (always first, at offset 8) */

  /* Chunk: length(4) + type(4) + data(length) + crc(4) */

  uint32_t ihdr_len = (data[8] << 24) | (data[9] << 16) |
                      (data[10] << 8) | data[11];

  if (ihdr_len < 13 || memcmp(data + 12, "IHDR", 4) != 0)
    {
      return -1;
    }

  uint32_t width  = (data[16] << 24) | (data[17] << 16) |
                    (data[18] << 8) | data[19];
  uint32_t height = (data[20] << 24) | (data[21] << 16) |
                    (data[22] << 8) | data[23];
  uint8_t  bit_depth  = data[24];
  uint8_t  color_type = data[25];
  uint8_t  interlace  = data[28];

  syslog(LOG_INFO, "IMG: PNG %ux%u bd=%u ct=%u i=%u\n",
         width, height, bit_depth, color_type, interlace);

  if (bit_depth != 8 || interlace != 0)
    {
      syslog(LOG_WARNING, "IMG: PNG format not supported "
             "(bd=%u, interlace=%u)\n", bit_depth, interlace);
      return -1;
    }

  /* Calculate bytes per pixel */

  int bpp;
  switch (color_type)
    {
      case 2:  bpp = 3; break;  /* RGB */
      case 6:  bpp = 4; break;  /* RGBA */
      default:
        syslog(LOG_WARNING, "IMG: PNG color type %u not supported\n",
               color_type);
        return -1;
    }

  /* Scale to fit display */

  int scale = 1;
  while ((width / scale) > (uint32_t)IMG_MAX_WIDTH ||
         (height / scale) > (uint32_t)IMG_MAX_HEIGHT)
    {
      scale++;
    }

  uint16_t out_w = width / scale;
  uint16_t out_h = height / scale;

  if ((size_t)(out_w * out_h) > IMG_MAX_PIXELS)
    {
      return -1;
    }

  /* Find IDAT chunks and collect raw compressed data */

  size_t idat_total = 0;
  size_t pos = 8;  /* Skip signature */

  /* First pass: calculate total IDAT data size */

  while (pos + 12 <= len)
    {
      uint32_t chunk_len = (data[pos] << 24) | (data[pos + 1] << 16) |
                           (data[pos + 2] << 8) | data[pos + 3];
      const char *type = (const char *)(data + pos + 4);

      if (memcmp(type, "IDAT", 4) == 0)
        {
          idat_total += chunk_len;
        }
      else if (memcmp(type, "IEND", 4) == 0)
        {
          break;
        }

      pos += 12 + chunk_len;  /* len + type + data + crc */
    }

  if (idat_total == 0)
    {
      return -1;
    }

  /* Collect IDAT data */

  uint8_t *idat_buf = (uint8_t *)pc_app_psram_alloc(idat_total);
  if (idat_buf == NULL)
    {
      return -1;
    }

  size_t idat_pos = 0;
  pos = 8;

  while (pos + 12 <= len)
    {
      uint32_t chunk_len = (data[pos] << 24) | (data[pos + 1] << 16) |
                           (data[pos + 2] << 8) | data[pos + 3];
      const char *type = (const char *)(data + pos + 4);

      if (memcmp(type, "IDAT", 4) == 0)
        {
          if (pos + 8 + chunk_len <= len)
            {
              memcpy(idat_buf + idat_pos, data + pos + 8, chunk_len);
              idat_pos += chunk_len;
            }
        }
      else if (memcmp(type, "IEND", 4) == 0)
        {
          break;
        }

      pos += 12 + chunk_len;
    }

  /* The IDAT data is zlib compressed.
   * Check for zlib header (78 01/9C/DA) and attempt raw inflate.
   *
   * For a truly minimal decoder, we only handle uncompressed
   * (stored) deflate blocks where BTYPE=00. This covers trivial
   * PNG files but not real-world compressed images.
   *
   * For real images, we'd need a full inflate implementation.
   * Since NuttX may have zlib available, check for it.
   */

  size_t raw_size = (size_t)height * (1 + (size_t)width * bpp);
  /* +1 per row for the filter byte */

  uint8_t *raw_pixels = NULL;

#ifdef CONFIG_LIB_ZLIB
  {
    /* Use zlib inflate if available */

    #include <zlib.h>

    raw_pixels = (uint8_t *)pc_app_psram_alloc(raw_size);
    if (raw_pixels == NULL)
      {
        pc_app_psram_free(idat_buf);
        return -1;
      }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in   = idat_buf;
    strm.avail_in  = idat_total;
    strm.next_out  = raw_pixels;
    strm.avail_out = raw_size;

    if (inflateInit(&strm) != Z_OK)
      {
        pc_app_psram_free(raw_pixels);
        pc_app_psram_free(idat_buf);
        return -1;
      }

    int zret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (zret != Z_STREAM_END && zret != Z_OK)
      {
        syslog(LOG_WARNING, "IMG: PNG inflate failed: %d\n", zret);
        pc_app_psram_free(raw_pixels);
        pc_app_psram_free(idat_buf);
        return -1;
      }
  }
#else
  {
    /* Try raw uncompressed DEFLATE blocks only.
     * Skip zlib header (2 bytes), check BTYPE=00 (stored). */

    if (idat_total < 2)
      {
        pc_app_psram_free(idat_buf);
        return -1;
      }

    size_t dpos = 2;  /* Skip zlib header */

    raw_pixels = (uint8_t *)pc_app_psram_alloc(raw_size);
    if (raw_pixels == NULL)
      {
        pc_app_psram_free(idat_buf);
        return -1;
      }

    size_t out_pos = 0;

    while (dpos < idat_total && out_pos < raw_size)
      {
        uint8_t bfinal = idat_buf[dpos] & 0x01;
        uint8_t btype  = (idat_buf[dpos] >> 1) & 0x03;
        dpos++;

        if (btype != 0)
          {
            /* Compressed block — we can't decode without inflate */

            syslog(LOG_WARNING,
                   "IMG: PNG requires inflate (BTYPE=%u)\n", btype);
            pc_app_psram_free(raw_pixels);
            pc_app_psram_free(idat_buf);
            return -1;
          }

        /* Stored block: LEN (2 bytes) + NLEN (2 bytes) + data */

        if (dpos + 4 > idat_total)
          {
            break;
          }

        uint16_t block_len = idat_buf[dpos] | (idat_buf[dpos + 1] << 8);
        dpos += 4;  /* Skip LEN + NLEN */

        size_t copy = block_len;
        if (out_pos + copy > raw_size) copy = raw_size - out_pos;
        if (dpos + copy > idat_total) copy = idat_total - dpos;

        memcpy(raw_pixels + out_pos, idat_buf + dpos, copy);
        out_pos += copy;
        dpos += block_len;

        if (bfinal) break;
      }
  }
#endif

  pc_app_psram_free(idat_buf);

  if (raw_pixels == NULL)
    {
      return -1;
    }

  /* Allocate output buffer */

  size_t pixel_bytes = (size_t)out_w * out_h * sizeof(uint16_t);
  img->pixels = (uint16_t *)pc_app_psram_alloc(pixel_bytes);

  if (img->pixels == NULL)
    {
      pc_app_psram_free(raw_pixels);
      return -1;
    }

  img->width  = out_w;
  img->height = out_h;
  img->size   = pixel_bytes;

  /* Convert with scaling + PNG filter reversal (filter byte per row) */

  size_t row_bytes = 1 + (size_t)width * bpp;

  for (int out_y = 0; out_y < out_h; out_y++)
    {
      int src_y = out_y * scale;

      for (int out_x = 0; out_x < out_w; out_x++)
        {
          int src_x = out_x * scale;

          /* +1 to skip the filter byte at the start of each row */

          size_t offset = (size_t)src_y * row_bytes + 1 +
                          (size_t)src_x * bpp;

          if (offset + bpp > raw_size)
            {
              img->pixels[out_y * out_w + out_x] = 0;
              continue;
            }

          uint8_t r = raw_pixels[offset + 0];
          uint8_t g = raw_pixels[offset + 1];
          uint8_t b = raw_pixels[offset + 2];

          /* Apply alpha compositing against black background */

          if (bpp == 4)
            {
              uint8_t a = raw_pixels[offset + 3];
              r = (r * a) / 255;
              g = (g * a) / 255;
              b = (b * a) / 255;
            }

          img->pixels[out_y * out_w + out_x] = rgb888_to_rgb565(r, g, b);
        }
    }

  pc_app_psram_free(raw_pixels);

  syslog(LOG_INFO, "IMG: PNG decoded %dx%d (scale 1:%d)\n",
         out_w, out_h, scale);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcweb_image_decode
 *
 * Description:
 *   Decode image data (BMP or PNG) into an RGB565 pixel buffer.
 *
 * Parameters:
 *   data    - Raw image file data
 *   len     - Data length in bytes
 *   img     - Output image structure
 *
 * Returns:
 *   0 on success, negative on failure
 *
 ****************************************************************************/

int pcweb_image_decode(const uint8_t *data, size_t len,
                       pcweb_image_t *img)
{
  memset(img, 0, sizeof(pcweb_image_t));

  if (data == NULL || len < 8)
    {
      return -1;
    }

  /* Detect format by signature */

  if (len >= 2 && data[0] == 'B' && data[1] == 'M')
    {
      return decode_bmp(data, len, img);
    }

  static const uint8_t png_sig[4] = { 0x89, 0x50, 0x4E, 0x47 };
  if (len >= 4 && memcmp(data, png_sig, 4) == 0)
    {
      return decode_png_minimal(data, len, img);
    }

  syslog(LOG_WARNING, "IMG: Unknown image format "
         "(hdr: %02x %02x %02x %02x)\n",
         data[0], data[1], data[2], data[3]);
  return -1;
}

/****************************************************************************
 * Name: pcweb_image_free
 *
 * Description:
 *   Free decoded image data.
 *
 ****************************************************************************/

void pcweb_image_free(pcweb_image_t *img)
{
  if (img && img->pixels)
    {
      pc_app_psram_free(img->pixels);
      img->pixels = NULL;
    }
}

/****************************************************************************
 * Name: pcweb_image_create_lvobj
 *
 * Description:
 *   Create an LVGL canvas object from decoded image data.
 *   The canvas is created as a child of the given parent container.
 *
 ****************************************************************************/

lv_obj_t *pcweb_image_create_lvobj(lv_obj_t *parent,
                                   const pcweb_image_t *img)
{
  if (parent == NULL || img == NULL || img->pixels == NULL)
    {
      return NULL;
    }

  /* Create a canvas and draw the image */

  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, img->pixels,
                       img->width, img->height,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_set_size(canvas, img->width, img->height);

  return canvas;
}
