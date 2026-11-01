#ifndef GUI_HANDLER_H
#define GUI_HANDLER_H
#include <X11/Xlib.h>
struct ShellTab;
int gui_init(void);
void gui_shutdown(void);
Display *gui_display(void);
Window gui_window(void);
int gui_handle_event(XEvent *event);
void gui_redraw(void);
int gui_tab_at(int x, int y);
#endif
