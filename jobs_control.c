#define _XOPEN_SOURCE 700
#include "jobs_control.h"
#include "shell_tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAX_JOBS 32
#define MAX_PROCS 32

enum { JOB_FREE, JOB_RUNNING, JOB_STOPPED, JOB_DONE };

typedef struct {
    int state;               /* JOB_FREE means this slot is unused */
    int id;                  /* number shown to the user: [1], [2], ... */
    struct ShellTab *tab;    /* the tab (shell) this job belongs to */
    pid_t pgid;              /* process group of the job */
    pid_t pids[MAX_PROCS];   /* every process of the pipeline */
    int exited[MAX_PROCS];   /* 1 once that process has finished */
    int npids;
    int out_fd;              /* pipe we read the job's output from (-1 = closed) */
    int foreground;          /* 1 while the shell is waiting for this job */
    char cmd[256];           /* command text, for "jobs" and messages */
} Job;

/* One table for all tabs. The GUI thread and the tab worker threads both
   use it, so every access is protected by jobs_lock. */
static Job jobs[MAX_JOBS];
static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- small helpers (call them with jobs_lock held) ---------- */

static const char *state_name(int state) {
    if (state == JOB_STOPPED) return "Stopped";
    if (state == JOB_DONE) return "Done";
    return "Running";
}

static void print_job(Job *j, char mark) {
    char line[400];
    snprintf(line, sizeof(line), "[%d]%c  %-10s %s\n", j->id, mark, state_name(j->state), j->cmd);
    shell_tab_append_output(j->tab, line);
}

static void free_job(Job *j) {
    if (j->out_fd >= 0) close(j->out_fd);
    j->out_fd = -1;
    j->state = JOB_FREE;
}

/* The "current" job (marked + by bash) is simply the newest one of the tab. */
static Job *current_job(struct ShellTab *tab) {
    Job *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state == JOB_FREE || j->state == JOB_DONE || j->tab != tab) continue;
        if (!best || j->id > best->id) best = j;
    }
    return best;
}

static Job *find_job(struct ShellTab *tab, int id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state != JOB_FREE && j->state != JOB_DONE && j->tab == tab && j->id == id) return j;
    }
    return NULL;
}

/* Copy whatever output is waiting in the pipe into the tab.
   The pipe is non-blocking, so this never waits. */
static void read_output(Job *j) {
    if (j->out_fd < 0) return;
    char buf[2048];
    ssize_t n;
    while ((n = read(j->out_fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        shell_tab_append_output(j->tab, buf);
    }
    if (n == 0) {          /* end of file: every process closed the pipe */
        close(j->out_fd);
        j->out_fd = -1;
    }
}

/* Ask the kernel (without waiting) what happened to each process.
   WUNTRACED  -> also tell us when a process is stopped (Ctrl+Z)
   WCONTINUED -> also tell us when a stopped process continues      */
static void update_state(Job *j) {
    int alive = 0;
    for (int i = 0; i < j->npids; i++) {
        if (j->exited[i]) continue;
        int status;
        pid_t r;
        while ((r = waitpid(j->pids[i], &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                j->exited[i] = 1;
                break;
            }
            if (WIFSTOPPED(status)) j->state = JOB_STOPPED;
            if (WIFCONTINUED(status)) j->state = JOB_RUNNING;
        }
        if (r < 0) j->exited[i] = 1;   /* process no longer exists */
        if (!j->exited[i]) alive++;
    }
    if (alive == 0) j->state = JOB_DONE;
}

static void short_sleep(void) {
    struct timespec ts = {0, 10000000};   /* 10 ms */
    nanosleep(&ts, NULL);
}

/* ---------------------------- public API ---------------------------- */

int jobs_add(struct ShellTab *tab, pid_t pgid, pid_t *pids, int npids,
             int out_fd, const char *cmd, int background) {
    pthread_mutex_lock(&jobs_lock);
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state == JOB_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&jobs_lock);
        shell_tab_append_output(tab, "myterm: too many jobs\n");
        kill(-pgid, SIGKILL);
        for (int i = 0; i < npids; i++) waitpid(pids[i], NULL, 0);
        close(out_fd);
        return -1;
    }

    /* New job number = highest number used in this tab + 1 (like bash). */
    int id = 1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].state != JOB_FREE && jobs[i].tab == tab && jobs[i].id >= id) id = jobs[i].id + 1;
    }

    Job *j = &jobs[slot];
    memset(j, 0, sizeof(*j));
    j->state = JOB_RUNNING;
    j->id = id;
    j->tab = tab;
    j->pgid = pgid;
    j->npids = npids < MAX_PROCS ? npids : MAX_PROCS;
    for (int i = 0; i < j->npids; i++) j->pids[i] = pids[i];
    j->out_fd = out_fd;
    j->foreground = !background;
    snprintf(j->cmd, sizeof(j->cmd), "%s", cmd);

    /* Non-blocking, so jobs_poll() can check the pipe without getting stuck. */
    fcntl(out_fd, F_SETFL, fcntl(out_fd, F_GETFL) | O_NONBLOCK);

    if (background) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[%d] %d\n", id, (int)pgid);
        shell_tab_append_output(tab, msg);
    }
    pthread_mutex_unlock(&jobs_lock);
    return slot;
}

void jobs_wait_foreground(struct ShellTab *tab, int slot) {
    if (slot < 0) return;

    pthread_mutex_lock(&jobs_lock);
    pid_t pgid = jobs[slot].pgid;
    pthread_mutex_unlock(&jobs_lock);

    /* Tell the tab which group Ctrl+C should interrupt. */
    pthread_mutex_lock(&tab->mutex);
    tab->fg_pgid = pgid;
    pthread_mutex_unlock(&tab->mutex);

    /* jobs_poll() (main loop) keeps updating the state; we just watch it. */
    for (;;) {
        pthread_mutex_lock(&jobs_lock);
        int state = jobs[slot].state;
        if (state == JOB_DONE) {
            read_output(&jobs[slot]);   /* last bits of output */
            free_job(&jobs[slot]);
        } else if (state == JOB_STOPPED) {
            jobs[slot].foreground = 0;  /* stays in the list; "fg"/"bg" can resume it */
        }
        pthread_mutex_unlock(&jobs_lock);
        if (state != JOB_RUNNING) break;
        short_sleep();
    }

    pthread_mutex_lock(&tab->mutex);
    tab->fg_pgid = 0;
    pthread_mutex_unlock(&tab->mutex);
}

int jobs_stop_foreground(struct ShellTab *tab) {
    int found = 0;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->tab == tab && j->foreground && j->state == JOB_RUNNING) {
            kill(-j->pgid, SIGTSTP);    /* same signal a real terminal sends on Ctrl+Z */
            found = 1;
        }
    }
    pthread_mutex_unlock(&jobs_lock);
    return found;
}

void jobs_poll(void) {
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state == JOB_FREE) continue;
        int old_state = j->state;

        read_output(j);
        update_state(j);

        if (j->state == JOB_STOPPED && old_state != JOB_STOPPED) {
            print_job(j, '+');                  /* "[1]+  Stopped    sleep 100" */
        }
        if (j->state == JOB_DONE && old_state != JOB_DONE) {
            read_output(j);
            if (!j->foreground) {               /* the fg waiter cleans up its own job */
                print_job(j, '+');              /* "[1]+  Done       sleep 5" */
                free_job(j);
            }
        }
    }
    pthread_mutex_unlock(&jobs_lock);
}

void jobs_close_tab(struct ShellTab *tab) {
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->state == JOB_FREE || j->tab != tab) continue;
        kill(-j->pgid, SIGKILL);                /* SIGKILL also works on stopped processes */
        for (int p = 0; p < j->npids; p++) {
            if (!j->exited[p]) waitpid(j->pids[p], NULL, 0);
            j->exited[p] = 1;
        }
        j->state = JOB_DONE;
        if (!j->foreground) free_job(j);        /* the fg waiter frees its own job */
    }
    pthread_mutex_unlock(&jobs_lock);
}

/* ------------------------ jobs / fg / bg ------------------------ */

/* Is `command` exactly `word`, optionally followed by spaces and arguments? */
static int is_word(const char *command, const char *word) {
    size_t n = strlen(word);
    return strncmp(command, word, n) == 0 && (command[n] == '\0' || isspace((unsigned char)command[n]));
}

int jobs_builtin(struct ShellTab *tab, const char *command) {
    if (is_word(command, "jobs")) {
        pthread_mutex_lock(&jobs_lock);
        Job *cur = current_job(tab);
        int max_id = cur ? cur->id : 0;
        for (int id = 1; id <= max_id; id++) {     /* print in job-number order */
            Job *j = find_job(tab, id);
            if (j) print_job(j, j == cur ? '+' : ' ');
        }
        pthread_mutex_unlock(&jobs_lock);
        return 1;
    }

    int is_fg = is_word(command, "fg");
    int is_bg = is_word(command, "bg");
    if (!is_fg && !is_bg) return 0;

    /* Optional argument: "fg 2" or "fg %2". No argument = current job. */
    const char *arg = command + 2;
    while (isspace((unsigned char)*arg)) arg++;
    if (*arg == '%') arg++;
    int want = atoi(arg);

    pthread_mutex_lock(&jobs_lock);
    Job *j = want ? find_job(tab, want) : current_job(tab);
    if (!j) {
        pthread_mutex_unlock(&jobs_lock);
        shell_tab_append_output(tab, is_fg ? "fg: no such job\n" : "bg: no such job\n");
        return 1;
    }

    char msg[400];
    if (is_fg) {
        snprintf(msg, sizeof(msg), "%s\n", j->cmd);
        shell_tab_append_output(tab, msg);
        j->foreground = 1;
        j->state = JOB_RUNNING;
        kill(-j->pgid, SIGCONT);                 /* wake it up (harmless if already running) */
        int slot = (int)(j - jobs);
        pthread_mutex_unlock(&jobs_lock);
        jobs_wait_foreground(tab, slot);         /* now the shell waits for it again */
        return 1;
    }

    /* bg */
    if (j->state == JOB_RUNNING) {
        snprintf(msg, sizeof(msg), "bg: job %d already in background\n", j->id);
    } else {
        j->state = JOB_RUNNING;
        kill(-j->pgid, SIGCONT);                 /* wake it up, but don't wait for it */
        snprintf(msg, sizeof(msg), "[%d]+ %s &\n", j->id, j->cmd);
    }
    shell_tab_append_output(tab, msg);
    pthread_mutex_unlock(&jobs_lock);
    return 1;
}
