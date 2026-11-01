#include "history.h"
#include "shell_tab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void history_init(ShellTab *tab) {
    const char *home = getenv("HOME");
    snprintf(tab->history_file, sizeof(tab->history_file), "%s/.myterm_history_%d", home ? home : ".", tab->id);
    
    FILE *f = fopen(tab->history_file, "r");
    if (!f) return;
    
    char line[INPUT_CAP];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (tab->history_count < 10000 && line[0]) {
            snprintf(tab->history[tab->history_count++], INPUT_CAP, "%s", line);
        }
    }
    fclose(f);
    tab->history_pos = (long)tab->history_count;
}

void history_add(ShellTab *tab, const char *command) {
    if (!command || !*command) return;
    if (tab->history_count && strcmp(tab->history[tab->history_count - 1], command) == 0) {
        tab->history_pos = (long)tab->history_count;
        return;
    }
    if (tab->history_count == 10000) {
        memmove(tab->history, tab->history + 1, sizeof(tab->history[0]) * 9999);
        tab->history_count--;
    }
    snprintf(tab->history[tab->history_count++], INPUT_CAP, "%s", command);
    tab->history_pos = (long)tab->history_count;
    
    FILE *f = fopen(tab->history_file, "a");
    if (f) {
        fprintf(f, "%s\n", command);
        fclose(f);
    }
}

const char *history_previous(ShellTab *tab) {
    if (tab->history_count == 0 || tab->history_pos <= 0) return NULL;
    tab->history_pos--;
    return tab->history[tab->history_pos];
}

const char *history_next(ShellTab *tab) {
    if (tab->history_pos < 0) tab->history_pos = 0;
    if ((size_t)tab->history_pos >= tab->history_count) return "";
    tab->history_pos++;
    if ((size_t)tab->history_pos >= tab->history_count) return "";
    return tab->history[tab->history_pos];
}

void history_show(ShellTab *tab) {
    size_t start = tab->history_count > 1000 ? tab->history_count - 1000 : 0;
    char line[INPUT_CAP + 32];
    for (size_t i = start; i < tab->history_count; i++) {
        snprintf(line, sizeof(line), "%zu  %s\n", i + 1, tab->history[i]);
        shell_tab_append_output(tab, line);
    }
}