#include "autocomplete.h"
#include "shell_tab.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void autocomplete_complete(ShellTab *tab) {
    size_t start = tab->cursor;
    while (start > 0 && tab->input[start - 1] != ' ' && tab->input[start - 1] != '\t' && tab->input[start - 1] != '/') {
        start--;
    }

    char prefix[INPUT_CAP];
    size_t plen = tab->cursor - start;
    if (plen >= sizeof(prefix)) {
        return;
    }

    memcpy(prefix, tab->input + start, plen);
    prefix[plen] = '\0';

    DIR *d = opendir(".");
    if (!d) {
        return;
    }

    char matches[256][256];
    size_t count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) && count < 256) {
        if (strncmp(ent->d_name, prefix, plen) == 0 && strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
            snprintf(matches[count++], sizeof(matches[0]), "%s", ent->d_name);
        }
    }
    closedir(d);

    if (!count) {
        return;
    }

    char replacement[INPUT_CAP];
    if (count == 1) {
        snprintf(replacement, sizeof(replacement), "%s", matches[0]);
    } else {
        size_t common = strlen(matches[0]);
        for (size_t i = 1; i < count; i++) {
            size_t j = 0;
            while (j < common && matches[i][j] && matches[0][j] == matches[i][j]) {
                j++;
            }
            common = j;
        }

        if (common <= plen) {
            char out[4096] = "Matches: ";
            for (size_t i = 0; i < count; i++) {
                strncat(out, matches[i], sizeof(out) - strlen(out) - 2);
                strncat(out, "  ", sizeof(out) - strlen(out) - 1);
            }
            strncat(out, "\n", sizeof(out) - strlen(out) - 1);
            shell_tab_append_output(tab, out);
            return;
        }
        snprintf(replacement, sizeof(replacement), "%.*s", (int)common, matches[0]);
    }

    size_t rlen = strlen(replacement);
    size_t tail = tab->input_len - tab->cursor;
    if (start + rlen + tail >= INPUT_CAP) {
        return;
    }

    memmove(tab->input + start + rlen, tab->input + tab->cursor, tail + 1);
    memcpy(tab->input + start, replacement, rlen);
    tab->input_len = start + rlen + tail;
    tab->cursor = start + rlen;
}