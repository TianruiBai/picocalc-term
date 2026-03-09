/****************************************************************************
 * sdk/template/main.c
 *
 * PicoCalc third-party app template.
 *
 * Build with the PicoCalc SDK, then package with pcpkg-create.
 *
 ****************************************************************************/

#include <stdio.h>
#include <pcterm/app.h>
#include <lvgl/lvgl.h>

/**
 * App entry point.
 *
 * The framework provides:
 *   - LVGL screen access via pc_app_get_screen()
 *   - PSRAM allocation via pc_app_psram_alloc()
 *   - System info via pc_app_get_hostname(), etc.
 *
 * Your app runs in the main event loop. Use LVGL widgets for UI.
 * Call pc_app_exit(0) to quit, or pc_app_yield() to save state and
 * return to the launcher.
 */

int main(int argc, char *argv[])
{
    /* Get the app's screen container (320×300 usable area) */

    lv_obj_t *screen = pc_app_get_screen();

    /* --- Build your UI here --- */

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Hello from PicoCalc!");
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    /* --- Event loop --- */

    /* The framework handles the LVGL event loop.
     * Your app can process keyboard events via an LVGL input group,
     * or register LVGL event callbacks on widgets.
     *
     * To exit cleanly:
     *   pc_app_exit(0);      — discard state, return to launcher
     *   pc_app_yield();      — save state, return to launcher
     */

    /* For now, just return (framework handles cleanup) */

    return 0;
}
