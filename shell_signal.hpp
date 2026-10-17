#ifndef SIGNAL_SHELL_HPP
#define SIGNAL_SHELL_HPP

#include <csignal>
#include <sys/wait.h>
#include <vector>
#include <cerrno>
#include <unistd.h>
#include <iostream>
#include <string>
#include "types.hpp"
using namespace std;

extern vector<Job> g_job_list;
extern pid_t g_fg_pid;

void signal_wiring();
void handle_sigchld(int);
void handle_sigint(int);
void handle_sigtstp(int);
extern volatile sig_atomic_t jobs_changed_flag;

#endif // SIGNAL_SHELL_HPP
