# MyTerm Design Document

## 1. Architecture

The program has one X11 event loop in `main.c`. `gui_handler` owns rendering and dispatches keyboard/mouse events. Each `ShellTab` owns an input buffer, cursor, transcript, history and a worker thread. The worker waits on a condition variable and executes submitted commands. Output is stored in a mutex-protected transcript and the GUI redraws it regularly.

## 2. Command execution

`cmd_exec.c` tokenizes commands into argument vectors, honoring basic single/double quoted words. It creates N-1 pipes for N commands, forks each pipeline stage, connects descriptors with `dup2`, applies `<`, `>`, and `>>`, and calls `execvp`. `cd`, `history`, and `exit` are built-ins. This parser is intentionally smaller than a POSIX shell parser.

## 3. GUI

Xlib creates the window, accepts keyboard and mouse events, and redraws the transcript and current input. Prompt is green, directory component blue, and user input/output white. Cursor location is calculated using the selected X11 font's text-width metrics.

## 4. Line editor

The line editor maintains `input`, `input_len`, and `cursor`. It supports insertion, Backspace/Delete, Left/Right, Home/End, Ctrl+A, Ctrl+E, history navigation, Tab completion, and Enter submission.

## 5. History

History is kept per tab in memory, capped at 10,000 commands, and appended to a file under the user's home directory. The `history` built-in prints up to the latest 1,000 entries. Up/Down select previous/next entries.

## 6. Autocomplete

Autocomplete scans the current working directory and matches names by the prefix immediately before the cursor. One match completes the name; multiple matches are extended to their longest common prefix, or listed when no longer prefix exists.

## 7. Tabs and concurrency

Tabs are represented by independent `ShellTab` objects with worker threads, command queues, mutexes, and condition variables. The X11 event loop remains centralized. For strict per-tab working-directory isolation, replace process-wide `chdir()` with a stored cwd and call `chdir(tab->cwd)` only in a forked child before exec; the starter version's `cd` behavior is not fully isolated across threads.

## 8. MultiWatch

The `multiWatch` module starts supplied commands concurrently, captures each command's output in a temporary file, then emits command-labelled output with Unix timestamps and removes temporary files. A production-quality implementation should stream outputs as they arrive, terminate all children on Ctrl+C, and handle process groups.

## 9. Known gaps

The code is a project baseline and requires testing and further implementation before claiming full compliance. Unicode shaping/multiline input, robust quote/escape parsing, background jobs, Ctrl+C/Ctrl+Z process-group handling, truly independent tab cwd state, streaming MultiWatch, and interactive stdin are incomplete.
