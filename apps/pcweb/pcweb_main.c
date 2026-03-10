/****************************************************************************
 * apps/pcweb/pcweb_main.c
 *
 * Textual web browser (Lynx/w3m-style).
 * Features: HTTP/HTTPS client, HTML subset parser, text-mode rendering.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>

#include "pcterm/app.h"

/****************************************************************************
 * External References — pcweb modules
 ****************************************************************************/

/* pcweb_http.c */

typedef struct http_response_s
{
  int           status_code;
  char          content_type[64];
  char         *body;
  size_t        body_len;
  char          redirect_url[512];
} http_response_t;

extern int  http_get(const char *url, http_response_t *resp);
extern void http_response_free(http_response_t *resp);

/* pcweb_html.c */

typedef struct html_doc_s html_doc_t;

extern html_doc_t *html_parse(const char *html, size_t len);
extern void        html_free(html_doc_t *doc);

/* pcweb_render.c */

extern char *pcweb_render(void *nodes_ptr);
extern int   pcweb_render_to_container(lv_obj_t *container, void *nodes_ptr,
                                       const char *base_url);

/* pcweb_nav.c */

typedef struct web_nav_s web_nav_t;

extern web_nav_t  *web_nav_create(void);
extern void        web_nav_destroy(web_nav_t *nav);
extern void        web_nav_goto(web_nav_t *nav, const char *url);
extern const char *web_nav_back(web_nav_t *nav);
extern const char *web_nav_forward(web_nav_t *nav);
extern const char *web_nav_current(web_nav_t *nav);
extern int         web_nav_load_bookmarks(web_nav_t *nav);
extern int         web_nav_add_bookmark(web_nav_t *nav,
                                        const char *title,
                                        const char *url);

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct web_state_s
{
  char url[512];           /* Current URL */
  char *page_text;         /* Rendered page text in PSRAM */
  size_t page_len;
  int scroll_y;            /* Vertical scroll position */
} web_state_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static web_state_t   g_web;
static web_nav_t    *g_nav       = NULL;
static lv_obj_t     *g_url_bar   = NULL;
static lv_obj_t     *g_content   = NULL;
static lv_obj_t     *g_status    = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcweb_set_status
 ****************************************************************************/

static void pcweb_set_status(const char *text)
{
  if (g_status)
    {
      lv_label_set_text(g_status, text);
    }
}

/****************************************************************************
 * Name: pcweb_navigate
 *
 * Description:
 *   Fetch URL, parse HTML, render and display.
 *
 ****************************************************************************/

static void pcweb_navigate(const char *url)
{
  http_response_t resp;
  int ret;

  if (url == NULL || url[0] == '\0')
    {
      return;
    }

  /* Update URL bar */

  if (g_url_bar)
    {
      lv_textarea_set_text(g_url_bar, url);
    }

  strncpy(g_web.url, url, sizeof(g_web.url) - 1);
  pcweb_set_status("Loading...");

  /* Fetch the page */

  memset(&resp, 0, sizeof(resp));
  ret = http_get(url, &resp);

  if (ret < 0)
    {
      pcweb_set_status("Network error");

      if (g_content)
        {
          lv_label_set_text(g_content,
            "Error: Could not fetch page.\n\n"
            "Check network connection.");
        }

      return;
    }

  /* Handle redirects */

  if (resp.redirect_url[0] != '\0')
    {
      http_response_free(&resp);
      pcweb_navigate(resp.redirect_url);
      return;
    }

  /* Parse HTML */

  if (resp.body != NULL && resp.body_len > 0)
    {
      /* Check if response is an image (direct image URL) */

      bool is_image = (strstr(resp.content_type, "image/") != NULL);

      if (is_image)
        {
          /* Direct image display — clear content and show image */

          lv_obj_t *scroll = lv_obj_get_parent(g_content);
          lv_obj_clean(scroll);

          /* Re-create placeholder label (will be replaced) */

          g_content = lv_label_create(scroll);
          lv_label_set_long_mode(g_content, LV_LABEL_LONG_WRAP);
          lv_obj_set_width(g_content, 308);
          lv_obj_set_style_text_color(g_content, lv_color_white(), 0);
          lv_obj_set_style_text_font(g_content, &lv_font_unscii_8, 0);

          /* Import image decoder */

          typedef struct { uint16_t w, h; uint16_t *px; size_t sz;
                         } img_t;
          extern int pcweb_image_decode(const uint8_t *, size_t, img_t *);
          extern lv_obj_t *pcweb_image_create_lvobj(lv_obj_t *,
                                                    const img_t *);
          extern void pcweb_image_free(img_t *);

          img_t img;
          if (pcweb_image_decode((const uint8_t *)resp.body,
                                 resp.body_len, &img) == 0)
            {
              pcweb_image_create_lvobj(scroll, &img);
              lv_label_set_text(g_content, "");
            }
          else
            {
              lv_label_set_text(g_content,
                "Error: Could not decode image.");
            }
        }
      else
        {
          html_doc_t *doc = html_parse(resp.body, resp.body_len);

          if (doc != NULL)
            {
              /* Clear the scroll container and use container renderer
               * which handles mixed text + images
               */

              lv_obj_t *scroll = lv_obj_get_parent(g_content);
              lv_obj_clean(scroll);

              /* Re-create g_content label reference after clean */

              g_content = NULL;

              int count = pcweb_render_to_container(scroll, doc, url);

              if (count <= 0)
                {
                  /* Fallback: text-only render */

                  char *rendered = pcweb_render(doc);

                  g_content = lv_label_create(scroll);
                  lv_label_set_long_mode(g_content, LV_LABEL_LONG_WRAP);
                  lv_obj_set_width(g_content, 308);
                  lv_obj_set_style_text_color(g_content,
                                              lv_color_white(), 0);
                  lv_obj_set_style_text_font(g_content,
                                             &lv_font_unscii_8, 0);

                  if (rendered != NULL)
                    {
                      lv_label_set_text(g_content, rendered);

                      if (g_web.page_text)
                        {
                          pc_app_psram_free(g_web.page_text);
                        }

                      g_web.page_text = rendered;
                      g_web.page_len  = strlen(rendered);
                    }
                }
              else
                {
                  /* Free old page text since container owns objects now */

                  if (g_web.page_text)
                    {
                      pc_app_psram_free(g_web.page_text);
                      g_web.page_text = NULL;
                      g_web.page_len  = 0;
                    }
                }

              html_free(doc);
            }
          else
            {
              /* Could not parse — show raw text */

              if (g_content)
                {
                  lv_label_set_text(g_content, resp.body);
                }
            }
        }
    }

  char status[64];
  snprintf(status, sizeof(status), "%d | %s",
           resp.status_code, url);
  pcweb_set_status(status);

  /* Add to navigation history */

  if (g_nav)
    {
      web_nav_goto(g_nav, url);
    }

  g_web.scroll_y = 0;

  http_response_free(&resp);
}

/****************************************************************************
 * Name: pcweb_key_handler
 ****************************************************************************/

static void pcweb_key_handler(lv_event_t *e)
{
  uint32_t key = lv_event_get_key(e);

  switch (key)
    {
      case LV_KEY_ENTER:
        {
          /* Navigate to URL in the bar */

          const char *url = lv_textarea_get_text(g_url_bar);
          pcweb_navigate(url);
        }
        break;

      case LV_KEY_BACKSPACE:
        {
          /* Go back */

          if (g_nav)
            {
              const char *prev = web_nav_back(g_nav);
              if (prev)
                {
                  pcweb_navigate(prev);
                }
            }
        }
        break;

      case LV_KEY_DOWN:
        g_web.scroll_y += 20;
        if (g_content)
          {
            lv_obj_scroll_to_y(lv_obj_get_parent(g_content),
                               g_web.scroll_y, LV_ANIM_OFF);
          }
        break;

      case LV_KEY_UP:
        g_web.scroll_y -= 20;
        if (g_web.scroll_y < 0) g_web.scroll_y = 0;
        if (g_content)
          {
            lv_obj_scroll_to_y(lv_obj_get_parent(g_content),
                               g_web.scroll_y, LV_ANIM_OFF);
          }
        break;

      case 'b':
      case 'B':
        /* Bookmark current page */
        if (g_nav && g_web.url[0])
          {
            web_nav_add_bookmark(g_nav, "Page", g_web.url);
            pcweb_set_status("Bookmark added");
          }
        break;

      case 'q':
      case 'Q':
        pc_app_exit(0);
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: pcweb_main
 ****************************************************************************/

static int pcweb_main(int argc, char *argv[])
{
  lv_obj_t *screen = pc_app_get_screen();

  /* Initialize navigation */

  g_nav = web_nav_create();
  if (g_nav)
    {
      web_nav_load_bookmarks(g_nav);
    }

  /* --- URL bar --- */

  g_url_bar = lv_textarea_create(screen);
  lv_obj_set_size(g_url_bar, 300, 24);
  lv_obj_align(g_url_bar, LV_ALIGN_TOP_LEFT, 2, 2);
  lv_textarea_set_placeholder_text(g_url_bar, "Enter URL...");
  lv_textarea_set_text(g_url_bar, "");
  lv_textarea_set_one_line(g_url_bar, true);
  lv_obj_set_style_text_font(g_url_bar, &lv_font_unscii_8, 0);

  /* --- Scrollable content area --- */

  lv_obj_t *scroll_cont = lv_obj_create(screen);
  lv_obj_set_size(scroll_cont, 316, 246);
  lv_obj_align(scroll_cont, LV_ALIGN_TOP_LEFT, 2, 30);
  lv_obj_set_style_bg_color(scroll_cont, lv_color_black(), 0);
  lv_obj_set_style_border_width(scroll_cont, 0, 0);
  lv_obj_set_style_pad_all(scroll_cont, 2, 0);
  lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);

  g_content = lv_label_create(scroll_cont);
  lv_label_set_long_mode(g_content, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_content, 308);
  lv_obj_set_style_text_color(g_content, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_content, &lv_font_unscii_8, 0);
  lv_label_set_text(g_content,
    "PicoCalc Web Browser\n\n"
    "Enter a URL and press Enter.\n\n"
    "[Up/Down] Scroll  [Backspace] Back\n"
    "[B] Bookmark  [Q] Quit");

  /* --- Status bar --- */

  g_status = lv_label_create(screen);
  lv_label_set_text(g_status, "Ready");
  lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 4, -4);
  lv_obj_set_style_text_font(g_status, &lv_font_unscii_8, 0);
  lv_obj_set_style_text_color(g_status, lv_color_make(100, 200, 100), 0);

  /* Register keyboard handler */

  lv_obj_add_event_cb(screen, pcweb_key_handler, LV_EVENT_KEY, NULL);
  lv_group_t *grp = lv_group_get_default();
  if (grp)
    {
      lv_group_add_obj(grp, g_url_bar);
      lv_group_add_obj(grp, screen);
    }

  /* If URL passed on command line, navigate directly */

  if (argc > 1 && argv[1][0] != '\0')
    {
      lv_textarea_set_text(g_url_bar, argv[1]);
      pcweb_navigate(argv[1]);
    }

  return 0;
}

static int pcweb_save(void *buf, size_t *len)
{
  /* Save current URL and scroll position */
  size_t need = sizeof(web_state_t);
  if (need > *len) return -1;
  memcpy(buf, &g_web, sizeof(web_state_t));
  *len = need;
  return 0;
}

static int pcweb_restore(const void *buf, size_t len)
{
  if (len < sizeof(web_state_t)) return -1;
  memcpy(&g_web, buf, sizeof(web_state_t));

  /* Re-fetch the page from saved URL */

  if (g_web.url[0] != '\0')
    {
      pcweb_navigate(g_web.url);
    }

  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const pc_app_t g_pcweb_app = {
  .info = {
    .name         = "pcweb",
    .display_name = "Web Browser",
    .version      = "1.0.0",
    .category     = "network",
    .icon         = LV_SYMBOL_HOME,
    .min_ram      = 262144,   /* 256 KB for page content + HTTP buffers */
    .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_NETWORK
                    | PC_APP_FLAG_STATEFUL,
  },
  .main    = pcweb_main,
  .save    = pcweb_save,
  .restore = pcweb_restore,
};
