# PicoCalc-Term App Framework API

## Overview

Every application running on PicoCalc-Term interacts with the OS through the **App Framework API**. This API provides app lifecycle management, state save/restore, LVGL screen access, memory allocation, and system queries.

The API is defined in `pcterm/include/pcterm/app.h`.

---

## Core Types

### `pc_app_info_t` — App Metadata

```c
typedef struct pc_app_info_s {
    const char *name;            /* Internal ID: "pcedit", "pcaudio", ... */
    const char *display_name;    /* Human-readable: "Text Editor" */
    const char *version;         /* Semantic version: "1.0.0" */
    const char *category;        /* "system", "office", "entertainment", ... */
    uint32_t    min_ram;         /* Minimum PSRAM bytes required */
    uint32_t    flags;           /* PC_APP_FLAG_* bitfield */
} pc_app_info_t;
```

### Flags

```c
#define PC_APP_FLAG_BUILTIN     (1 << 0)   /* System app, cannot be removed */
#define PC_APP_FLAG_BACKGROUND  (1 << 1)   /* Can run background service (audio) */
#define PC_APP_FLAG_NETWORK     (1 << 2)   /* Requires Wi-Fi */
#define PC_APP_FLAG_STATEFUL    (1 << 3)   /* Supports save/restore */
```

---

## App Registration

### Built-in Apps

Built-in apps register via a static table compiled into firmware:

```c
/* In app's main source file */
static const pc_app_t g_pcedit_app = {
    .info = {
        .name         = "pcedit",
        .display_name = "Text Editor",
        .version      = "1.0.0",
        .category     = "office",
        .min_ram      = 65536,
        .flags        = PC_APP_FLAG_BUILTIN | PC_APP_FLAG_STATEFUL,
    },
    .main    = pcedit_main,
    .save    = pcedit_save,
    .restore = pcedit_restore,
};

/* Register in pcterm/src/app_registry.c */
static const pc_app_t *g_builtin_apps[] = {
    &g_settings_app,
    &g_pcedit_app,
    &g_pccsv_app,
    &g_pcaudio_app,
    &g_pcvideo_app,
    &g_pcterm_app,
    &g_pcssh_app,
    &g_pcweb_app,
    NULL,
};
```

### Third-party Apps (ELF)

Third-party apps are loaded dynamically from `/mnt/sd/apps/<name>/app.elf`. They export a standard entry point:

```c
/* Third-party app template */
#include <pcterm/app.h>

int main(int argc, char *argv[])
{
    lv_obj_t *screen = pc_app_get_screen();
    /* Build UI, run event loop */
    pc_app_exit(0);
    return 0;
}
```

The framework injects the `pc_app_*` API functions via the NuttX symbol table export mechanism.

---

## Lifecycle Functions

### `pc_app_exit(int code)`

Terminate the app cleanly. State is **discarded** (no resume on next launch).

```c
void pc_app_exit(int code);
```

- `code` — Exit status (0 = success, >0 = error)
- Frees all PSRAM allocations made by the app
- Destroys all LVGL objects on the app screen
- Deletes any saved state file for this app
- Returns control to the launcher

### `pc_app_yield(void)`

Save state and return to launcher. **State is preserved** for resume on next launch.

```c
void pc_app_yield(void);
```

- Calls the app's `save` callback to serialize state
- Writes state blob to `/mnt/sd/etc/appstate/<name>.state`
- Frees app memory and LVGL objects
- Returns control to the launcher

This is also called automatically when the user presses `Fn+Home`.

---

## Screen Access

### `pc_app_get_screen(void)`

Get the LVGL parent object for the app's UI area (320 × 300 pixels, below the status bar).

```c
lv_obj_t *pc_app_get_screen(void);
```

- Returns a pre-created `lv_obj_t*` sized to 320×300
- The status bar (top 20px) is managed by the OS and not accessible
- The app should create all its UI widgets as children of this object
- On exit, all children are automatically destroyed

### `pc_app_get_screen_width(void)` / `pc_app_get_screen_height(void)`

```c
int pc_app_get_screen_width(void);   /* Returns 320 */
int pc_app_get_screen_height(void);  /* Returns 300 (320 - 20px status bar) */
```

---

## Memory Management

### PSRAM Allocation

Apps that need large buffers (editor buffers, audio decode, image data) should use PSRAM:

```c
void *pc_app_psram_alloc(size_t size);
void *pc_app_psram_realloc(void *ptr, size_t new_size);
void  pc_app_psram_free(void *ptr);
size_t pc_app_psram_available(void);
```

- All PSRAM allocations are tracked per-app
- On app exit (or yield), all remaining PSRAM allocations are freed
- `pc_app_psram_available()` returns bytes of free PSRAM

### Standard Heap (SRAM)

For small, performance-critical allocations, use standard `malloc()`/`free()`. SRAM heap is ~350KB shared across the OS and active app.

---

## State Save/Restore

### Save Callback

```c
typedef int (*pc_app_save_t)(void *buf, size_t maxlen);
```

- Called when the user presses `Fn+Home` or `pc_app_yield()` is invoked
- `buf` — Write your serialized state here
- `maxlen` — Maximum bytes available (typically 256KB)
- Return: number of bytes written to `buf`, or 0 for no state, or <0 on error

### Restore Callback

```c
typedef int (*pc_app_restore_t)(const void *buf, size_t len);
```

- Called on launch **instead of** `main()` if a saved state file exists
- `buf` — Previously saved state data
- `len` — Size of the state data
- Return: 0 on success (app should rebuild UI from state), <0 on error (framework falls back to `main()`)

### State Serialization Best Practices

```c
/* Example: pcedit state */
typedef struct {
    uint32_t magic;           /* 0x50434544 = "PCED" */
    uint32_t version;         /* State format version */
    char     filepath[256];   /* Open file path */
    uint32_t cursor_line;
    uint32_t cursor_col;
    uint32_t scroll_offset;
    uint32_t buffer_len;      /* Length of unsaved text following this struct */
    bool     modified;        /* Has unsaved changes? */
    /* Followed by: buffer_len bytes of text buffer content */
} pcedit_state_t;

static int pcedit_save(void *buf, size_t maxlen)
{
    pcedit_state_t *state = (pcedit_state_t *)buf;
    state->magic = 0x50434544;
    state->version = 1;
    strncpy(state->filepath, g_current_file, 255);
    state->cursor_line = g_cursor_line;
    state->cursor_col = g_cursor_col;
    state->scroll_offset = g_scroll_offset;
    state->modified = g_buffer_modified;

    /* Save buffer contents after the header */
    state->buffer_len = g_buffer_len;
    if (sizeof(pcedit_state_t) + g_buffer_len > maxlen) {
        /* Buffer too large — save header only, user will need to reopen file */
        state->buffer_len = 0;
        return sizeof(pcedit_state_t);
    }
    memcpy((uint8_t *)buf + sizeof(pcedit_state_t),
           g_buffer, g_buffer_len);
    return sizeof(pcedit_state_t) + g_buffer_len;
}
```

---

## System Queries

```c
const char *pc_app_get_hostname(void);        /* "picocalc" */
bool        pc_app_wifi_connected(void);      /* true if Wi-Fi is up */
const char *pc_app_wifi_ssid(void);           /* Current SSID or NULL */
uint32_t    pc_app_wifi_ip(void);             /* IPv4 in network byte order */
uint8_t     pc_app_battery_percent(void);     /* 0-100 */
bool        pc_app_battery_charging(void);    /* true if charging */
uint32_t    pc_app_uptime_sec(void);          /* Seconds since boot */
```

---

## Audio Service

Background audio is managed as a system service. Apps interact with it through:

```c
/* Play a file (starts on Core 1, survives app switch) */
int  pc_audio_play(const char *filepath);
int  pc_audio_play_playlist(const char **files, int count);
void pc_audio_pause(void);
void pc_audio_resume(void);
void pc_audio_stop(void);
void pc_audio_next(void);
void pc_audio_prev(void);
void pc_audio_set_volume(uint8_t vol);   /* 0-100 */
uint8_t pc_audio_get_volume(void);

typedef struct {
    bool     playing;
    bool     paused;
    char     current_file[256];
    uint32_t position_ms;
    uint32_t duration_ms;
    uint8_t  volume;
} pc_audio_status_t;

void pc_audio_get_status(pc_audio_status_t *status);
```

---

## Event Callbacks (Optional)

Apps can register for system events:

```c
typedef enum {
    PC_EVENT_WIFI_CONNECTED,
    PC_EVENT_WIFI_DISCONNECTED,
    PC_EVENT_BATTERY_LOW,      /* < 10% */
    PC_EVENT_BATTERY_CRITICAL, /* < 5% */
    PC_EVENT_SD_REMOVED,
    PC_EVENT_SD_INSERTED,
} pc_event_type_t;

typedef void (*pc_event_cb_t)(pc_event_type_t event, void *arg);

void pc_app_register_event(pc_event_type_t type, pc_event_cb_t cb, void *arg);
void pc_app_unregister_event(pc_event_type_t type);
```

---

## Thread Safety

- **LVGL calls** must be made from the LVGL thread context only. If your app uses a worker thread, use `lv_async_call()` to post UI updates.
- **PSRAM allocation** functions are thread-safe (mutex-protected).
- **Audio service** functions are thread-safe (Core 1 communication via atomics + message queue).
- **State save/restore** callbacks are called from the LVGL thread — no concurrency concerns.

---

## Error Codes

```c
#define PC_OK               0
#define PC_ERR_NOMEM       -1    /* Out of memory */
#define PC_ERR_IO          -2    /* File/device I/O error */
#define PC_ERR_INVAL       -3    /* Invalid argument */
#define PC_ERR_NOENT       -4    /* File/resource not found */
#define PC_ERR_BUSY        -5    /* Resource busy */
#define PC_ERR_NET         -6    /* Network error */
#define PC_ERR_AUTH        -7    /* Authentication failed */
#define PC_ERR_TOOBIG      -8    /* Data exceeds limit */
```
