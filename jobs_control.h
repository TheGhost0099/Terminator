#ifndef JOBS_CONTROL_H
#define JOBS_CONTROL_H
#include <sys/types.h>

/*
 * Job control, like in a Linux shell:
 *   Ctrl+Z      stop the command running in the foreground
 *   cmd &       start a command in the background
 *   jobs        list stopped / background jobs
 *   fg [%n]     bring a job to the foreground (wakes it if stopped)
 *   bg [%n]     let a stopped job continue running in the background
 *
 * Every command started by cmd_exec.c becomes a "job". A job is one
 * process group (all processes of a pipeline share it), so one signal
 * sent with kill(-pgid, ...) reaches the whole pipeline.
 */

struct ShellTab;

/* Register a freshly started command as a job. Returns the job's slot
   number (used by jobs_wait_foreground), or -1 if it could not be added. */
int jobs_add(struct ShellTab *tab, pid_t pgid, pid_t *pids, int npids,
             int out_fd, const char *cmd, int background);

/* Block until the job finishes or is stopped by Ctrl+Z. */
void jobs_wait_foreground(struct ShellTab *tab, int slot);

/* Ctrl+Z: send SIGTSTP to the tab's foreground job. Returns 1 if one was running. */
int jobs_stop_foreground(struct ShellTab *tab);

/* Handle the built-in commands "jobs", "fg" and "bg".
   Returns 1 if the command was one of them, 0 otherwise. */
int jobs_builtin(struct ShellTab *tab, const char *command);

/* Called regularly from the main loop: collects output from all jobs and
   notices jobs that stopped, continued or finished. */
void jobs_poll(void);

/* Kill and forget every job of a tab (used when the tab is closed). */
void jobs_close_tab(struct ShellTab *tab);

#endif
