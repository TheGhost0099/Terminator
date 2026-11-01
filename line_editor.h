#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H
#include <X11/Xlib.h>
struct ShellTab;
int line_editor_handle_key(struct ShellTab *tab, XKeyEvent *event);
#endif
