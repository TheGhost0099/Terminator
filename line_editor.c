#include "line_editor.h"
#include "shell_tab.h"
#include "history.h"
#include "autocomplete.h"
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

static void insert_char(ShellTab *t, char c) {
    if (t->input_len + 1 >= INPUT_CAP) return;
    memmove(t->input + t->cursor + 1, t->input + t->cursor, t->input_len - t->cursor + 1);
    t->input[t->cursor++] = c;
    t->input_len++;
}

int line_editor_handle_key(ShellTab *t, XKeyEvent *event) {
    KeySym k;
    char buf[64];
    int n = XLookupString(event, buf, sizeof(buf), &k, NULL);

    if ((event->state & ControlMask) && (k == XK_c || k == XK_C)) {
        /* Ctrl+C: interrupt the running command (if any) with SIGINT,
           then show ^C and clear the half-typed line, like bash does. */
        shell_tab_send_signal(t, SIGINT);
        shell_tab_append_output(t, "^C\n");
        t->input_len = t->cursor = 0;
        t->input[0] = '\0';
        return 1;
    }
    if ((event->state & ControlMask) && (k == XK_a || k == XK_A)) {
        t->cursor = 0;
        return 1;
    }
    if ((event->state & ControlMask) && (k == XK_e || k == XK_E)) {
        t->cursor = t->input_len;
        return 1;
    }
    if ((event->state & ControlMask) && (k == XK_d || k == XK_D)) {
        if (!t->input_len) {
            shell_tab_append_output(t, "exit\n");
            return 2;
        }
        if (t->cursor < t->input_len) {
            memmove(t->input + t->cursor, t->input + t->cursor + 1, t->input_len - t->cursor);
            t->input_len--;
        }
        return 1;
    }

    switch (k) {
        case XK_Left:
            if (t->cursor) t->cursor--;
            return 1;
        case XK_Right:
            if (t->cursor < t->input_len) t->cursor++;
            return 1;
        case XK_Home:
            t->cursor = 0;
            return 1;
        case XK_End:
            t->cursor = t->input_len;
            return 1;
        case XK_BackSpace:
            if (t->cursor) {
                memmove(t->input + t->cursor - 1, t->input + t->cursor, t->input_len - t->cursor + 1);
                t->cursor--;
                t->input_len--;
            }
            return 1;
        case XK_Delete:
            if (t->cursor < t->input_len) {
                memmove(t->input + t->cursor, t->input + t->cursor + 1, t->input_len - t->cursor);
                t->input_len--;
            }
            return 1;
        case XK_Up: {
            const char *s = history_previous(t);
            if (s) {
                snprintf(t->input, INPUT_CAP, "%s", s);
                t->input_len = strlen(t->input);
                t->cursor = t->input_len;
            }
            return 1;
        }
        case XK_Down: {
            const char *s = history_next(t);
            if (s) {
                snprintf(t->input, INPUT_CAP, "%s", s);
                t->input_len = strlen(t->input);
                t->cursor = t->input_len;
            }
            return 1;
        }
        case XK_Tab:
            autocomplete_complete(t);
            return 1;
        case XK_Return:
        case XK_KP_Enter: {
            char command[INPUT_CAP];
            snprintf(command, sizeof(command), "%s", t->input);
            char shown[INPUT_CAP + 128];
            snprintf(shown, sizeof(shown), "ayushman@terminator:$ %s\n", command);
            shell_tab_append_output(t, shown);
            if (*command) {
                history_add(t, command);
                shell_tab_submit(t, command);
            }
            t->input_len = t->cursor = 0;
            t->input[0] = '\0';
            return 1;
        }
        default:
            break;
    }

    if (n > 0 && !(event->state & (ControlMask | Mod1Mask))) {
        for (int i = 0; i < n; i++) {
            if ((unsigned char)buf[i] >= 32) {
                insert_char(t, buf[i]);
            }
        }
    }
    return 1;
}