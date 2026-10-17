
#include "pipeline.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string>
#include <fstream>
#include <pwd.h>
#include <grp.h>
#include <iomanip>
#include <array>
#include <vector>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>

using namespace std;

// frward declaration of globals from main.cpp
pipeline_t pipeline_parse_tokens(const string& line);
int pipeline_exec_flow(pipeline_t& P, bool run_bg);
int single_cmd_exec(const vector<string>& tokens, bool run_bg);

// Globals from main.cpp
extern bool g_running;
extern pid_t g_fg_pid;
extern vector<Job> g_job_list;
extern string global_cwd;
extern string global_prev_dir;
extern string shell_home;
extern string global_history_file;
extern vector<string> g_command_history;

//function prototypes for builtins
#include <string>
#include <vector>
using std::string;

using std::vector;
//splitting string into tokens based on white space
vector<string> split_ws(const string &input_str) {

    string mod_str = input_str;
    size_t search_idx{};
    while (true) {
        size_t found_at = mod_str.find("&nbsp;", search_idx);
        if (found_at == string::npos) {
            break;
        }
        mod_str.replace(found_at, 6, " ");
        search_idx = found_at + 1;
    }
    auto tokens = vector<string>();
    const string ws_chars = " \t\n\r\f\v";

    size_t seg_start = mod_str.find_first_not_of(ws_chars);
    while (seg_start != string::npos) {
        size_t seg_end = mod_str.find_first_of(ws_chars, seg_start);
        if (seg_end == string::npos) {

            tokens.push_back(mod_str.substr(seg_start));
            seg_start = string::npos;
        } else {
            tokens.push_back(mod_str.substr(seg_start, seg_end - seg_start));
            seg_start = mod_str.find_first_not_of(ws_chars, seg_end);
        }
    }
  return tokens;
}



// removing front and bcak spaces/tabs/newlines
static string trim_str(const string& input_str) {
    size_t start_pos = input_str.find_first_not_of(" \t\n");
    if (start_pos == string::npos) {
        return string(); // empty if only whitespace
    }
    size_t end_pos = input_str.find_last_not_of(" \t\n");
    auto length = end_pos - start_pos + 1;
    return input_str.substr(start_pos, length);
}

static string resolve_path(const string& raw_path) {
    if (raw_path.empty()) {
        return shell_home;
    }

    if (raw_path == "~") {
        return shell_home;
    }

    if (raw_path == "-") {
        return global_prev_dir;
    }

    char first_char = raw_path.front();
    if (first_char == '~') {
        string remaining_path = raw_path.substr(1);
        return shell_home + remaining_path;
    }
    return raw_path;
}

// Compatibility wrappers
pipeline_t pipeline_tokens_split(const string& line) {
    return pipeline_parse_tokens(line);
}

int pipeline_chain_execute(pipeline_t& P, bool run_bg) {
    return pipeline_exec_flow(P, run_bg);
}

int single_command_run(const vector<string>& tokens, bool run_bg) {
    return single_cmd_exec(tokens, run_bg);
}


// braing the individual pipeline segments into individual segments
static vector<string> extract_pipeline_segments(const string &pipeline_line) {
    vector<string> stage_list;
    string current_stage;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (char c : pipeline_line) {
        if (escaped) {
            current_stage.push_back(c);
            escaped = false;
        } else if (c == '\\' && !in_single_quote) {
            current_stage.push_back(c);
            escaped = true;
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            current_stage.push_back(c);
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            current_stage.push_back(c);
        } else if (c == '|' && !in_single_quote && !in_double_quote) {
            string cleaned_stage = trim_str(current_stage);
            if (!cleaned_stage.empty()) stage_list.push_back(cleaned_stage);
            current_stage.clear();
        } else {
            current_stage.push_back(c);
        }
    }

    string cleaned_stage = trim_str(current_stage);
    if (!cleaned_stage.empty()) stage_list.push_back(cleaned_stage);
    return stage_list;
}

// Creates a stage_t object from a single command segment
static stage_t make_stage_from_segment(const string &cmd_segment) {
    stage_t stage_obj{};
    stage_obj.argv_tokens = tokenize_command(cmd_segment);
    return stage_obj;
}

// Parses the pipeline string into a pipeline_t structure
pipeline_t pipeline_parse_tokens(const string &pipeline_line) {
    pipeline_t parsed_pipeline{};
    auto stage_strings = extract_pipeline_segments(pipeline_line);
    for (size_t idx = 0; idx < stage_strings.size(); ++idx) {
        parsed_pipeline.stages.push_back(make_stage_from_segment(stage_strings[idx]));
    }
    return parsed_pipeline;
}


// command builtins impleentations
bool is_builtin(const string& command_name) {
    static const unordered_set<string> builtin_cmds{
        "cd", "pwd", "echo", "ls", "pinfo", "search", "history", "exit", "quit"
    };
    return builtin_cmds.find(command_name) != builtin_cmds.end();
}

int builtin_cd(const vector<string>& params);
int builtin_pwd(const vector<string>& params);
int builtin_echo(const vector<string>& params);
int builtin_ls(const vector<string>& params);
int builtin_pinfo(const vector<string>& params);
int builtin_search(const vector<string>& params);
int builtin_history(const vector<string>& params);

int run_builtin(const string& command_name, const vector<string>& params) {
    using builtin_fn = int(*)(const vector<string>&);
    static const unordered_map<string, builtin_fn> dispatch_table{
        {"cd",      builtin_cd},
        {"pwd",     builtin_pwd},
        {"echo",    builtin_echo},
        {"ls",      builtin_ls},
        {"pinfo",   builtin_pinfo},
        {"search",  builtin_search},
        {"history", builtin_history}
    };
    if (command_name == "exit" || command_name == "quit") {
        g_running = false;
        return 0;
    }

    auto it = dispatch_table.find(command_name);
    if (it != dispatch_table.end()) {
        return (it->second)(params);
    }
    return -1;
}


//chnaging directory
int builtin_cd(const vector<string>& arguments) {

    if (arguments.size() > 2) {
        cerr << "cd: too many arguments" << endl;
        return -1;
    }
    bool is_prev_dir_call = (arguments.size() == 2 && arguments[1] == "-");

    string target_dir{};
    if (arguments.size() == 1) {
        target_dir = shell_home;
    } else {
        target_dir = resolve_path(arguments[1]);
    }
    char prev_dir_buf[3000];
    if (!::getcwd(prev_dir_buf, sizeof(prev_dir_buf))) {
        perror("getcwd");
        return -1;
    }
    if (::chdir(target_dir.c_str()) != 0) {
        perror("cd");
        return -1;
    }
    global_prev_dir = string(prev_dir_buf);

    char curr_dir_buf[3000];
    if (::getcwd(curr_dir_buf, sizeof(curr_dir_buf))) {
        global_cwd = string(curr_dir_buf);
    }
    if (is_prev_dir_call) {
        cout << global_cwd << endl;
    }

    return 0;
}

// previuos working dir
int builtin_pwd(const vector<string>& cmd_tokens) {

    auto extra_count = cmd_tokens.size();
    if (extra_count > 1) {
        const string& extra_arg = cmd_tokens[1];
        bool looks_like_option = (!extra_arg.empty() && extra_arg.front() == '-');

        if (looks_like_option) {
            cerr << "pwd: invalid option -- '" << extra_arg << "'\n";
            cerr << "Try 'pwd --help' for more information.\n";
        } else {
            cerr << "pwd: too many arguments\n";
        }
        return 1;
    }


    cout << global_cwd << '\n';
    return 0;
}


// non-breaking spaces (&nbsp;) into normal spaces
string decode_html_spaces(const string &input) {
    string result;
    for (size_t i = 0; i < input.size();) {
        if (input.compare(i, 6, "&nbsp;") == 0) {
            result.push_back(' ');
            i += 6;
        } else {
            result.push_back(input[i]);
            ++i;
        }
    }
    return result;
}

// Tokenizer: collapses multiple spaces, preserves quoted strings
vector<string> tokenize_command(const string &line) {
    vector<string> tokens;
    string token;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool token_started = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (c == '\\' && !in_single_quote) {
            token_started = true;
            if (i + 1 < line.size()) {
                token.push_back(line[++i]);
            } else {
                token.push_back(c);
            }
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            token_started = true;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            token_started = true;
        } else if (!in_single_quote && !in_double_quote &&
                   (c == '<' || c == '>')) {
            if (token_started) {
                tokens.push_back(token);
                token.clear();
                token_started = false;
            }
            if (c == '>' && i + 1 < line.size() && line[i + 1] == '>') {
                tokens.push_back(">>");
                ++i;
            } else {
                tokens.emplace_back(1, c);
            }
        } else if (!in_single_quote && !in_double_quote &&
                   isspace(static_cast<unsigned char>(c))) {
            if (token_started) {
                tokens.push_back(token);
                token.clear();
                token_started = false;
            }
        } else {
            token.push_back(c);
            token_started = true;
        }
    }
    if (token_started) {
        tokens.push_back(token);
    }
    return tokens;
}

// Builtin echo: prints args with single space between them
int builtin_echo(const vector<string>& parameters) {
    for (size_t i = 1; i < parameters.size(); ++i) {
        if (i > 1) cout << " ";
        cout << parameters[i];
    }
    cout << "\n";
    return 0;
}



// Reads the 'ls' command options, turns on the right flags, and figures out which folders/files to list
static bool parse_ls_args(const vector<string>& input_args,
                          vector<string>& collected_paths,
                          bool& flag_show_all,
                          bool& flag_long_format) {
    size_t arg_index = 1;
    while (arg_index < input_args.size()) {
        const string& current_arg = input_args[arg_index];

        if (!current_arg.empty() && current_arg[0] == '-') {
            if (current_arg == "--") { // end of options
                for (size_t extra_idx = arg_index + 1; extra_idx < input_args.size(); ++extra_idx) {
                    collected_paths.push_back(input_args[extra_idx]);
                }
                break;
            }
            //processif every flag
            for (size_t char_pos = 1; char_pos < current_arg.size(); ++char_pos) {
                char opt_flag = current_arg[char_pos];
                if (opt_flag == 'a') {
                    flag_show_all = true;
                } else if (opt_flag == 'l') {
                    flag_long_format = true;
                } else {
                    cerr << "ls: invalid option -- '" << opt_flag << "'\n";
                    cerr << "Try 'ls --help' for more information.\n";
                    return false;
                }
            }
        } else {
            collected_paths.push_back(current_arg);
        }
        arg_index++;
    }

    if (collected_paths.empty()) {
        collected_paths.push_back(".");
    }
    return true;
}

//printing one file at a time in lon format
static void print_long_format(const string& file_path, const char* display_name) {
    struct stat file_stat{};
    if (::stat(file_path.c_str(), &file_stat) != 0) {
        cerr << "ls: cannot access '" << display_name << "': " << strerror(errno) << "\n";
        return;
    }

    char perms[11] = "----------";
    if (S_ISDIR(file_stat.st_mode)) perms[0] = 'd';
    if (file_stat.st_mode & S_IRUSR) perms[1] = 'r';
    if (file_stat.st_mode & S_IWUSR) perms[2] = 'w';
    if (file_stat.st_mode & S_IXUSR) perms[3] = 'x';
    if (file_stat.st_mode & S_IRGRP) perms[4] = 'r';
    if (file_stat.st_mode & S_IWGRP) perms[5] = 'w';
    if (file_stat.st_mode & S_IXGRP) perms[6] = 'x';
    if (file_stat.st_mode & S_IROTH) perms[7] = 'r';
    if (file_stat.st_mode & S_IWOTH) perms[8] = 'w';
    if (file_stat.st_mode & S_IXOTH) perms[9] = 'x';

    struct passwd* user_info = getpwuid(file_stat.st_uid);
    struct group* group_info = getgrgid(file_stat.st_gid);

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%b %d %H:%M",
                  localtime(&file_stat.st_mtime));

    cout << perms << " " << setw(2) << file_stat.st_nlink << " "
              << setw(8) << (user_info ? user_info->pw_name : to_string(file_stat.st_uid)) << " "
              << setw(8) << (group_info ? group_info->gr_name : to_string(file_stat.st_gid)) << " "
              << setw(8) << file_stat.st_size << " " << time_str << " "
              << display_name << "\n";
}

// printing file in short or pass to another function for long print
static void print_file(const string& absolute_path, bool use_long_format) {
    string base_name = absolute_path.substr(absolute_path.find_last_of('/') + 1);
    if (use_long_format) {
        print_long_format(absolute_path, base_name.c_str());
    } else {
        cout << base_name << "\n";
    }
}

// listing content of single dir
static void list_directory(const string& dir_path,
                           bool include_hidden,
                           bool use_long_format,
                           bool header_required) {
    DIR* dir_handle = ::opendir(dir_path.c_str());
    if (!dir_handle) {
        cerr << "ls: cannot open directory '" << dir_path << "': " << strerror(errno) << "\n";
        return;
    }

    if (header_required) {
        cout << dir_path << ":\n";
    }

    vector<string> dir_entries;
    struct dirent* entry_ptr{};
    while ((entry_ptr = ::readdir(dir_handle)) != nullptr) {
        if (!include_hidden && entry_ptr->d_name[0] == '.') {
            continue;
        }
        dir_entries.push_back(entry_ptr->d_name);
    }
    ::closedir(dir_handle);

    sort(dir_entries.begin(), dir_entries.end());

    for (size_t idx = 0; idx < dir_entries.size(); ++idx) {
        if (use_long_format) {
            string full_path = dir_path + "/" + dir_entries[idx];
            print_long_format(full_path, dir_entries[idx].c_str());
        } else {
            cout << dir_entries[idx] << "\n";
        }
    }
}

// main ls caller function
int builtin_ls(const vector<string>& raw_args) {
    vector<string> resolved_paths;
    bool show_all_flag{false};
    bool long_format_flag{false};

    if (!parse_ls_args(raw_args, resolved_paths, show_all_flag, long_format_flag)) {
        return 1;
    }
    vector<string> file_list;
    vector<string> dir_list;

    size_t path_idx = 0;
    while (path_idx < resolved_paths.size()) {
        string abs_path = resolve_path(resolved_paths[path_idx]);
        struct stat path_stat{};
        if (::stat(abs_path.c_str(), &path_stat) != 0) {
            cerr << "ls: cannot access '" << resolved_paths[path_idx] << "': " << strerror(errno) << "\n";
            path_idx++;
            continue;
        }
        if (S_ISDIR(path_stat.st_mode)) {
            dir_list.push_back(abs_path);
        } else {
            file_list.push_back(abs_path);
        }
        path_idx++;
    }

    for (size_t f_idx = 0; f_idx < file_list.size(); ++f_idx) {
        print_file(file_list[f_idx], long_format_flag);
    }

    for (size_t d_idx = 0; d_idx < dir_list.size(); ++d_idx) {
        bool header_needed = (file_list.size() + dir_list.size() > 1);
        list_directory(dir_list[d_idx], show_all_flag, long_format_flag, header_needed);
        if (d_idx < dir_list.size() - 1) {
            cout << "\n";
        }
    }

    return 0;
}

// Figures out which process id  to use — either from the user’s input or the current process
static pid_t resolve_pid_from_args(const vector<string>& arguments) {
    if (arguments.size() < 2) {
        return ::getpid();
    }

    pid_t resolved_pid{-1};
    try {
        int parsed_value = stoi(arguments[1]);
        resolved_pid = static_cast<pid_t>(parsed_value);
    }
    catch (const invalid_argument&) {
        cerr << "pinfo: Invalid PID argument '" << arguments[1]
                  << "'. Not a number." << endl;
    }
    catch (const out_of_range&) {
        cerr << "pinfo: PID argument '" << arguments[1]
                  << "' is out of range." << endl;
    }

    return resolved_pid;
}

//func to display process info
static int display_process_info(pid_t process_id) {

    string proc_status_file = "/proc/" + to_string(process_id) + "/status";
    ifstream status_stream(proc_status_file);
    if (!status_stream) {
        cerr << "pinfo: process with PID " << process_id << " not found." << endl;
        return -1;
    }

    // Print PID first
    cout << "pid: " << process_id << "\n";

    string line_buffer;
    while (getline(status_stream, line_buffer)) {
        if (line_buffer.rfind("State:", 0) == 0 || line_buffer.rfind("VmSize:", 0) == 0) {
            cout << line_buffer << "\n";
        }
    }
    status_stream.close();

    // preparing to read executable path
    string exe_symlink = "/proc/" + to_string(process_id) + "/exe";
    char exe_path_buf[4096];
    ssize_t exe_path_len = ::readlink(exe_symlink.c_str(), exe_path_buf, sizeof(exe_path_buf) - 1);

    if (exe_path_len != -1) {
        exe_path_buf[exe_path_len] = '\0';
        cout << "Executable Path: " << string(exe_path_buf) << "\n";
    } else {
        cout << "Executable Path: (Permission Denied or Path not found)\n";
    }

    return 0;
}

//main pinfo caller func
int builtin_pinfo(const vector<string>& arguments) {
    pid_t proc_to_inspect = resolve_pid_from_args(arguments);
    if (proc_to_inspect == static_cast<pid_t>(-1)) {
        return -1;
    }
    return display_process_info(proc_to_inspect);
}





// func to serch recursively file/dir by name
static void search_recursive(const string& base_dir,
                              const string& search_name,
                              bool& found_flag) {
    //exit if found
    if (found_flag) {
        return;
    }

    DIR* directory_ptr = ::opendir(base_dir.c_str());
    if (!directory_ptr) {
        return;
    }

    struct dirent* entry_ptr{};
    while (!found_flag && (entry_ptr = ::readdir(directory_ptr)) != nullptr) {
        const char* current_name = entry_ptr->d_name;


        if ((strcmp(current_name, ".") == 0) ||
            (strcmp(current_name, "..") == 0)) {
            continue;
        }


        if (strcmp(current_name, search_name.c_str()) == 0) {
            found_flag = true;
            continue;
        }


        if (entry_ptr->d_type == DT_DIR) {
            string next_path = base_dir + "/" + current_name;
            search_recursive(next_path, search_name, found_flag);
        }
    }

    ::closedir(directory_ptr);
}


//search func
int builtin_search(const vector<string>& cmd_args) {
    if (cmd_args.size() != 2) {
        cerr << "Usage: search <filename>\n";
        return -1;
    }

    bool match_found{false};
    search_recursive(".", cmd_args[1], match_found);

    cout << (match_found ? "True\n" : "False\n");
    return 0;
}

//saving history record to histroy file
void write_history_to_file(const vector<string>& history_entries) {
    ofstream history_out(global_history_file, ios::trunc);
    size_t pos{};
    while (pos < history_entries.size()) {
        history_out << history_entries[pos] << "\n";
        pos = pos + 1;
    }
}

void add_command_to_history(const string& command_input) {
    if (command_input.empty()) {
        return;
    }

    string trimmed_cmd = trim_str(command_input);


    if (trimmed_cmd.rfind("history", 0) == 0) {
        return;
    }
    if (!g_command_history.empty() && g_command_history.back() == command_input) {
        return;
    }

    g_command_history.push_back(command_input);

    if (g_command_history.size() > 20) {
        g_command_history.erase(g_command_history.begin());
    }
    write_history_to_file(g_command_history);
}

//displaying last 10 entries
int builtin_history(const vector<string>& arguments) {
    size_t num_to_display{10};

    if (arguments.size() > 2) {
        cerr << "history: too many arguments\n";
        return 1;
    }
    if (arguments.size() == 2) {
        try {
            int parsed_value = stoi(arguments[1]);
            if (parsed_value < 0) {
                cerr << "history: invalid number\n";
                return 1;
            }
            num_to_display = static_cast<size_t>(parsed_value);
        } catch (...) {
            cerr << "history: invalid number\n";
            return 1;
        }
    }

    size_t total_count = g_command_history.size();
    size_t start_index = (total_count > num_to_display) ? (total_count - num_to_display) : 0;
    size_t current_index = start_index;
    while (current_index < total_count) {
        cout << g_command_history[current_index] << endl;
        current_index = current_index + 1;
    }

    return 0;
}



// Command execution
string resolve_full_path(const string& cmd) {
    if (cmd.find('/') != string::npos) return cmd;
    const char* path_env = getenv("PATH");
    if (!path_env) return cmd;

    stringstream ss(path_env);
    string dir;
    while (getline(ss, dir, ':')) {
        string full = dir + "/" + cmd;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return cmd;
}

#define BG_JOB_STARTED 1001
// parsing multiple arg and builing args
static int parse_redirs_and_build_args(
    const vector<string>& token_list,
    vector<string>& filtered_args,
    int& input_fd, int& output_fd)
{
    input_fd = -1;
    output_fd = -1;

    size_t token_index{};
    while (token_index < token_list.size()) {
        const string& current_token = token_list[token_index];

        if (current_token == "<" || current_token == ">" || current_token == ">>") {
            token_index++;
            if (token_index >= token_list.size()) {
                cerr << "Syntax error: no file specified for redirection\n";
                return -1;
            }

            const string& target_file = token_list[token_index];

            if (current_token == "<") {
                if (input_fd != -1) close(input_fd);
                input_fd = open(target_file.c_str(), O_RDONLY);
                if (input_fd < 0) { perror("open input"); return -1; }
            } else {
                if (output_fd != -1) close(output_fd);
                int open_flags = O_WRONLY | O_CREAT | (current_token == ">>" ? O_APPEND : O_TRUNC);
                output_fd = open(target_file.c_str(), open_flags, 0644);
                if (output_fd < 0) { perror("open output"); return -1; }
            }
        } else {
            filtered_args.push_back(current_token);
        }
        token_index++;
    }
    return 0;
}

//executing builtin command with redirection
static int run_builtin_with_redirs(
    const string& builtin_name,
    const vector<string>& arg_list,
    int input_fd, int output_fd)
{
    if (input_fd != -1) dup2(input_fd, STDIN_FILENO);
    if (output_fd != -1) dup2(output_fd, STDOUT_FILENO);

    int status_code = run_builtin(builtin_name, arg_list);
    cout.flush();
    return status_code;
}

// Runs a non‑built‑in command, handling pipes, redirection, and background jobs
static int launch_external_command(
    vector<string>& arg_list,
    bool run_in_background, int input_fd, int output_fd, const string& cmd_label)
{
    arg_list[0] = resolve_full_path(arg_list[0]);

    vector<char*> argv_vector;
    for (size_t idx = 0; idx < arg_list.size(); ++idx) {
        argv_vector.push_back(const_cast<char*>(arg_list[idx].c_str()));
    }
    argv_vector.push_back(nullptr);

    pid_t spawned_pid = fork();
    if (spawned_pid < 0) {
        perror("fork");
        return -1;
    }

    if (spawned_pid == 0) { // Child
        if (run_in_background) setpgid(0, 0);
        if (input_fd != -1) { dup2(input_fd, STDIN_FILENO); close(input_fd); }
        if (output_fd != -1) { dup2(output_fd, STDOUT_FILENO); close(output_fd); }
        execvp(argv_vector[0], argv_vector.data());
        perror(argv_vector[0]);
        _exit(127);
    }

    // Parent
    if (run_in_background) {
        static int job_id_counter = 1;
        cout << "[" << job_id_counter++ << "] " << spawned_pid << "\n";
        cout.flush();
        g_job_list.emplace_back(Job{spawned_pid, cmd_label, JobState::Running, true});
        return BG_JOB_STARTED;
    }

    g_fg_pid = spawned_pid;
    int wait_status{};
    waitpid(spawned_pid, &wait_status, WUNTRACED);

    if (WIFSTOPPED(wait_status)) {
        cerr << "\n[Stopped] " << cmd_label << " (" << spawned_pid << ")" << endl;
        g_job_list.push_back({spawned_pid, cmd_label, JobState::Stopped, true});
    } else if (WIFSIGNALED(wait_status)) {
        cerr << "\n[Killed] " << cmd_label << " (" << spawned_pid << ")" << endl;
    }

    int exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1;
    g_fg_pid = -1;

    if (isatty(STDOUT_FILENO)) {
        char last_char{};
        if (lseek(STDOUT_FILENO, -1, SEEK_END) != -1 &&
            read(STDOUT_FILENO, &last_char, 1) == 1 &&
            last_char != '\n') {
            write(STDOUT_FILENO, "\n", 1);
        }
    }
    return exit_code;
}

// main commaand exe func
int single_cmd_exec(const vector<string>& token_list, bool run_in_background) {
    if (token_list.empty()) return 0;

    int stdin_copy = dup(STDIN_FILENO);
    int stdout_copy = dup(STDOUT_FILENO);
    int input_fd{-1}, output_fd{-1};
    int exec_result{};

    vector<string> filtered_args;
    exec_result = parse_redirs_and_build_args(token_list, filtered_args, input_fd, output_fd);

    if (exec_result == 0 && !filtered_args.empty()) {
        string cmd_name = filtered_args[0];
        if (is_builtin(cmd_name)) {
            exec_result = run_builtin_with_redirs(cmd_name, filtered_args, input_fd, output_fd);
        } else {
            exec_result = launch_external_command(filtered_args, run_in_background, input_fd, output_fd, cmd_name);
        }
    }

    if (input_fd != -1) close(input_fd);
    if (output_fd != -1) close(output_fd);
    dup2(stdin_copy, STDIN_FILENO);
    dup2(stdout_copy, STDOUT_FILENO);
    close(stdin_copy);
    close(stdout_copy);

    return exec_result;
}



// removing redirection token from args
static vector<string> strip_redirection_tokens(const vector<string>& token_list) {
    auto result_tokens = vector<string>();
    size_t index{};
    while (index < token_list.size()) {
        const auto& current_token = token_list[index];
        if (current_token == "<" || current_token == ">" || current_token == ">>") {
            index++;
        } else {
            result_tokens.push_back(current_token);
        }
        index++;
    }
    return result_tokens;
}

// applying redirection
static bool apply_redirections(const vector<string>& token_list) {
    size_t index{};
    while (index < token_list.size()) {
        const auto& current_token = token_list[index];
        if (current_token == "<") {
            if (index + 1 >= token_list.size()) {
                cerr << "Syntax error: no file specified for redirection\n";
                return false;
            }
            const string& infile = token_list[++index];
            int fd_in = open(infile.c_str(), O_RDONLY);
            if (fd_in < 0) { perror(("open: " + infile).c_str()); _exit(1); }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        else if (current_token == ">" || current_token == ">>") {
            if (index + 1 >= token_list.size()) {
                cerr << "Syntax error: no file specified for redirection\n";
                return false;
            }
            const string& outfile = token_list[++index];
            int open_flags = O_WRONLY | O_CREAT | ((current_token == ">>") ? O_APPEND : O_TRUNC);
            int fd_out = open(outfile.c_str(), open_flags, 0644);
            if (fd_out < 0) { perror(("open: " + outfile).c_str()); _exit(1); }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
        index++;
    }
    return true;
}

// pipe creation
static bool create_pipes(vector<array<int, 2>>& pipes, int count) {
    pipes.resize(count);
    for (int i = 0; i < count; ++i) {
        if (pipe(pipes[i].data()) < 0) {
            perror("pipe");
            return false;
        }
    }
    return true;
}

//connecting stages of pipes
static void connect_stage_pipes(int stage_index, int last_stage_index, const vector<array<int, 2>>& pipes) {
    if (stage_index > 0) {
        dup2(pipes[stage_index - 1][0], STDIN_FILENO);
    }
    if (stage_index < last_stage_index) {
        dup2(pipes[stage_index][1], STDOUT_FILENO);
    }
}

static void close_all_pipes(const vector<array<int, 2>>& pipes) {
    for (size_t i = 0; i < pipes.size(); ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static void exec_command(const vector<string>& argv_clean) {
    if (argv_clean.empty()) _exit(0);

    if (is_builtin(argv_clean[0])) {
        int status = run_builtin(argv_clean[0], argv_clean);
        cout.flush();
        cerr.flush();
        _exit(status < 0 ? 1 : status);
    }

    vector<char*> argv_ptrs;
    for (size_t i = 0; i < argv_clean.size(); ++i) {
        argv_ptrs.push_back(const_cast<char*>(argv_clean[i].c_str()));
    }
    argv_ptrs.push_back(nullptr);

    execvp(argv_ptrs[0], argv_ptrs.data());
    perror(argv_ptrs[0]);
    _exit(127);
}

//main pipleine exe handler
int pipeline_exec_flow(pipeline_t& pipeline, bool run_in_background) {
    if (pipeline.stages.empty()) return 0;
    if (pipeline.stages.size() == 1) {
        return single_cmd_exec(pipeline.stages[0].argv_tokens, run_in_background);
    }

    int total_stages = static_cast<int>(pipeline.stages.size());
    vector<pid_t> stage_pids(total_stages, -1);
    vector<array<int, 2>> pipes;
    pid_t process_group = -1;

    if (!create_pipes(pipes, total_stages - 1)) return -1;

    for (int stage = 0; stage < total_stages; ++stage) {
        auto argv_clean = strip_redirection_tokens(pipeline.stages[stage].argv_tokens);

        stage_pids[stage] = fork();
        if (stage_pids[stage] < 0) { perror("fork"); return -1; }

        if (stage_pids[stage] == 0) { // Child
            if (run_in_background) {
                pid_t target_group = (stage == 0) ? 0 : stage_pids[0];
                setpgid(0, target_group);
            }
            connect_stage_pipes(stage, total_stages - 1, pipes);
            if (!apply_redirections(pipeline.stages[stage].argv_tokens)) _exit(1);
            close_all_pipes(pipes);
            exec_command(argv_clean);
        }

        if (run_in_background) {
            if (stage == 0) process_group = stage_pids[stage];
            setpgid(stage_pids[stage], process_group);
        }
    }

    close_all_pipes(pipes);

    int final_status{};
    if (run_in_background) {
        static int pipeline_job_id = 1;
        cout << "[" << pipeline_job_id++ << "] " << process_group << "\n";
        for (pid_t pid : stage_pids) {
            g_job_list.push_back({pid, pipeline.stages[0].argv_tokens[0], JobState::Running, true});
        }
        return BG_JOB_STARTED;
    } else {
        g_fg_pid = stage_pids.back();
        for (size_t i = 0; i < stage_pids.size(); ++i) {
            waitpid(stage_pids[i], &final_status, 0);
        }
        g_fg_pid = -1;
    }

    return WIFEXITED(final_status) ? WEXITSTATUS(final_status) : -1;
}

