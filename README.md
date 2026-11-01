# MyTerm — X11 Custom Terminal

A modular C/Xlib terminal project for the IIT Kharagpur Computing Lab assignment.

## Build and run

Requirements on Ubuntu/WSL: GCC, make, X11 development headers, an X server.

```bash
sudo apt update
sudo apt install build-essential libx11-dev
make
./myterm
```

If you already have X11 working, run the commands from your Linux environment. The project uses the same RGB background values as the initial prototype. The prompt is green, the current directory is blue, and input/output is white.

## Files

- `main.c`: app entry point/event loop
- `gui_handler.c/.h`: X11 window and rendering
- `cmd_exec.c/.h`: command parsing and execution
- `shell_tab.c/.h`: per-tab state and worker threads
- `line_editor.c/.h`: editing, arrows, Enter, Ctrl+A/E, Ctrl+D
- `history.c/.h`: persistent history, Up/Down and `history`
- `autocomplete.c/.h`: filename completion
- `multiwatch.c/.h`: basic multi-command execution

## Current implementation status / limitations

This is a working starter implementation, not a fully bash-compatible shell. It supports simple external commands, quoted words, pipelines, `<`, `>`, `>>`, `cd`, `exit`, basic history, line editing, tabs, and filename completion. The project is deliberately split into modules to make extension easier.

Before submission, test each required feature in your environment. In particular, interactive programs, multiline Unicode editing, Ctrl+C/Ctrl+Z process-group/job-control semantics, precise multiWatch interleaving and cancellation, nested quoting, shell expansion, and tab-close cleanup need additional work. X11 core-font text drawing is not a complete Unicode shaping engine. `cd` is handled inside the tab worker's execution context; because tabs share a process, `chdir()` is process-wide and must be replaced with per-tab cwd-aware child execution before relying on different directories in different tabs.

## Controls

- Enter: submit command
- Tab: filename completion
- Up/Down: history navigation
- Left/Right, Home/End: move cursor
- Ctrl+A / Ctrl+E: start/end of line
- Ctrl+W or `+` area: create tab
- F1/F2/F3: switch among first three tabs
- Ctrl+D on empty input: close current tab / exit when last tab closes

## Design document

See `DESIGNDOC.md` for architecture and feature notes.
