/****************************************************************************
 * pcterm/include/pcterm/runit.h
 *
 * Minimal runit-compatible service supervisor for eUX OS.
 * See pcterm/src/runit.c for implementation details.
 *
 ****************************************************************************/

#ifndef __PCTERM_RUNIT_H
#define __PCTERM_RUNIT_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Start the service directory supervisor (runs as a task).
 * Scans svdir (default: /etc/sv/) for service subdirectories.
 * Each subdir with a ./run script becomes a supervised service.
 */

int runit_runsvdir(const char *svdir);

/* Control a named service.
 * cmd: "start"/"up", "stop"/"down", "restart", "status"
 */

int runit_sv_control(const char *name, const char *cmd);

/* Print status of all supervised services to stdout. */

void runit_list_services(void);

/* Stop all running services (used during OS shutdown). */

void runit_stop_all(void);

#endif /* __PCTERM_RUNIT_H */
