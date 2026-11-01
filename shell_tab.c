#include "shell_tab.h"
#include "cmd_exec.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ShellTab tabs[TAB_MAX];
static int count = 0, active = 0;

static void *worker_main(void *p) {
    cmd_exec_worker((ShellTab *)p);
    return NULL;
}

int shell_tabs_init(void) {
    return shell_tabs_create() >= 0 ? 0 : -1;
}

int shell_tabs_create(void) {
    if (count >= TAB_MAX) return -1;
    int i = count++;
    ShellTab *t = &tabs[i];
    memset(t, 0, sizeof(*t));
    t->id = i + 1;
    t->alive = 1;
    t->history_pos = 0;
    
    pthread_mutex_init(&t->mutex, NULL);
    pthread_cond_init(&t->cond, NULL);
    
    if (!getcwd(t->cwd, sizeof(t->cwd))) {
        snprintf(t->cwd, sizeof(t->cwd), "/");
    }
    snprintf(t->title, sizeof(t->title), "Tab %d", t->id);
    history_init(t);
    
    if (pthread_create(&t->worker, NULL, worker_main, t) != 0) {
        t->alive = 0;
        count--;
        return -1;
    }
    t->worker_started = 1;
    active = i;
    return i;
}

void shell_tabs_close(int index) {
    if (index < 0 || index >= count) return;
    tabs[index].alive = 0;
    pthread_mutex_lock(&tabs[index].mutex);
    pthread_cond_broadcast(&tabs[index].cond);
    pthread_mutex_unlock(&tabs[index].mutex);
    if (tabs[index].worker_started) {
        pthread_join(tabs[index].worker, NULL);
    }
}

void shell_tabs_shutdown(void) {
    for (int i = 0; i < count; i++) {
        shell_tabs_close(i);
    }
    for (int i = 0; i < count; i++) {
        pthread_mutex_destroy(&tabs[i].mutex);
        pthread_cond_destroy(&tabs[i].cond);
    }
}

int shell_tabs_count(void) {
    return count;
}

int shell_tabs_active(void) {
    return active;
}

int shell_tabs_switch(int i) {
    if (i < 0 || i >= count || !tabs[i].alive) return -1;
    active = i;
    return 0;
}

ShellTab *shell_tab_get(int i) {
    return i >= 0 && i < count && tabs[i].alive ? &tabs[i] : NULL;
}

void shell_tab_append_output(ShellTab *t, const char *s) {
    if (!t || !s) return;
    pthread_mutex_lock(&t->mutex);
    size_t n = strlen(s);
    if (n >= OUTPUT_CAP) {
        s += n - (OUTPUT_CAP - 1);
        n = OUTPUT_CAP - 1;
    }
    if (t->output_len + n >= OUTPUT_CAP) {
        size_t drop = t->output_len + n - (OUTPUT_CAP - 1);
        if (drop > t->output_len) drop = t->output_len;
        memmove(t->output, t->output + drop, t->output_len - drop);
        t->output_len -= drop;
    }
    memcpy(t->output + t->output_len, s, n);
    t->output_len += n;
    t->output[t->output_len] = '\0';
    pthread_mutex_unlock(&t->mutex);
}

int shell_tab_submit(ShellTab *t, const char *cmd) {
    if (!t || !cmd) return -1;
    pthread_mutex_lock(&t->mutex);
    if (t->command_pending || t->busy) {
        pthread_mutex_unlock(&t->mutex);
        shell_tab_append_output(t, "[command already running; try again shortly]\n");
        return -1;
    }
    snprintf(t->command, sizeof(t->command), "%s", cmd);
    t->command_pending = 1;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->mutex);
    return 0;
}