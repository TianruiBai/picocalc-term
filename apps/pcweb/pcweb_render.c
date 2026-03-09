/****************************************************************************
 * apps/pcweb/pcweb_render.c
 *
 * Page renderer: converts parsed HTML nodes into LVGL widgets.
 * Supports text (labels with recolor) and inline images (canvas).
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RENDER_WIDTH_CHARS   50    /* Characters per line */
#define RENDER_MAX_OUTPUT    65536 /* Max rendered text size */
#define RENDER_MAX_IMAGES    16    /* Max inline images per page */

/****************************************************************************
 * External References
 ****************************************************************************/

/* pcweb_image.c */

typedef struct pcweb_image_s
{
  uint16_t    width;
  uint16_t    height;
  uint16_t   *pixels;
  size_t      size;
} pcweb_image_t;

extern int       pcweb_image_decode(const uint8_t *data, size_t len,
                                    pcweb_image_t *img);
extern void      pcweb_image_free(pcweb_image_t *img);
extern lv_obj_t *pcweb_image_create_lvobj(lv_obj_t *parent,
                                          const pcweb_image_t *img);

/* pcweb_http.c */

typedef struct http_response_s http_response_t;
extern int  http_get(const char *url, http_response_t *resp);
extern void http_response_free(http_response_t *resp);

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Rendered page: a mix of text labels and image canvases */

typedef struct render_item_s
{
  lv_obj_t *obj;         /* LVGL object (label or canvas) */
  bool      is_image;    /* True if image canvas */
  pcweb_image_t img_data; /* Image pixel data (if is_image) */
  struct render_item_s *next;
} render_item_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Render parsed HTML nodes into LVGL widgets within a container.
 * Creates mixed text labels and image canvases for inline layout.
 *
 * @param container  Parent LVGL container (scrollable)
 * @param nodes_ptr  Linked list of parsed HTML nodes
 * @param base_url   Base URL for resolving relative image paths
 * @return Number of items rendered, or -1 on error
 */
int pcweb_render_to_container(lv_obj_t *container, void *nodes_ptr,
                              const char *base_url)
{
  typedef enum
  {
    HTML_TEXT_,
    HTML_HEADING_,
    HTML_PARAGRAPH_,
    HTML_LINK_,
    HTML_LIST_ITEM_,
    HTML_PREFORMAT_,
    HTML_LINE_BREAK_,
    HTML_IMAGE_,
  } node_type_t;

  typedef struct node_s
  {
    int          type;
    char        *text;
    char        *href;
    char        *src;
    char        *alt;
    int          level;
    struct node_s *next;
  } node_t;

  node_t *nodes = (node_t *)nodes_ptr;

  if (container == NULL || nodes == NULL)
    {
      return -1;
    }

  /* Accumulate text in a buffer; flush to label when we encounter
   * an image or reach buffer limit */

  char *text_buf = (char *)pc_app_psram_alloc(RENDER_MAX_OUTPUT);
  if (text_buf == NULL) return -1;

  size_t text_pos = 0;
  int item_count = 0;
  int image_count = 0;

  /* Helper: flush accumulated text to a new label */

  #define FLUSH_TEXT() do { \
    if (text_pos > 0) { \
      text_buf[text_pos] = '\0'; \
      lv_obj_t *lbl = lv_label_create(container); \
      lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP); \
      lv_obj_set_width(lbl, 308); \
      lv_obj_set_style_text_color(lbl, lv_color_white(), 0); \
      lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0); \
      lv_label_set_text(lbl, text_buf); \
      text_pos = 0; \
      item_count++; \
    } \
  } while (0)

  node_t *n = nodes;

  while (n)
    {
      switch (n->type)
        {
          case 0: /* HTML_TEXT */
            if (n->text && text_pos < RENDER_MAX_OUTPUT - 256)
              {
                size_t tlen = strlen(n->text);
                if (text_pos + tlen < RENDER_MAX_OUTPUT - 1)
                  {
                    memcpy(text_buf + text_pos, n->text, tlen);
                    text_pos += tlen;
                  }
              }
            break;

          case 1: /* HTML_HEADING */
            {
              if (text_pos > 0 && text_pos < RENDER_MAX_OUTPUT - 4)
                {
                  text_buf[text_pos++] = '\n';
                }

              text_buf[text_pos++] = '\n';

              if (n->text)
                {
                  int added = snprintf(text_buf + text_pos,
                                       RENDER_MAX_OUTPUT - text_pos,
                                       "#FFFFFF %s#\n", n->text);
                  if (added > 0) text_pos += added;
                }
            }
            break;

          case 3: /* HTML_LINK */
            if (n->text && n->href)
              {
                int added = snprintf(text_buf + text_pos,
                                     RENDER_MAX_OUTPUT - text_pos,
                                     "#0077CC [%s]#", n->text);
                if (added > 0) text_pos += added;
              }
            else if (n->href)
              {
                int added = snprintf(text_buf + text_pos,
                                     RENDER_MAX_OUTPUT - text_pos,
                                     "#0077CC [%s]#", n->href);
                if (added > 0) text_pos += added;
              }
            break;

          case 4: /* HTML_LIST_ITEM */
            if (text_pos > 0 && text_pos < RENDER_MAX_OUTPUT - 4)
              {
                text_buf[text_pos++] = '\n';
              }

            text_buf[text_pos++] = ' ';
            text_buf[text_pos++] = '-';
            text_buf[text_pos++] = ' ';
            break;

          case 6: /* HTML_LINE_BREAK */
            if (text_pos < RENDER_MAX_OUTPUT - 2)
              {
                text_buf[text_pos++] = '\n';
              }
            break;

          case 7: /* HTML_IMAGE */
            if (n->src && image_count < RENDER_MAX_IMAGES)
              {
                /* Flush any pending text first */

                FLUSH_TEXT();

                /* Resolve URL (handle relative paths) */

                char img_url[512];
                if (n->src[0] == '/' && base_url != NULL)
                  {
                    /* Absolute path relative to host */

                    const char *host_end = strstr(base_url, "://");
                    if (host_end)
                      {
                        host_end += 3;
                        const char *path = strchr(host_end, '/');
                        size_t hlen = path ?
                          (size_t)(path - base_url) : strlen(base_url);
                        snprintf(img_url, sizeof(img_url),
                                 "%.*s%s", (int)hlen, base_url, n->src);
                      }
                    else
                      {
                        strncpy(img_url, n->src, sizeof(img_url) - 1);
                      }
                  }
                else if (strncmp(n->src, "http", 4) != 0 &&
                         base_url != NULL)
                  {
                    /* Relative path */

                    const char *last_slash = strrchr(base_url, '/');
                    if (last_slash && last_slash > base_url + 8)
                      {
                        size_t base_len = last_slash - base_url + 1;
                        snprintf(img_url, sizeof(img_url),
                                 "%.*s%s", (int)base_len,
                                 base_url, n->src);
                      }
                    else
                      {
                        snprintf(img_url, sizeof(img_url),
                                 "%s/%s", base_url, n->src);
                      }
                  }
                else
                  {
                    strncpy(img_url, n->src, sizeof(img_url) - 1);
                  }

                img_url[sizeof(img_url) - 1] = '\0';

                syslog(LOG_INFO, "RENDER: Fetching image: %s\n",
                       img_url);

                /* Download image */

                typedef struct
                {
                  int    status_code;
                  char   content_type[64];
                  char  *body;
                  size_t body_len;
                  char   redirect_url[512];
                } resp_t;

                resp_t resp;
                memset(&resp, 0, sizeof(resp));

                if (http_get(img_url, (http_response_t *)&resp) == 0 &&
                    resp.body != NULL && resp.body_len > 8)
                  {
                    pcweb_image_t img;

                    if (pcweb_image_decode(
                          (const uint8_t *)resp.body,
                          resp.body_len, &img) == 0)
                      {
                        /* Create LVGL canvas for the image */

                        lv_obj_t *canvas =
                          pcweb_image_create_lvobj(container, &img);

                        if (canvas)
                          {
                            item_count++;
                            image_count++;
                            syslog(LOG_INFO,
                                   "RENDER: Image %dx%d displayed\n",
                                   img.width, img.height);
                          }
                        else
                          {
                            pcweb_image_free(&img);
                          }

                        /* Note: canvas owns the pixel buffer now */
                      }
                    else
                      {
                        /* Decode failed — show alt text placeholder */

                        int added = snprintf(
                          text_buf + text_pos,
                          RENDER_MAX_OUTPUT - text_pos,
                          "\n#888888 [img: %s]#\n",
                          n->alt ? n->alt :
                            (n->text ? n->text : "?"));
                        if (added > 0) text_pos += added;
                      }

                    http_response_free((http_response_t *)&resp);
                  }
                else
                  {
                    /* Download failed — show placeholder */

                    int added = snprintf(
                      text_buf + text_pos,
                      RENDER_MAX_OUTPUT - text_pos,
                      "\n#888888 [img: %s]#\n",
                      n->alt ? n->alt :
                        (n->text ? n->text : "?"));
                    if (added > 0) text_pos += added;

                    if (resp.body)
                      {
                        http_response_free((http_response_t *)&resp);
                      }
                  }
              }
            break;

          default:
            if (n->text && text_pos < RENDER_MAX_OUTPUT - 256)
              {
                size_t tlen = strlen(n->text);
                if (text_pos + tlen < RENDER_MAX_OUTPUT - 1)
                  {
                    memcpy(text_buf + text_pos, n->text, tlen);
                    text_pos += tlen;
                  }
              }
            break;
        }

      n = n->next;
    }

  /* Flush remaining text */

  FLUSH_TEXT();

  #undef FLUSH_TEXT

  pc_app_psram_free(text_buf);

  syslog(LOG_INFO, "RENDER: %d items (%d images)\n",
         item_count, image_count);
  return item_count;
}

/**
 * Legacy interface: render nodes to a single text string (no images).
 * Used when the container approach is not needed.
 */
char *pcweb_render(void *nodes_ptr)
{
  typedef struct node_s
  {
    int          type;
    char        *text;
    char        *href;
    char        *src;
    char        *alt;
    int          level;
    struct node_s *next;
  } node_t;

  node_t *nodes = (node_t *)nodes_ptr;
  char *output = (char *)pc_app_psram_alloc(RENDER_MAX_OUTPUT);
  if (output == NULL) return NULL;

  size_t pos = 0;
  node_t *n = nodes;

  while (n && pos < RENDER_MAX_OUTPUT - 256)
    {
      switch (n->type)
        {
          case 0: /* HTML_TEXT */
            if (n->text)
              {
                size_t tlen = strlen(n->text);
                if (pos + tlen < RENDER_MAX_OUTPUT - 1)
                  {
                    memcpy(output + pos, n->text, tlen);
                    pos += tlen;
                  }
              }
            break;

          case 1: /* HTML_HEADING */
            {
              if (pos > 0) output[pos++] = '\n';
              output[pos++] = '\n';

              if (n->text)
                {
                  int added = snprintf(output + pos,
                                       RENDER_MAX_OUTPUT - pos,
                                       "#FFFFFF %s#\n", n->text);
                  if (added > 0) pos += added;
                }
            }
            break;

          case 3: /* HTML_LINK */
            if (n->text && n->href)
              {
                int added = snprintf(output + pos,
                                     RENDER_MAX_OUTPUT - pos,
                                     "#0077CC [%s]#", n->text);
                if (added > 0) pos += added;
              }
            else if (n->href)
              {
                int added = snprintf(output + pos,
                                     RENDER_MAX_OUTPUT - pos,
                                     "#0077CC [%s]#", n->href);
                if (added > 0) pos += added;
              }
            break;

          case 4: /* HTML_LIST_ITEM */
            if (pos > 0) output[pos++] = '\n';
            output[pos++] = ' ';
            output[pos++] = '-';
            output[pos++] = ' ';
            break;

          case 6: /* HTML_LINE_BREAK */
            output[pos++] = '\n';
            break;

          case 7: /* HTML_IMAGE */
            {
              /* In text-only mode, show alt text or placeholder */

              const char *alt = n->alt ? n->alt :
                                (n->text ? n->text : "[image]");
              int added = snprintf(output + pos,
                                   RENDER_MAX_OUTPUT - pos,
                                   "\n[img: %s]\n", alt);
              if (added > 0) pos += added;
            }
            break;

          default:
            if (n->text)
              {
                size_t tlen = strlen(n->text);
                if (pos + tlen < RENDER_MAX_OUTPUT - 1)
                  {
                    memcpy(output + pos, n->text, tlen);
                    pos += tlen;
                  }
              }
            break;
        }

      n = n->next;
    }

  output[pos] = '\0';
  syslog(LOG_DEBUG, "PCWEB: Rendered %zu chars\n", pos);
  return output;
}
