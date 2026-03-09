/****************************************************************************
 * pcterm/src/psram_stubs.c
 *
 * PSRAM stub implementations.
 * On boards without external PSRAM (like the RP2350B which uses only
 * internal 520KB SRAM), these simply delegate to standard malloc/free.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdlib.h>
#include <nuttx/mm/mm.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *psram_malloc(size_t size)
{
  return malloc(size);
}

void *psram_realloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}

void psram_free(void *ptr)
{
  free(ptr);
}

size_t psram_available(void)
{
  struct mallinfo info = mallinfo();
  return (size_t)info.fordblks;
}
