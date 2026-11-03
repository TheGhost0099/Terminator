CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -O2 -pthread -D_POSIX_C_SOURCE=200809L
LDLIBS := -lX11 -pthread
SRC := main.c gui_handler.c cmd_exec.c shell_tab.c line_editor.c history.c autocomplete.c multiwatch.c jobs_control.c
OBJ := $(SRC:.c=.o)

all: myterm
myterm: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)
clean:
	rm -f $(OBJ) myterm
.PHONY: all clean
