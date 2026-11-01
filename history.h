#ifndef HISTORY_H
#define HISTORY_H
struct ShellTab;
void history_init(struct ShellTab *tab);
void history_add(struct ShellTab *tab, const char *command);
const char *history_previous(struct ShellTab *tab);
const char *history_next(struct ShellTab *tab);
void history_show(struct ShellTab *tab);
#endif
