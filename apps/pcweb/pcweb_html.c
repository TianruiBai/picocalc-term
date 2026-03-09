/****************************************************************************
 * apps/pcweb/pcweb_html.c
 *
 * Minimal HTML parser for the text-mode web browser.
 * Extracts text content from a subset of HTML tags:
 *   <h1>-<h6>, <p>, <br>, <a>, <li>, <pre>, <strong>, <em>, <title>
 *
 * Strips all other tags and entities.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>

#include "pcterm/app.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef enum
{
  HTML_TEXT,
  HTML_HEADING,
  HTML_PARAGRAPH,
  HTML_LINK,
  HTML_LIST_ITEM,
  HTML_PREFORMAT,
  HTML_LINE_BREAK,
  HTML_IMAGE,
} html_node_type_t;

typedef struct html_node_s
{
  html_node_type_t type;
  char            *text;      /* Display text */
  char            *href;      /* URL (for links only) */
  char            *src;       /* Image source URL (for images only) */
  char            *alt;       /* Alt text (for images) */
  int              level;     /* Heading level (1-6) */
  struct html_node_s *next;
} html_node_t;

typedef struct html_doc_s
{
  char       *title;
  html_node_t *nodes;
  html_node_t *tail;
  int          node_count;
} html_doc_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static char *psram_strdup(const char *s)
{
  size_t len = strlen(s);
  char *dup = (char *)pc_app_psram_alloc(len + 1);
  if (dup) memcpy(dup, s, len + 1);
  return dup;
}

static html_node_t *node_create(html_node_type_t type, const char *text)
{
  html_node_t *n = (html_node_t *)pc_app_psram_alloc(sizeof(html_node_t));
  if (n == NULL) return NULL;

  memset(n, 0, sizeof(html_node_t));
  n->type = type;
  if (text) n->text = psram_strdup(text);
  return n;
}

static void doc_append(html_doc_t *doc, html_node_t *node)
{
  if (doc->tail)
    {
      doc->tail->next = node;
    }
  else
    {
      doc->nodes = node;
    }

  doc->tail = node;
  doc->node_count++;
}

/**
 * Decode basic HTML entities: &amp; &lt; &gt; &quot; &nbsp;
 */
static void decode_entities(char *text)
{
  char *src = text;
  char *dst = text;

  while (*src)
    {
      if (*src == '&')
        {
          if (strncmp(src, "&amp;", 5) == 0)
            { *dst++ = '&'; src += 5; }
          else if (strncmp(src, "&lt;", 4) == 0)
            { *dst++ = '<'; src += 4; }
          else if (strncmp(src, "&gt;", 4) == 0)
            { *dst++ = '>'; src += 4; }
          else if (strncmp(src, "&quot;", 6) == 0)
            { *dst++ = '"'; src += 6; }
          else if (strncmp(src, "&nbsp;", 6) == 0)
            { *dst++ = ' '; src += 6; }
          else if (strncmp(src, "&#", 2) == 0)
            {
              /* Numeric entity — just skip */
              while (*src && *src != ';') src++;
              if (*src == ';') src++;
              *dst++ = '?';
            }
          else
            {
              *dst++ = *src++;
            }
        }
      else
        {
          *dst++ = *src++;
        }
    }

  *dst = '\0';
}

/**
 * Extract the value of an HTML attribute (e.g., href="...").
 */
static int extract_attr(const char *tag, const char *attr_name,
                        char *out, size_t out_len)
{
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "%s=\"", attr_name);

  const char *start = strstr(tag, pattern);
  if (start == NULL)
    {
      snprintf(pattern, sizeof(pattern), "%s='", attr_name);
      start = strstr(tag, pattern);
    }

  if (start == NULL) return -1;

  start += strlen(pattern);
  char delim = *(start - 1);  /* Quote character */

  const char *end = strchr(start, delim);
  if (end == NULL) return -1;

  size_t len = end - start;
  if (len >= out_len) len = out_len - 1;

  memcpy(out, start, len);
  out[len] = '\0';
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Parse an HTML document into a linked list of displayable nodes.
 */
html_doc_t *html_parse(const char *html, size_t len)
{
  html_doc_t *doc;

  doc = (html_doc_t *)pc_app_psram_alloc(sizeof(html_doc_t));
  if (doc == NULL) return NULL;

  memset(doc, 0, sizeof(html_doc_t));

  char text_buf[1024];
  int  text_len = 0;
  bool in_tag = false;
  bool in_pre = false;
  char tag_buf[256];
  int  tag_len = 0;

  for (size_t i = 0; i < len; i++)
    {
      char ch = html[i];

      if (in_tag)
        {
          if (ch == '>')
            {
              tag_buf[tag_len] = '\0';
              in_tag = false;

              /* Process tag */

              char *tag = tag_buf;
              bool closing = (tag[0] == '/');
              if (closing) tag++;

              /* Flush text before processing tag */

              if (text_len > 0)
                {
                  text_buf[text_len] = '\0';
                  decode_entities(text_buf);

                  html_node_t *n = node_create(HTML_TEXT, text_buf);
                  if (n) doc_append(doc, n);
                  text_len = 0;
                }

              /* Tag-specific handling */

              if (strncasecmp(tag, "h", 1) == 0 && tag[1] >= '1' &&
                  tag[1] <= '6' && !closing)
                {
                  /* Will be applied to next text node */
                }
              else if (strncasecmp(tag, "br", 2) == 0 ||
                       strncasecmp(tag, "br/", 3) == 0)
                {
                  html_node_t *n = node_create(HTML_LINE_BREAK, "\n");
                  if (n) doc_append(doc, n);
                }
              else if (strncasecmp(tag, "p", 1) == 0 && !closing)
                {
                  html_node_t *n = node_create(HTML_LINE_BREAK, "\n");
                  if (n) doc_append(doc, n);
                }
              else if (strncasecmp(tag, "a ", 2) == 0 && !closing)
                {
                  char href[256] = "";
                  extract_attr(tag_buf, "href", href, sizeof(href));

                  /* Store href — next text becomes the link text */
                  /* Simplified: create link node with href */

                  html_node_t *n = node_create(HTML_LINK, "");
                  if (n)
                    {
                      n->href = psram_strdup(href);
                      doc_append(doc, n);
                    }
                }
              else if (strncasecmp(tag, "li", 2) == 0 && !closing)
                {
                  html_node_t *n = node_create(HTML_LIST_ITEM, "- ");
                  if (n) doc_append(doc, n);
                }
              else if (strncasecmp(tag, "img ", 4) == 0 ||
                       strncasecmp(tag, "img/", 4) == 0)
                {
                  /* Parse <img src="..." alt="..."> */

                  char src[256] = "";
                  char alt[128] = "";
                  extract_attr(tag_buf, "src", src, sizeof(src));
                  extract_attr(tag_buf, "alt", alt, sizeof(alt));

                  if (src[0] != '\0')
                    {
                      html_node_t *n = node_create(HTML_IMAGE,
                                                   alt[0] ? alt : "[image]");
                      if (n)
                        {
                          n->src = psram_strdup(src);
                          if (alt[0])
                            {
                              n->alt = psram_strdup(alt);
                            }

                          doc_append(doc, n);
                        }
                    }
                }
              else if (strncasecmp(tag, "pre", 3) == 0)
                {
                  in_pre = !closing;
                }
              else if (strncasecmp(tag, "title", 5) == 0 && closing)
                {
                  /* Title was captured as text — extract */
                }

              tag_len = 0;
            }
          else if (tag_len < (int)sizeof(tag_buf) - 1)
            {
              tag_buf[tag_len++] = ch;
            }
        }
      else
        {
          if (ch == '<')
            {
              in_tag = true;
              tag_len = 0;
            }
          else
            {
              /* Collapse whitespace unless in <pre> */

              if (!in_pre && (ch == '\n' || ch == '\r' || ch == '\t'))
                {
                  ch = ' ';
                }

              if (!in_pre && ch == ' ' && text_len > 0 &&
                  text_buf[text_len - 1] == ' ')
                {
                  continue;  /* Skip duplicate spaces */
                }

              if (text_len < (int)sizeof(text_buf) - 1)
                {
                  text_buf[text_len++] = ch;
                }
            }
        }
    }

  /* Flush remaining text */

  if (text_len > 0)
    {
      text_buf[text_len] = '\0';
      decode_entities(text_buf);
      html_node_t *n = node_create(HTML_TEXT, text_buf);
      if (n) doc_append(doc, n);
    }

  syslog(LOG_INFO, "HTML: Parsed %d nodes\n", doc->node_count);
  return doc;
}

/**
 * Free an HTML document.
 */
void html_free(html_doc_t *doc)
{
  if (doc == NULL) return;

  html_node_t *n = doc->nodes;
  while (n)
    {
      html_node_t *next = n->next;
      if (n->text) pc_app_psram_free(n->text);
      if (n->href) pc_app_psram_free(n->href);
      if (n->src)  pc_app_psram_free(n->src);
      if (n->alt)  pc_app_psram_free(n->alt);
      pc_app_psram_free(n);
      n = next;
    }

  if (doc->title) pc_app_psram_free(doc->title);
  pc_app_psram_free(doc);
}
