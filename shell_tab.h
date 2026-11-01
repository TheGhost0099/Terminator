#ifndef SHELL_TAB_H
#define SHELL_TAB_H
#include <pthread.h>
#include <stddef.h>
#define TAB_MAX 8
#define INPUT_CAP 4096
#define OUTPUT_CAP 65536
#define TITLE_CAP 64

typedef struct ShellTab {
    int id;
    int alive;
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int worker_started;
    int command_pending;
    int busy;
    char command[INPUT_CAP];
    char cwd[4096];
    char output[OUTPUT_CAP];
    size_t output_len;
    char input[INPUT_CAP];
    size_t input_len;
    size_t cursor;
    char title[TITLE_CAP];
    char history[10000][INPUT_CAP];
    size_t history_count;
    long history_pos;
    char history_file[4096];
} ShellTab;

int shell_tabs_init(void);
int shell_tabs_create(void);
void shell_tabs_close(int index);
void shell_tabs_shutdown(void);
int shell_tabs_count(void);
int shell_tabs_active(void);
int shell_tabs_switch(int index);
ShellTab *shell_tab_get(int index);
void shell_tab_append_output(ShellTab *tab, const char *text);
int shell_tab_submit(ShellTab *tab, const char *command);
#endif
