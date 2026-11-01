#include "multiwatch.h"
#include "shell_tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

int multiwatch_run(ShellTab *tab, const char *args) {
    while (*args == ' ' || *args == '\t') args++;
    if (!*args) {
        shell_tab_append_output(tab, "Usage: multiWatch \"cmd1\" \"cmd2\" ...\n");
        return -1;
    }
    
    /* Parse quoted command strings, start each concurrently, collect output through files. */
    char copy[4096];
    snprintf(copy, sizeof(copy), "%s", args);
    char *commands[16];
    int n = 0;
    char *p = copy;
    
    while (*p && n < 16) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        if (*p == '\"' || *p == '\'') {
            char q = *p++;
            commands[n++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = '\0';
        } else {
            commands[n++] = p;
            while (*p && *p != ',') p++;
            if (*p) *p++ = '\0';
        }
    }
    
    if (!n) {
        shell_tab_append_output(tab, "multiWatch: no commands supplied\n");
        return -1;
    }
    
    pid_t pids[16];
    char paths[16][128];
    pid_t pgid = 0;   /* all watched commands share one process group (for Ctrl+C) */

    for (int i = 0; i < n; i++) {
        snprintf(paths[i], sizeof(paths[i]), "/tmp/.myterm_multiwatch_%d_%d.txt", (int)getpid(), i);
        pid_t pid = fork();
        if (pid == 0) {
            setpgid(0, pgid);
            FILE *f = fopen(paths[i], "w");
            if (f) {
                dup2(fileno(f), STDOUT_FILENO);
                dup2(fileno(f), STDERR_FILENO);
                fclose(f);
            }
            execl("/bin/sh", "sh", "-c", commands[i], (char *)NULL);
            _exit(127);
        }
        pids[i] = pid;
        if (pid < 0) continue;
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);
        pthread_mutex_lock(&tab->mutex);
        tab->fg_pgid = pgid;
        pthread_mutex_unlock(&tab->mutex);
    }
    
    for (int i = 0; i < n; i++) {
        int st;
        if (pids[i] > 0) waitpid(pids[i], &st, 0);
        FILE *f = fopen(paths[i], "r");
        if (f) {
            char line[1024];
            time_t now = time(NULL);
            char head[256];
            snprintf(head, sizeof(head), "\n\"%s\", %ld:\n----------------------------------------\n", commands[i], (long)now);
            shell_tab_append_output(tab, head);
            while (fgets(line, sizeof(line), f)) {
                shell_tab_append_output(tab, line);
            }
            fclose(f);
        }
        unlink(paths[i]);
    }
    pthread_mutex_lock(&tab->mutex);
    tab->fg_pgid = 0;
    pthread_mutex_unlock(&tab->mutex);

    return 0;
}