#ifndef CMD_EXEC_H
#define CMD_EXEC_H
struct ShellTab;
void cmd_exec_worker(struct ShellTab *tab);
void cmd_exec_run(struct ShellTab *tab, const char *command);
#endif
