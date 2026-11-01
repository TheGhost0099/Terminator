#include "gui_handler.h"
#include "shell_tab.h"
#include <X11/Xlib.h>
#include <time.h>

int main(void){if(gui_init()!=0)return 1;if(shell_tabs_init()!=0){gui_shutdown();return 1;}gui_redraw();int running=1;while(running){Display*d=gui_display();while(XPending(d)){XEvent e;XNextEvent(d,&e);if(!gui_handle_event(&e)){running=0;break;}}struct timespec ts={0,20000000};nanosleep(&ts,NULL);gui_redraw();}shell_tabs_shutdown();gui_shutdown();return 0;}
