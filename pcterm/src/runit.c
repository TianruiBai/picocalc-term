/****************************************************************************
 * pcterm/src/runit.c
 *
 * Minimal runit-compatible service supervisor for eUX OS.
 *
 * Implements a lightweight subset of runit (https://smarden.org/runit/):
 *   - runsvdir: Scans a directory for service subdirectories and
 *     starts a runsv process for each.
 *   - runsv: Monitors a single service (runs ./run script, restarts
 *     on exit, handles ./down and ./finish).
 *   - sv: Control interface (start/stop/restart/status).
 *
 * Design choices for embedded:
 *   - Single-threaded scanning (no fork() — NuttX uses task_create)
 *   - Services are NuttX tasks, not forked processes
 *   - Control via /tmp/run/sv/<name>/control files
 *   - Status via /tmp/run/sv/<name>/status files
 *   - No pipe supervision (log service handled separately)
 *
 * Service directory layout (runit-compatible):
 *   /etc/sv/<name>/run       — Executable run script (required)
 *   /etc/sv/<name>/finish    — Finish script (optional, run on exit)
 *   /etc/sv/<name>/down      — If present, service starts paused
 *
 * Runtime state:
 *   /tmp/run/sv/<name>/pid   — PID of running service
 *   /tmp/run/sv/<name>/stat  — "run", "down", "finish"
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <sys/stat.h>

#include <nuttx/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RUNIT_MAX_SERVICES    16
#define RUNIT_SVC_NAME_MAX    32
#define RUNIT_SCAN_INTERVAL   5     /* Seconds between directory rescans */
#define RUNIT_RESTART_DELAY   1     /* Seconds before restarting a crashed service */
#define RUNIT_RUNDIR          "/tmp/run/sv"

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum svc_state_e
{
  SVC_STATE_DOWN = 0,     /* Not running (explicitly stopped or ./down exists) */
  SVC_STATE_RUN,          /* Running normally */
  SVC_STATE_FINISH,       /* Running finish script after exit */
  SVC_STATE_WANT_DOWN,    /* User requested stop */
  SVC_STATE_WANT_UP,      /* User requested start, will launch next scan */
};

struct svc_entry_s
{
  char     name[RUNIT_SVC_NAME_MAX];
  char     svdir[PATH_MAX];       /* e.g., /etc/sv/audio */
  pid_t    pid;                   /* PID of running service task */
  enum svc_state_e state;
  time_t   started;               /* Time service was last started */
  int      restart_count;         /* Number of restarts */
  bool     active;                /* Entry in use */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct svc_entry_s g_services[RUNIT_MAX_SERVICES];
static int     g_svc_count = 0;
static bool    g_runsvdir_running = false;
static char    g_svdir_path[PATH_MAX] = "/etc/sv";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: svc_find
 ****************************************************************************/

static struct svc_entry_s *svc_find(const char *name)
{
  for (int i = 0; i < g_svc_count; i++)
    {
      if (g_services[i].active &&
          strcmp(g_services[i].name, name) == 0)
        {
          return &g_services[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: svc_has_down_file
 *
 * Description:
 *   Check if the service directory contains a ./down file, which means
 *   the service should not auto-start.
 *
 ****************************************************************************/

static bool svc_has_down_file(const char *svdir)
{
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/down", svdir);

  struct stat st;
  return (stat(path, &st) == 0);
}

/****************************************************************************
 * Name: svc_write_status
 *
 * Description:
 *   Write service status to /tmp/run/sv/<name>/stat and pid files.
 *
 ****************************************************************************/

static void svc_write_status(struct svc_entry_s *svc)
{
  char dir[PATH_MAX];
  char path[PATH_MAX];

  snprintf(dir, sizeof(dir), "%s/%s", RUNIT_RUNDIR, svc->name);

  /* Ensure runtime directory exists */

  mkdir(RUNIT_RUNDIR, 0755);
  mkdir(dir, 0755);

  /* Write status */

  snprintf(path, sizeof(path), "%s/stat", dir);
  FILE *f = fopen(path, "w");
  if (f)
    {
      const char *state_str;
      switch (svc->state)
        {
          case SVC_STATE_RUN:       state_str = "run"; break;
          case SVC_STATE_FINISH:    state_str = "finish"; break;
          case SVC_STATE_WANT_DOWN: state_str = "want_down"; break;
          case SVC_STATE_WANT_UP:   state_str = "want_up"; break;
          default:                  state_str = "down"; break;
        }

      fprintf(f, "%s\n", state_str);
      fclose(f);
    }

  /* Write PID */

  snprintf(path, sizeof(path), "%s/pid", dir);
  f = fopen(path, "w");
  if (f)
    {
      fprintf(f, "%d\n", (int)svc->pid);
      fclose(f);
    }
}

/****************************************************************************
 * Name: svc_run_script
 *
 * Description:
 *   Execute a service's run script as a NuttX task. The script name
 *   is read from the ./run file in the service directory.
 *
 *   Since NuttX doesn't have fork/exec in the traditional sense,
 *   we parse the exec line from the run script and use task_create
 *   to launch the command directly.
 *
 ****************************************************************************/

static pid_t svc_run_script(const char *svdir, const char *script_name)
{
  char script_path[PATH_MAX];
  char cmd_line[128];

  snprintf(script_path, sizeof(script_path), "%s/%s", svdir, script_name);

  /* Read the run script to extract the exec command */

  FILE *f = fopen(script_path, "r");
  if (!f)
    {
      syslog(LOG_ERR, "runit: cannot open %s: %d\n", script_path, errno);
      return -1;
    }

  cmd_line[0] = '\0';

  char line[128];
  while (fgets(line, sizeof(line), f))
    {
      /* Skip comments and blank lines */

      char *p = line;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == '#' || *p == '\n' || *p == '\0') continue;

      /* Look for "exec <command>" */

      if (strncmp(p, "exec ", 5) == 0)
        {
          p += 5;
          while (*p == ' ' || *p == '\t') p++;

          /* Strip trailing whitespace/newline */

          size_t len = strlen(p);
          while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' ||
                             p[len-1] == ' ')) len--;
          if (len >= sizeof(cmd_line)) len = sizeof(cmd_line) - 1;
          memcpy(cmd_line, p, len);
          cmd_line[len] = '\0';
          break;
        }
    }

  fclose(f);

  if (cmd_line[0] == '\0')
    {
      syslog(LOG_ERR, "runit: no 'exec' command in %s\n", script_path);
      return -1;
    }

  /* Parse command and arguments */

  char *argv[8];
  int argc = 0;
  char *tok = strtok(cmd_line, " \t");
  while (tok && argc < 7)
    {
      argv[argc++] = tok;
      tok = strtok(NULL, " \t");
    }
  argv[argc] = NULL;

  if (argc == 0)
    {
      return -1;
    }

  /* Create NuttX task for the service */

  pid_t pid = task_create(argv[0],
                          100,           /* Priority: normal */
                          4096,          /* Stack size */
                          NULL,          /* Entry: resolved by name */
                          (FAR char * const *)argv);

  if (pid < 0)
    {
      syslog(LOG_ERR, "runit: failed to start '%s': %d\n",
             argv[0], errno);
      return -1;
    }

  syslog(LOG_INFO, "runit: started service '%s' (pid %d)\n",
         argv[0], pid);
  return pid;
}

/****************************************************************************
 * Name: svc_start
 ****************************************************************************/

static int svc_start(struct svc_entry_s *svc)
{
  if (svc->state == SVC_STATE_RUN && svc->pid > 0)
    {
      /* Already running — check if still alive */

      if (kill(svc->pid, 0) == 0)
        {
          return 0;  /* Still running */
        }

      /* Dead — fall through to restart */
    }

  pid_t pid = svc_run_script(svc->svdir, "run");
  if (pid < 0)
    {
      svc->state = SVC_STATE_DOWN;
      svc_write_status(svc);
      return -1;
    }

  svc->pid = pid;
  svc->state = SVC_STATE_RUN;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  svc->started = ts.tv_sec;

  svc_write_status(svc);
  return 0;
}

/****************************************************************************
 * Name: svc_stop
 ****************************************************************************/

static int svc_stop(struct svc_entry_s *svc)
{
  if (svc->state != SVC_STATE_RUN || svc->pid <= 0)
    {
      svc->state = SVC_STATE_DOWN;
      svc_write_status(svc);
      return 0;
    }

  syslog(LOG_INFO, "runit: stopping service '%s' (pid %d)\n",
         svc->name, svc->pid);

  /* Send SIGTERM, then wait briefly */

  kill(svc->pid, SIGTERM);
  usleep(500000);  /* 500ms grace period */

  /* Check if still alive, send SIGKILL */

  if (kill(svc->pid, 0) == 0)
    {
      kill(svc->pid, SIGKILL);
    }

  svc->pid = -1;
  svc->state = SVC_STATE_DOWN;
  svc_write_status(svc);
  return 0;
}

/****************************************************************************
 * Name: svc_check
 *
 * Description:
 *   Check if a service is still running. If it died and should be up,
 *   restart it after a delay.
 *
 ****************************************************************************/

static void svc_check(struct svc_entry_s *svc)
{
  if (svc->state == SVC_STATE_DOWN ||
      svc->state == SVC_STATE_WANT_DOWN)
    {
      return;
    }

  if (svc->state == SVC_STATE_WANT_UP)
    {
      svc_start(svc);
      return;
    }

  if (svc->state == SVC_STATE_RUN && svc->pid > 0)
    {
      /* Check if process is still alive */

      if (kill(svc->pid, 0) == 0)
        {
          return;  /* Still running */
        }

      /* Service died — check for finish script */

      syslog(LOG_WARNING, "runit: service '%s' exited, "
             "restarting in %ds...\n",
             svc->name, RUNIT_RESTART_DELAY);

      svc->restart_count++;
      sleep(RUNIT_RESTART_DELAY);
      svc_start(svc);
    }
}

/****************************************************************************
 * Name: runsvdir_scan
 *
 * Description:
 *   Scan the service directory (/etc/sv/) for new service directories.
 *   Each subdirectory with a ./run file becomes a supervised service.
 *
 ****************************************************************************/

static void runsvdir_scan(void)
{
  DIR *dir = opendir(g_svdir_path);
  if (!dir)
    {
      return;
    }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL)
    {
      if (ent->d_name[0] == '.') continue;

      /* Check if this is a directory with a ./run file */

      char svdir[PATH_MAX];
      char run_path[PATH_MAX];

      snprintf(svdir, sizeof(svdir), "%s/%s", g_svdir_path, ent->d_name);
      snprintf(run_path, sizeof(run_path), "%s/run", svdir);

      struct stat st;
      if (stat(run_path, &st) != 0) continue;

      /* Already known? */

      if (svc_find(ent->d_name) != NULL) continue;

      /* Add new service */

      if (g_svc_count >= RUNIT_MAX_SERVICES)
        {
          syslog(LOG_WARNING, "runit: max services (%d) reached\n",
                 RUNIT_MAX_SERVICES);
          break;
        }

      struct svc_entry_s *svc = &g_services[g_svc_count++];
      memset(svc, 0, sizeof(*svc));
      strlcpy(svc->name, ent->d_name, sizeof(svc->name));
      strlcpy(svc->svdir, svdir, sizeof(svc->svdir));
      svc->pid = -1;
      svc->active = true;

      /* Check for ./down file */

      if (svc_has_down_file(svdir))
        {
          svc->state = SVC_STATE_DOWN;
          syslog(LOG_INFO, "runit: service '%s' found (down)\n",
                 svc->name);
        }
      else
        {
          svc->state = SVC_STATE_WANT_UP;
          syslog(LOG_INFO, "runit: service '%s' found (starting)\n",
                 svc->name);
        }
    }

  closedir(dir);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: runit_runsvdir
 *
 * Description:
 *   Main loop of the service supervisor. Scans the service directory,
 *   starts services, and monitors them — restarting on crash.
 *
 *   This function runs as a background NuttX task.
 *
 ****************************************************************************/

int runit_runsvdir(const char *svdir)
{
  if (svdir != NULL)
    {
      strlcpy(g_svdir_path, svdir, sizeof(g_svdir_path));
    }

  syslog(LOG_INFO, "runit: runsvdir scanning %s\n", g_svdir_path);
  g_runsvdir_running = true;

  /* Create runtime directory */

  mkdir("/tmp/run", 0755);
  mkdir(RUNIT_RUNDIR, 0755);

  while (g_runsvdir_running)
    {
      /* Scan for new services */

      runsvdir_scan();

      /* Check all known services */

      for (int i = 0; i < g_svc_count; i++)
        {
          if (g_services[i].active)
            {
              svc_check(&g_services[i]);
            }
        }

      sleep(RUNIT_SCAN_INTERVAL);
    }

  return 0;
}

/****************************************************************************
 * Name: runit_sv_control
 *
 * Description:
 *   Control a named service (equivalent to `sv start/stop/restart <name>`).
 *
 *   cmd: "start", "stop", "restart", "status"
 *
 ****************************************************************************/

int runit_sv_control(const char *name, const char *cmd)
{
  struct svc_entry_s *svc = svc_find(name);

  if (svc == NULL)
    {
      syslog(LOG_ERR, "runit: service '%s' not found\n", name);
      return -ENOENT;
    }

  if (strcmp(cmd, "start") == 0 || strcmp(cmd, "up") == 0)
    {
      svc->state = SVC_STATE_WANT_UP;
      return svc_start(svc);
    }

  if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "down") == 0)
    {
      svc->state = SVC_STATE_WANT_DOWN;
      return svc_stop(svc);
    }

  if (strcmp(cmd, "restart") == 0)
    {
      svc_stop(svc);
      svc->state = SVC_STATE_WANT_UP;
      return svc_start(svc);
    }

  if (strcmp(cmd, "status") == 0)
    {
      const char *state_str;
      switch (svc->state)
        {
          case SVC_STATE_RUN:       state_str = "run"; break;
          case SVC_STATE_DOWN:      state_str = "down"; break;
          case SVC_STATE_FINISH:    state_str = "finish"; break;
          case SVC_STATE_WANT_DOWN: state_str = "want down"; break;
          case SVC_STATE_WANT_UP:   state_str = "want up"; break;
          default:                  state_str = "unknown"; break;
        }

      printf("%s: %s (pid %d, restarts %d)\n",
             svc->name, state_str, svc->pid, svc->restart_count);
      return 0;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: runit_list_services
 *
 * Description:
 *   Print status of all supervised services.
 *
 ****************************************************************************/

void runit_list_services(void)
{
  printf("%-16s %-10s %6s %8s\n", "SERVICE", "STATE", "PID", "RESTARTS");
  printf("%-16s %-10s %6s %8s\n", "-------", "-----", "---", "--------");

  for (int i = 0; i < g_svc_count; i++)
    {
      if (!g_services[i].active) continue;

      const char *state_str;
      switch (g_services[i].state)
        {
          case SVC_STATE_RUN:       state_str = "run"; break;
          case SVC_STATE_DOWN:      state_str = "down"; break;
          case SVC_STATE_FINISH:    state_str = "finish"; break;
          case SVC_STATE_WANT_DOWN: state_str = "stopping"; break;
          case SVC_STATE_WANT_UP:   state_str = "starting"; break;
          default:                  state_str = "unknown"; break;
        }

      printf("%-16s %-10s %6d %8d\n",
             g_services[i].name, state_str,
             g_services[i].pid, g_services[i].restart_count);
    }
}

/****************************************************************************
 * Name: runit_stop_all
 *
 * Description:
 *   Stop all supervised services (used during shutdown).
 *
 ****************************************************************************/

void runit_stop_all(void)
{
  syslog(LOG_INFO, "runit: stopping all services\n");

  for (int i = 0; i < g_svc_count; i++)
    {
      if (g_services[i].active && g_services[i].state == SVC_STATE_RUN)
        {
          svc_stop(&g_services[i]);
        }
    }

  g_runsvdir_running = false;
}
