#define _XOPEN_SOURCE 700
#include "cmd_exec.h"
#include "shell_tab.h"
#include "history.h"
#include "multiwatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>

#define MAX_CMDS 32
#define MAX_ARGS 128

typedef struct {
    char *argv[MAX_ARGS];
    char *infile;
    char *outfile;
    int append;
    char storage[INPUT_CAP];
    size_t used;
} Parsed;

/* Small shell lexer: whitespace separates words; single/double quotes group words;
   |, <, > and >> are operators outside quotes. */
static char *store_token(Parsed *cmd, const char *token) {
    size_t n = strlen(token) + 1;
    if (cmd->used + n > sizeof(cmd->storage)) return NULL;
    char *dst = cmd->storage + cmd->used;
    memcpy(dst, token, n);
    cmd->used += n;
    return dst;
}

static int parse(const char *src, Parsed cmds[MAX_CMDS], int *ncmd) {
    memset(cmds, 0, sizeof(Parsed) * MAX_CMDS);
    int c = 0, a = 0;
    *ncmd = 1;
    const char *r = src;
    while (*r) {
        while (isspace((unsigned char)*r)) r++;
        if (!*r) break;
        if (*r == '|') {
            r++;
            if (c + 1 >= MAX_CMDS) return -1;
            c++;
            a = 0;
            (*ncmd)++;
            continue;
        }
        if (*r == '<' || *r == '>') {
            char op = *r++;
            int append = 0;
            if (op == '>' && *r == '>') {
                append = 1;
                r++;
            }
            while (isspace((unsigned char)*r)) r++;
            if (!*r || *r == '|' || *r == '<' || *r == '>') return -1;
            char token[INPUT_CAP];
            size_t n = 0;
            char quote = 0;
            if (*r == '\'' || *r == '"') quote = *r++;
            while (*r && (quote ? *r != quote : (!isspace((unsigned char)*r) && *r != '|' && *r != '<' && *r != '>'))) {
                if (n + 1 < sizeof(token)) token[n++] = *r;
                r++;
            }
            if (quote) {
                if (*r != quote) return -1;
                r++;
            }
            token[n] = '\0';
            char *v = store_token(&cmds[c], token);
            if (!v) return -1;
            if (op == '<') cmds[c].infile = v;
            else {
                cmds[c].outfile = v;
                cmds[c].append = append;
            }
            continue;
        }
        if (a >= MAX_ARGS - 1) return -1;
        char token[INPUT_CAP];
        size_t n = 0;
        char quote = 0;
        if (*r == '\'' || *r == '"') quote = *r++;
        while (*r && (quote ? *r != quote : (!isspace((unsigned char)*r) && *r != '|' && *r != '<' && *r != '>'))) {
            if (n + 1 < sizeof(token)) token[n++] = *r;
            r++;
        }
        if (quote) {
            if (*r != quote) return -1;
            r++;
        }
        token[n] = '\0';
        char *v = store_token(&cmds[c], token);
        if (!v) return -1;
        cmds[c].argv[a++] = v;
    }
    cmds[c].argv[a] = NULL;
    for (int i = 0; i < *ncmd; i++) {
        if (!cmds[i].argv[0]) return -1;
    }
    return 0;
}

static void output_error(ShellTab *t, const char *msg) {
    shell_tab_append_output(t, msg);
}

void cmd_exec_run(ShellTab *tab, const char *command) {
    while (isspace((unsigned char)*command)) command++;
    if (!*command) return;
    if (strcmp(command, "exit") == 0) {
        tab->alive = 0;
        return;
    }
    if (strcmp(command, "history") == 0) {
        history_show(tab);
        return;
    }
    if (strncmp(command, "cd", 2) == 0 && (command[2] == '\0' || isspace((unsigned char)command[2]))) {
        char path[INPUT_CAP];
        snprintf(path, sizeof(path), "%s", command + 2);
        char *p = path;
        while (isspace((unsigned char)*p)) p++;
        if (!*p) p = getenv("HOME");
        if (!p) {
            output_error(tab, "cd: HOME is not set\n");
            return;
        }
        char target[4096];
        if (*p == '/') snprintf(target, sizeof(target), "%s", p);
        else snprintf(target, sizeof(target), "%s/%s", tab->cwd, p);
        /* Resolve/validate directory without changing the GUI process cwd. */
        pid_t check = fork();
        if (check == 0) {
            if (chdir(target) < 0) _exit(1);
            _exit(0);
        }
        if (check < 0) {
            output_error(tab, "cd: fork failed\n");
            return;
        }
        int st;
        while (waitpid(check, &st, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            output_error(tab, "cd: no such directory\n");
            return;
        }
        /* Store normalized path where possible. */
        char resolved[4096];
        if (realpath(target, resolved)) snprintf(tab->cwd, sizeof(tab->cwd), "%s", resolved);
        return;
    }
    if (strncmp(command, "multiWatch", 10) == 0 && (command[10] == ' ' || command[10] == '\0')) {
        multiwatch_run(tab, command + 10);
        return;
    }

    Parsed cmds[MAX_CMDS];
    int ncmd = 0;
    if (parse(command, cmds, &ncmd) < 0) {
        output_error(tab, "myterm: parse error (check quotes/operators)\n");
        return;
    }
    int pipes[MAX_CMDS - 1][2];
    for (int i = 0; i < ncmd - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            output_error(tab, "myterm: pipe failed\n");
            return;
        }
    }
    int outpipe[2];
    if (pipe(outpipe) < 0) {
        output_error(tab, "myterm: output pipe failed\n");
        for (int j = 0; j < ncmd - 1; j++) {
            close(pipes[j][0]);
            close(pipes[j][1]);
        }
        return;
    }
    pid_t pids[MAX_CMDS];
    int spawned = 0;
    for (int i = 0; i < ncmd; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < ncmd - 1) dup2(pipes[i][1], STDOUT_FILENO);
            for (int j = 0; j < ncmd - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            close(outpipe[0]);
            /* Send diagnostics to GUI. Capture stdout of final stage unless redirected. */
            dup2(outpipe[1], STDERR_FILENO);
            if (i == ncmd - 1 && !cmds[i].outfile) dup2(outpipe[1], STDOUT_FILENO);
            close(outpipe[1]);
            if (chdir(tab->cwd) < 0) {
                dprintf(STDERR_FILENO, "myterm: cannot enter %s\n", tab->cwd);
                _exit(126);
            }
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd < 0) {
                    dprintf(STDERR_FILENO, "%s: %s\n", cmds[i].infile, strerror(errno));
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (cmds[i].outfile) {
                int flags = O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC);
                int fd = open(cmds[i].outfile, flags, 0666);
                if (fd < 0) {
                    dprintf(STDERR_FILENO, "%s: %s\n", cmds[i].outfile, strerror(errno));
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            execvp(cmds[i].argv[0], cmds[i].argv);
            dprintf(STDERR_FILENO, "%s: command not found\n", cmds[i].argv[0]);
            _exit(127);
        }
        if (pid < 0) {
            output_error(tab, "myterm: fork failed\n");
            break;
        }
        pids[spawned++] = pid;
    }
    for (int j = 0; j < ncmd - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }
    close(outpipe[1]);
    char out[2048];
    ssize_t nr;
    while ((nr = read(outpipe[0], out, sizeof(out) - 1)) > 0) {
        out[nr] = '\0';
        shell_tab_append_output(tab, out);
    }
    close(outpipe[0]);
    for (int i = 0; i < spawned; i++) {
        int status;
        while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR) {}
    }
}

void cmd_exec_worker(ShellTab *tab) {
    for (;;) {
        pthread_mutex_lock(&tab->mutex);
        while (tab->alive && !tab->command_pending) {
            pthread_cond_wait(&tab->cond, &tab->mutex);
        }
        if (!tab->alive) {
            pthread_mutex_unlock(&tab->mutex);
            break;
        }
        char cmd[INPUT_CAP];
        snprintf(cmd, sizeof(cmd), "%s", tab->command);
        tab->command_pending = 0;
        tab->busy = 1;
        pthread_mutex_unlock(&tab->mutex);
        
        cmd_exec_run(tab, cmd);
        
        pthread_mutex_lock(&tab->mutex);
        tab->busy = 0;
        pthread_mutex_unlock(&tab->mutex);
    }
}