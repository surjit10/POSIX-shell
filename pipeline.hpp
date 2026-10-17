#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <vector>
#include <string>
#include "types.hpp"
using namespace std;
struct stage_t {
    vector<string> argv_tokens;
};

struct pipeline_t {
    vector<stage_t> stages;
};

int single_command_run(const vector<string>& tokens, bool run_bg);
int pipeline_chain_execute(pipeline_t& P, bool run_bg);
pipeline_t pipeline_tokens_split(const string& line);

int builtin_history(const vector<string>& args);

vector<string> split_ws(const string& s);
vector<string> tokenize_command(const string& line);
void add_command_to_history(const string& command);

#endif // PIPELINE_HPP
