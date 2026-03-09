/****************************************************************************
 * apps/pcterm/pcterm_nsh.c
 *
 * NuttShell integration: pseudo-terminal (PTY) creation and I/O bridge
 * between the terminal widget and NuttShell.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <signal.h>

#ifdef CONFIG_PSEUDOTERM
#include <pty.h>
#endif

#include <nuttx/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NSH_STACK_SIZE     4096
#define PTY_READ_BUF       256

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*nsh_output_cb)(const char *data, size_t len, void *user);

typedef struct pcterm_nsh_s
{
  int            master_fd;     /* PTY master (our side) */
  int            slave_fd;      /* PTY slave (NuttShell side) */
  pid_t          nsh_pid;       /* NuttShell task PID */
  pthread_t      read_thread;   /* Thread reading master_fd */
  bool           running;
  nsh_output_cb  output_cb;     /* Callback for NuttShell output */
  void          *output_user;
} pcterm_nsh_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Thread that reads NuttShell output from the PTY master and forwards
 * it to the terminal widget via callback.
 */
static void *nsh_read_thread(void *arg)
{
  pcterm_nsh_t *nsh = (pcterm_nsh_t *)arg;
  char buf[PTY_READ_BUF];

  syslog(LOG_INFO, "PCTERM: NSH read thread started\n");

  while (nsh->running)
    {
      ssize_t n = read(nsh->master_fd, buf, sizeof(buf));

      if (n > 0)
        {
          if (nsh->output_cb)
            {
              nsh->output_cb(buf, n, nsh->output_user);
            }
        }
      else if (n == 0)
        {
          /* EOF — NuttShell exited */
          break;
        }
      else
        {
          if (errno == EINTR) continue;
          break;
        }
    }

  syslog(LOG_INFO, "PCTERM: NSH read thread exiting\n");
  nsh->running = false;
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Start a NuttShell session with a pseudo-terminal.
 *
 * @param output_cb  Callback for NuttShell output data
 * @param user       User context for callback
 * @return NSH context, or NULL on failure
 */
pcterm_nsh_t *pcterm_nsh_start(nsh_output_cb output_cb, void *user)
{
  pcterm_nsh_t *nsh;

  nsh = (pcterm_nsh_t *)malloc(sizeof(pcterm_nsh_t));
  if (nsh == NULL) return NULL;

  memset(nsh, 0, sizeof(pcterm_nsh_t));
  nsh->output_cb   = output_cb;
  nsh->output_user = user;
  nsh->master_fd   = -1;
  nsh->slave_fd    = -1;

#ifdef CONFIG_PSEUDOTERM
  /* Create pseudo-terminal pair */

  int master, slave;

  if (openpty(&master, &slave, NULL, NULL, NULL) < 0)
    {
      syslog(LOG_ERR, "PCTERM: openpty failed: %d\n", errno);
      free(nsh);
      return NULL;
    }

  nsh->master_fd = master;
  nsh->slave_fd  = slave;

  /* Set master to non-blocking for reads */

  int flags = fcntl(master, F_GETFL, 0);
  fcntl(master, F_SETFL, flags | O_NONBLOCK);

  /* Start NuttShell task with slave PTY as stdin/stdout/stderr.
   *
   * NuttX task_create runs the task in the same address space.
   * We use a wrapper function that redirects stdio to the slave PTY
   * before calling the NuttShell entry point.
   */

  /* Save current stdio for restoration after fork */

  int saved_stdin  = dup(STDIN_FILENO);
  int saved_stdout = dup(STDOUT_FILENO);
  int saved_stderr = dup(STDERR_FILENO);

  /* Redirect stdio to slave PTY */

  dup2(slave, STDIN_FILENO);
  dup2(slave, STDOUT_FILENO);
  dup2(slave, STDERR_FILENO);

  /* Create NuttShell task */

  extern int nsh_main(int argc, char *argv[]);
  nsh->nsh_pid = task_create("pcnsh", 100, NSH_STACK_SIZE,
                             (main_t)nsh_main, NULL);

  /* Restore parent's stdio */

  dup2(saved_stdin,  STDIN_FILENO);
  dup2(saved_stdout, STDOUT_FILENO);
  dup2(saved_stderr, STDERR_FILENO);

  close(saved_stdin);
  close(saved_stdout);
  close(saved_stderr);

  if (nsh->nsh_pid < 0)
    {
      syslog(LOG_ERR, "PCTERM: task_create failed: %d\n", errno);
      close(master);
      close(slave);
      free(nsh);
      return NULL;
    }

  syslog(LOG_INFO, "PCTERM: PTY created (master=%d, slave=%d), "
         "NSH pid=%d\n", master, slave, nsh->nsh_pid);

#else
  syslog(LOG_WARNING, "PCTERM: PSEUDOTERM not configured\n");
  free(nsh);
  return NULL;
#endif

  /* Start read thread */

  nsh->running = true;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2048);
  pthread_create(&nsh->read_thread, &attr, nsh_read_thread, nsh);
  pthread_attr_destroy(&attr);

  return nsh;
}

/**
 * Send keyboard input to the NuttShell.
 *
 * @param nsh   NSH context
 * @param data  Input data (ASCII or ANSI escape sequences)
 * @param len   Data length
 */
int pcterm_nsh_write(pcterm_nsh_t *nsh, const char *data, size_t len)
{
  if (nsh == NULL || nsh->master_fd < 0) return -1;

  return write(nsh->master_fd, data, len);
}

/**
 * Stop the NuttShell session and clean up.
 */
void pcterm_nsh_stop(pcterm_nsh_t *nsh)
{
  if (nsh == NULL) return;

  nsh->running = false;

  /* Kill the NuttShell task if running */

  if (nsh->nsh_pid > 0)
    {
      kill(nsh->nsh_pid, SIGTERM);
      nsh->nsh_pid = 0;
    }

  /* Wait for read thread */

  pthread_join(nsh->read_thread, NULL);

  /* Close PTY file descriptors */

  if (nsh->master_fd >= 0) close(nsh->master_fd);
  if (nsh->slave_fd >= 0)  close(nsh->slave_fd);

  free(nsh);

  syslog(LOG_INFO, "PCTERM: NSH session stopped\n");
}
