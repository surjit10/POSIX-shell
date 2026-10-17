#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <vector>
#include <unistd.h>
using namespace std;
enum class JobState { Running, Stopped, Done };

struct Job {
    pid_t pid;
    string name;
    JobState state;
    bool is_bg;
};

#endif // TYPES_HPP
