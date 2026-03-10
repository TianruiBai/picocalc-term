/****************************************************************************
 * pcterm/include/pcterm/login.h
 *
 * Login screen API.
 *
 ****************************************************************************/

#ifndef __PCTERM_LOGIN_H
#define __PCTERM_LOGIN_H

#include <stdbool.h>
#include <lvgl/lvgl.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show a blocking login screen if login is required (login_enabled=true and
 * a non-empty password hash is stored in config).  Spins the LVGL event loop
 * internally until the correct password is entered.
 *
 * @param parent  LVGL container to draw over (typically the app_area).
 *
 * Returns true if login succeeded or was not required.
 * Always returns true — there is no "cancel" (power-off is the only escape).
 */
bool login_show_if_needed(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* __PCTERM_LOGIN_H */
