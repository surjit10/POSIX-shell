#include "shell_signal.hpp"
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
extern volatile sig_atomic_t jobs_changed_flag;

using namespace std;

// Handle SIGCHLD: update job states when child processes change status
void handle_sigchld(int) {
    jobs_changed_flag = 1;
}


// Handle SIGINT: forward interrupt to foreground process
void handle_sigint(int) {
    if (g_fg_pid != -1 && g_fg_pid != 0) {
        kill(g_fg_pid, SIGINT);
    }
}

// Handle SIGTSTP: forward stop signal to foreground process
void handle_sigtstp(int) {
    if (g_fg_pid != -1 && g_fg_pid != 0) {
        kill(g_fg_pid, SIGTSTP);
    }
}

// Configure signal handlers

void signal_wiring() {
    struct sigaction sigchld_action{};
    sigchld_action.sa_handler = handle_sigchld;
    sigemptyset(&sigchld_action.sa_mask);
    // Restart interrupted syscalls (like read) on SIGCHLD
    sigchld_action.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sigchld_action, nullptr);

    // Keep default simple forwarding for these (you typically want ^C to break reads)
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);
}
