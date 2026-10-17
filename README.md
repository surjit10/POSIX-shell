# POSIX Shell in C++

> Systems Programming Project · C++17 · Linux/POSIX APIs

A compact Unix-style command shell built from first principles in C++. The project demonstrates practical operating-systems knowledge through process creation, pipelines, file-descriptor redirection, signal handling, background execution, persistent history, and raw-terminal input editing.

## Résumé snapshot

- Engineered a modular command shell using `fork`, `execvp`, `pipe`, `dup2`, `waitpid`, `sigaction`, and `termios`.
- Implemented multi-stage pipelines, foreground/background execution, `<`, `>`, and `>>` redirection, and `&&`, `||`, and `;` command chaining.
- Built quote- and escape-aware tokenization so spaces and shell operators can be passed as literal arguments.
- Added interactive history navigation, tab completion across built-ins, `$PATH`, and filesystem entries, plus persistent command history.
- Developed built-ins for navigation, file inspection, process information, recursive search, and history management.
- Added an automated smoke-test suite covering parsing, pipelines, conditional execution, redirection, and built-ins.

## Features

### Command execution

- External program lookup through `$PATH`
- Arbitrary-length pipelines such as `cat file | grep text | wc -l`
- Conditional and sequential execution with `&&`, `||`, and `;`
- Background execution with a trailing `&`
- Input, overwrite, and append redirection with `<`, `>`, and `>>`
- Single quotes, double quotes, and backslash-escaped characters

### Built-in commands

| Command | Purpose |
| --- | --- |
| `cd [path]` | Change directory; supports `~` and `-` |
| `pwd` | Print the current directory |
| `echo [args...]` | Print arguments |
| `ls [-a] [-l] [path...]` | List files with optional hidden/long formats |
| `pinfo [pid]` | Read process state, memory, and executable data from `/proc` |
| `search <name>` | Recursively search below the current directory |
| `history [count]` | Show recent persisted commands |
| `exit`, `quit` | Leave the shell |

### Interactive experience

- `user@host:path>` prompt with home-directory shortening
- Left/right cursor movement and backspace editing
- Up/down history navigation
- Tab completion for commands and paths
- `Ctrl+C`, `Ctrl+Z`, and `Ctrl+D` handling

## Build and run

Requirements: Linux or another POSIX-like environment, GNU Make, and a C++17 compiler.

```bash
make
./myshell
```

Clean generated files with:

```bash
make clean
```

## Verification

Run the automated smoke tests:

```bash
make test
```

The test suite exercises quoted and escaped arguments, compact redirection syntax, pipelines, logical operators, external commands, and built-ins. The project also compiles with `-Wall -Wextra -Wpedantic` enabled.

## Example session

```console
$ ./myshell
user@host:~> echo "systems programming" | tr a-z A-Z
SYSTEMS PROGRAMMING
user@host:~> sleep 20 &
[1] 12345
user@host:~> false || echo recovered
recovered
user@host:~> echo first>notes.txt; echo second>>notes.txt
user@host:~> cat notes.txt
first
second
```

## Architecture

```text
main.cpp            interactive loop, prompt, history, completion, command chaining
pipeline.cpp        tokenizer, built-ins, redirection, process and pipeline execution
shell_signal.cpp    SIGCHLD handling and foreground signal forwarding
types.hpp           shared job data structures
pipeline.hpp        command and pipeline interfaces
shell_signal.hpp    signal interfaces
tests/              end-to-end smoke tests
```

Execution follows a small, readable flow:

```text
input → command-chain parser → quote-aware tokenizer → built-in or fork/exec
                                      └──────────────→ pipe/dup2 redirection
```

## Scope

This is an educational Unix-style shell rather than a drop-in replacement for Bash. It intentionally focuses on process control, file descriptors, signals, and terminal programming; advanced shell-language features such as variable expansion, globbing, command substitution, and scripting syntax are outside its scope.

## Author

**Surjit** — systems programming and C++ project portfolio
