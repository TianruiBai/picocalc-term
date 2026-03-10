/****************************************************************************
 * pcterm/include/pcterm/vconsole.h
 *
 * Virtual console (VT) manager for PicoCalc-Term.
 *
 * Provides 4 virtual consoles (tty0-3):
 *   tty0 = GUI mode (LVGL launcher / apps)
 *   tty1-3 = Text terminal consoles, each with independent NSH session
 *
 * Switch consoles with Ctrl+Alt+F1 through Ctrl+Alt+F4 (Linux-style).
 * Each text console runs its own NuttShell via a pseudo-terminal (PTY).
 *
 ****************************************************************************/

#ifndef __PCTERM_VCONSOLE_H
#define __PCTERM_VCONSOLE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <lvgl/lvgl.h>

#include "pcterm/terminal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VCONSOLE_COUNT      4        /* Total virtual consoles (tty0-3) */
#define VCONSOLE_GUI        0        /* tty0 = GUI launcher mode */
#define VCONSOLE_TTY1       1        /* First text console */
#define VCONSOLE_TTY2       2
#define VCONSOLE_TTY3       3

/* Startup mode (config setting) */

#define VCONSOLE_STARTUP_GUI      0  /* Boot to tty0 GUI launcher */
#define VCONSOLE_STARTUP_CONSOLE  1  /* Boot to tty1 text console */

/* Reader thread stack size */

#define VCONSOLE_READER_STACK   4096
#define VCONSOLE_SHELL_STACK    4096

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct vconsole_s
{
  int         index;          /* Console number 0-3 */
  bool        is_gui;         /* true for tty0 (GUI mode) */
  bool        active;         /* Currently displayed on screen */
  bool        initialized;    /* PTY + reader set up */
  bool        logged_in;      /* User has authenticated on this console */

  /* Terminal (NULL for GUI console) */

  terminal_t *term;

  /* PTY pair (-1 if not open) */

  int         pty_master;     /* Master end — reader thread + key input */
  int         pty_slave;      /* Slave end — shell stdio */

  /* Tasks */

  pid_t       shell_pid;      /* Shell/login task PID (-1 if not running) */
  pid_t       reader_pid;     /* Reader thread PID (-1 if not running) */

  /* PTY slave path for task_create argv (must outlive the child task) */

  char        pty_path[32];   /* e.g. "/dev/pts/0" */
  char        tty_name[8];    /* e.g. "tty1" */

  /* LVGL container on the app area */

  lv_obj_t   *container;      /* Holds terminal canvas (or NULL for GUI) */
} vconsole_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the virtual console subsystem.
 * Creates LVGL containers for each text console.
 *
 * @param app_area  The statusbar app area (320×300 parent container)
 */
void vconsole_init(lv_obj_t *app_area);

/**
 * Switch to a different virtual console.
 * Hides the current console and shows the target.
 * For text consoles, spawns login/shell if not yet started.
 *
 * @param index  Console number (0-3)
 */
void vconsole_switch(int index);

/**
 * Get the currently active console index (0-3).
 */
int vconsole_get_active(void);

/**
 * Check if the currently active console is a text console (tty1-3).
 */
bool vconsole_is_text_active(void);

/**
 * Route a raw keyboard event to the active text console.
 * Called from keypad_read_cb when a text console is active.
 *
 * @param raw_key  Raw key code from south-bridge keyboard
 * @param mods     Modifier flags (shift/ctrl/alt/fn)
 */
void vconsole_key_input(uint8_t raw_key, uint8_t mods);

/**
 * Render the active text console's terminal if its dirty flag is set.
 * Called from the main event loop. (LVGL is single-threaded — only
 * call this from the main loop context.)
 */
void vconsole_render_if_dirty(void);

/**
 * Shut down all virtual consoles (cleanup on exit).
 */
void vconsole_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_VCONSOLE_H */
