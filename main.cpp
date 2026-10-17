#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <csignal>
#include<algorithm>

// Custom headers
#include "types.hpp"
#include "pipeline.hpp"
#include "shell_signal.hpp"

// System headers
extern "C" {
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <pwd.h>
  #include <dirent.h>
  #include <signal.h>
  #include <termios.h>
  #include <errno.h>
}

using namespace std;

//forward declarotions of globals from main.cpp
string current_working_dir();
void update_cwd_cache();
void history_file_prepare();
string find_current_username();
string find_computer_name();
void prompt_show();
bool read_command_line(string& out);
string remove_white_space(const string& s);
vector<string> history_load_memory();
string shorten_path(const string& complete_path);

int command_segment_handle(const string& rawCommand);

void disable_raw_mode();
void add_command_to_history(const string &command);
int  input_capture_line(char *buffer);
void jobs_status_refresh();
void terminal_rawmode_enable();




// globals

static const size_t HISTORY_MAX_STORE = 20;
static const size_t HISTORY_DEFAULT_PRINT = 10;
vector<Job> g_job_list;

string shell_home;          // shell start dir
string global_prev_dir;     // for 'cd -'
string global_cwd;          // cwd cache
string global_history_file; // history file path
pid_t  g_fg_pid = -1;       // current foreground child pid (if any)
bool   g_running = true;    // main loop control

// Builtin implementations

string current_working_dir() {
    char path_buffer[4096];
    string resolved_path{};


    if (getcwd(path_buffer, sizeof(path_buffer)) != nullptr) {
        return string(path_buffer);
    }
    if (!global_cwd.empty()) {
        resolved_path = global_cwd;
    } else {
        resolved_path = ".";
    }

    return resolved_path;
}


void update_cwd_cache() {
    global_cwd = current_working_dir();
}
//preparing histroy file
void history_file_prepare() {
    string base_home_dir{};
    const char* env_home_ptr = getenv("HOME");
    if (env_home_ptr) {
        base_home_dir = string(env_home_ptr);
    } else {
        base_home_dir = shell_home;
    }

    const string history_filename{".myshell_history"};
    global_history_file = base_home_dir + "/" + history_filename;


    int fd_hist = open(global_history_file.c_str(), O_CREAT | O_APPEND, 0644);
    if (fd_hist >= 0) {
        close(fd_hist);
    }
}

//finds current user
string find_current_username() {
    uid_t current_userID = getuid();
    struct passwd* user_details = getpwuid(current_userID);

    if (user_details != nullptr) return string(user_details->pw_name);
    return "unknown";
}
//finds the computer name
string find_computer_name() {
    const int store = 256;
    char computer_name_buffer[store];
    int resultCode = gethostname(computer_name_buffer, store);

    if (resultCode == 0) return string(computer_name_buffer);
    return "unknown";
}
//minimize the path if it is home directory
string shorten_path(const string& complete_path) {
    if (!shell_home.empty() &&
        complete_path.length() >= shell_home.length() &&
        complete_path.substr(0, shell_home.length()) == shell_home) {

        if (complete_path.length() == shell_home.length()) {
            return "~";
        } else {
            string path_remainder = complete_path.substr(shell_home.length());
            return "~" + path_remainder;
        }
    }
    return complete_path;
}
// print the shell console prompt
void prompt_show() {
    string user = find_current_username();
    string host = find_computer_name();
    update_cwd_cache();
    string path = shorten_path(global_cwd);
    cout << user << "@" << host << ":" << path << "> ";
    cout.flush();
}
// Read a line of input from the user
bool read_command_line(string& out) {
    if (!getline(cin, out)) return false;
    return true;
}

string remove_white_space(const string& input_str) {
    if (input_str.empty()) {
        return string();
    }
    string processed_str = input_str;
    size_t search_pos{};
    while ((search_pos = processed_str.find("&nbsp;", search_pos)) != string::npos) {
        processed_str.replace(search_pos, 6, " ");
        search_pos += 1;
    }
    size_t start_idx{};
    while (start_idx < processed_str.size() &&
           (processed_str[start_idx] == ' ' || processed_str[start_idx] == '\t')) {
        ++start_idx;
    }
    if (start_idx == processed_str.size()) {
        return string();
    }
    size_t end_idx = processed_str.size() - 1;
    while (end_idx > start_idx &&
           (processed_str[end_idx] == ' ' || processed_str[end_idx] == '\t')) {
        --end_idx;
    }

    return processed_str.substr(start_idx, end_idx - start_idx + 1);
}


//histroy
vector<string> history_load_memory() {
    auto loaded_history = vector<string>();

    if (global_history_file.empty()) {
        return loaded_history;
    }

    ifstream hist_file_stream(global_history_file);
    string raw_line;

    while (getline(hist_file_stream, raw_line)) {
        string cleaned_line = remove_white_space(raw_line);
        if (!cleaned_line.empty()) {
            loaded_history.push_back(cleaned_line);
            if (loaded_history.size() >= HISTORY_MAX_STORE) {
                break;
            }
        }
    }

    return loaded_history;
}

// Command Dispatch and Execution

struct CommandInfo {
    string commandString;
    bool isBackgroundTask = false;
};

CommandInfo prepareCommand(const string& rawUserInput) {
    CommandInfo info;
    info.commandString = remove_white_space(rawUserInput);

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;
    size_t background_pos = string::npos;

    for (size_t i = 0; i < info.commandString.size(); ++i) {
        const char c = info.commandString[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && !in_single_quote) {
            escaped = true;
        } else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '&' && !in_single_quote && !in_double_quote) {
            background_pos = i;
        }
    }

    if (!info.commandString.empty() && background_pos == info.commandString.size() - 1) {
        info.isBackgroundTask = true;
        info.commandString.erase(background_pos);
        info.commandString = remove_white_space(info.commandString);
    }
    return info;
}

#define BG_JOB_STARTED 1001

int command_segment_handle(const string& rawCommand) {
    CommandInfo commandDetails = prepareCommand(rawCommand);

    if (commandDetails.commandString.empty()) return 0;

    pipeline_t pipeline = pipeline_tokens_split(commandDetails.commandString);
    if (pipeline.stages.size() > 1) {
        return pipeline_chain_execute(pipeline, commandDetails.isBackgroundTask);
    }

    vector<string> tokens = tokenize_command(commandDetails.commandString);
    return single_command_run(tokens, commandDetails.isBackgroundTask);
}

static void flush_segment(vector<string> &tokens, string &segment) {
    if (!segment.empty()) {
        tokens.push_back(remove_white_space(segment));
        segment.clear();
    }
}

static vector<string> split_commands_and_ops(const string &input_line) {
    vector<string> parsed_tokens;
    string current_segment{};
    size_t pos_idx{};
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    while (pos_idx < input_line.size()) {
        const char current_char = input_line[pos_idx];

        if (escaped) {
            current_segment.push_back(current_char);
            escaped = false;
            ++pos_idx;
            continue;
        }
        if (current_char == '\\' && !in_single_quote) {
            current_segment.push_back(current_char);
            escaped = true;
            ++pos_idx;
            continue;
        }
        if (current_char == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            current_segment.push_back(current_char);
            ++pos_idx;
            continue;
        }
        if (current_char == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            current_segment.push_back(current_char);
            ++pos_idx;
            continue;
        }

        //check for 2 character command separators
        if (!in_single_quote && !in_double_quote && pos_idx + 1 < input_line.size()) {
            string op_pair = input_line.substr(pos_idx, 2);
            if (op_pair == "&&" || op_pair == "||") {
                flush_segment(parsed_tokens, current_segment);
                parsed_tokens.push_back(op_pair);
                pos_idx += 2;
                continue;
            }
        }

        // Check for single-character command separator
        if (!in_single_quote && !in_double_quote && current_char == ';') {
            flush_segment(parsed_tokens, current_segment);
            parsed_tokens.push_back(";");
        } else {
            current_segment.push_back(current_char);
        }
        ++pos_idx;
    }
    flush_segment(parsed_tokens, current_segment);
    return parsed_tokens;
}

//decision state for next command execution
static bool should_execute_next(const string &operator_token, int prev_status) {
    if (operator_token == ";") {
        return true;
    }
    if (operator_token == "&&" && prev_status == 0) {
        return true;
    }
    if (operator_token == "||" && prev_status != 0) {
        return true;
    }
    return false;
}

// Dispatch and execute commands in sequence according to logical operators
int commands_flow_dispatch(const string &input_line) {
    auto parsed_tokens = split_commands_and_ops(input_line);
    if (parsed_tokens.empty()) {
        return 0;
    }

    int current_status{};
    string active_operator{";"};

    for (size_t idx = 0; idx < parsed_tokens.size(); ++idx) {
        const auto &current_token = parsed_tokens[idx];
        if (current_token == "&&" || current_token == "||" || current_token == ";") {
            active_operator = current_token;
            continue;
        }
        if (!current_token.empty() && should_execute_next(active_operator, current_status)) {
            current_status = command_segment_handle(current_token);

            if (current_status == BG_JOB_STARTED) {
                return BG_JOB_STARTED;
            }
        }
    }

    return current_status;
}


// terminal handling raw mode
struct termios saved_termios;

void disable_raw_mode() {
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

void enable_terminal_raw_mode() {
    if (!isatty(STDIN_FILENO)) {
        return;
    }

    if (tcgetattr(STDIN_FILENO, &saved_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }

    atexit(disable_raw_mode);

    struct termios raw_mode = saved_termios;
    raw_mode.c_lflag &= ~(ECHO | ICANON);
    raw_mode.c_cc[VMIN] = 1;
    raw_mode.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1) {
        perror("tcsetattr");
        exit(1);
    }
}

//complete autocomplete handling an


vector<string> g_command_history;
size_t g_history_index = 0;

const vector<string> builtin_commands = {
    "cd", "pwd", "echo", "ls", "pinfo", "search",
    "history", "exit", "quit"
};

enum class CompletionType {
    COMMAND,
    FILESYSTEM
};

// escape spaces and special characters so the string can be used in the shell without breaking
static string escape_for_shell(const string &name) {
    string escaped;
    for (char c : name) {
        if (isspace(static_cast<unsigned char>(c)) ||
            strchr("*?()[]{}$&;|<>\\'\"`", c)) {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

//Return a list of filesystem entries that match the typed prefix

vector<string> filesystem_candidates_find(const string& prefix) {
    vector<string> matches;
    string path = ".";
    string partial_name = prefix;

    size_t last_slash = prefix.find_last_of('/');
    if (last_slash != string::npos) {
        path = prefix.substr(0, last_slash);
        if (path.empty()) path = "/"; // root dir case
        partial_name = prefix.substr(last_slash + 1);
    }

    // Expand ~ to home
    if (path == "~") {
        const char* home = getenv("HOME");
        if (home) path = home;
    } else if (path.rfind("~/", 0) == 0) {
        const char* home = getenv("HOME");
        if (home) path = string(home) + path.substr(1);
    }

    DIR *dp = opendir(path.c_str());
    if (!dp) return matches;

    struct dirent *entry;
    while ((entry = readdir(dp)) != nullptr) {
        string entry_name = entry->d_name;
        if (partial_name.empty() && (entry_name == "." || entry_name == "..")) {
            continue;
        }

        if (entry_name[0] == '.' && (partial_name.empty() || partial_name[0] != '.')) {
            continue;
        }

        if (entry_name.rfind(partial_name, 0) == 0) {
    string full_path = path + (path.back() == '/' ? "" : "/") + entry_name;
    struct stat st{};
    if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        // return relative-to-input
        matches.push_back(prefix.substr(0, last_slash + 1) + escape_for_shell(entry_name) + "/");
    } else {
        matches.push_back(prefix.substr(0, last_slash + 1) + escape_for_shell(entry_name));
    }
}

    }
    closedir(dp);
    return matches;
}

// Command candidates builtins and  path  executables
vector<string> command_candidates_find(const string &prefix) {
    set<string> unique_matches;

    // Builtins
    for (const auto &cmd : builtin_commands) {
        if (cmd.rfind(prefix, 0) == 0) {
            unique_matches.insert(cmd);
        }
    }

    // PATH executables
    char *path_env = getenv("PATH");
    if (path_env) {
        stringstream ss(path_env);
        string dir;
        while (getline(ss, dir, ':')) {
            if (dir.empty()) continue;
            DIR *dp = opendir(dir.c_str());
            if (!dp) continue;

            struct dirent *entry;
            while ((entry = readdir(dp)) != nullptr) {
                if (strncmp(entry->d_name, prefix.c_str(), prefix.size()) == 0) {
                    string full_path = dir + "/" + entry->d_name;
                    struct stat st{};
                    if (stat(full_path.c_str(), &st) == 0 &&
                        !S_ISDIR(st.st_mode) && (st.st_mode & S_IXUSR)) {
                        unique_matches.insert(entry->d_name);
                    }
                }
            }
            closedir(dp);
        }
    }
    return vector<string>(unique_matches.begin(), unique_matches.end());
}

//longest commann prefix
string longest_common_prefix(const vector<string>& matches) {
    if (matches.empty()) return "";
    string prefix = matches[0];
    for (size_t i = 1; i < matches.size(); i++) {
        size_t j = 0;
        while (j < prefix.size() && j < matches[i].size() &&
               prefix[j] == matches[i][j]) {
            j++;
        }
        prefix = prefix.substr(0, j);
        if (prefix.empty()) break;
    }
    return prefix;
}

// autocomplete result
struct AutoCompleteResult {
    string completed_text;
    vector<string> suggestions;
};

//main auto complte handler function
AutoCompleteResult autocomplete_suggestions_get(const string &input_prefix,
                                                CompletionType match_type) {
    vector<string> matches;

    if (match_type == CompletionType::COMMAND) {
        auto cmds = command_candidates_find(input_prefix);
        auto files = filesystem_candidates_find(input_prefix);
        cmds.insert(cmds.end(), files.begin(), files.end());
        matches = move(cmds);
    } else if (match_type == CompletionType::FILESYSTEM) {
        matches = filesystem_candidates_find(input_prefix);
    }

    AutoCompleteResult result;
    result.suggestions = matches;


    string path_prefix;
    size_t last_slash = input_prefix.find_last_of('/');
    if (last_slash != string::npos) {
        path_prefix = input_prefix.substr(0, last_slash + 1);
    }

    if (matches.empty()) {
        result.completed_text = input_prefix;
    } else if (matches.size() == 1) {
        result.completed_text = path_prefix + matches[0];
    } else {
        string lcp = longest_common_prefix(matches);
        if (lcp.size() > (input_prefix.size() - path_prefix.size())) {
            result.completed_text = path_prefix + lcp;
        } else {
            result.completed_text = input_prefix;
        }
    }
    return result;
}





// refreshes the prmpt,input line,and cursor position on the termunal.
#define CMD_MAX 4096

static void draw_prompt_with_line(const string &input_line, int cursor_index) {

    write(STDOUT_FILENO, "\33[2K\r", 5);


    string username = find_current_username();
    string hostname = find_computer_name();
    update_cwd_cache();
    string cwd_short = shorten_path(global_cwd);
    string prompt_str = username + "@" + hostname + ":" + cwd_short + "> ";

    write(STDOUT_FILENO, prompt_str.c_str(), prompt_str.size());
    write(STDOUT_FILENO, input_line.c_str(), input_line.size());

    int chars_to_move_left = static_cast<int>(input_line.size()) - cursor_index;
    if (chars_to_move_left > 0) {
        string move_cmd = "\33[" + to_string(chars_to_move_left) + "D";
        write(STDOUT_FILENO, move_cmd.c_str(), move_cmd.size());
    }
}

//histry navigation
static void navigate_history(int direction, string &current_line, int &cursor_index) {
    if (direction < 0 && g_history_index > 0) { // Up
        g_history_index--;
        current_line = g_command_history[g_history_index];
    }
    else if (direction > 0 && g_history_index < g_command_history.size()) { // Down
        g_history_index++;
        if (g_history_index == g_command_history.size()) {
            current_line.clear();
        } else {
            current_line = g_command_history[g_history_index];
        }
    }
    cursor_index = static_cast<int>(current_line.length());
    draw_prompt_with_line(current_line, cursor_index);
}

//handling tab for auto complete
static void handle_autocomplete(string &current_line, int &cursor_index) {
    string before_cursor = current_line.substr(0, cursor_index);
    size_t last_space_pos = before_cursor.find_last_of(" \t\n\v\f\r");

    string word_fragment;
    CompletionType comp_type;

    if (last_space_pos == string::npos) {
        comp_type = CompletionType::COMMAND;
        word_fragment = before_cursor;
    } else {
        comp_type = CompletionType::FILESYSTEM;
        word_fragment = before_cursor.substr(last_space_pos + 1);
    }

    auto suggestions = autocomplete_suggestions_get(word_fragment, comp_type);

   if (suggestions.suggestions.size() == 1) {
    size_t replace_start = (last_space_pos == string::npos) ? 0 : last_space_pos + 1;
    current_line.replace(replace_start, word_fragment.length(), suggestions.suggestions[0]);
    cursor_index = static_cast<int>(replace_start + suggestions.suggestions[0].length());
    draw_prompt_with_line(current_line, cursor_index);
}
else if (!suggestions.suggestions.empty()) {
    write(STDOUT_FILENO, "\n", 1);
    for (const auto &suggestion : suggestions.suggestions) {
        write(STDOUT_FILENO, suggestion.c_str(), suggestion.size());
        write(STDOUT_FILENO, "\t", 1);
    }
    write(STDOUT_FILENO, "\n", 1);
    draw_prompt_with_line(current_line, cursor_index);
}

}


static void handle_arrow_keys(char seq_first, char seq_second, string &current_line, int &cursor_index) {
    if (seq_first != '[') return;

    if (seq_second == 'A') {
        navigate_history(-1, current_line, cursor_index); // Up
    }
    else if (seq_second == 'B') {
        navigate_history(1, current_line, cursor_index); // Down
    }
    else if (seq_second == 'C' && cursor_index < static_cast<int>(current_line.size())) { // Right
        cursor_index++;
        write(STDOUT_FILENO, "\x1b[C", 3);
    }
    else if (seq_second == 'D' && cursor_index > 0) { // Left
        cursor_index--;
        write(STDOUT_FILENO, "\x1b[D", 3);
    }
}

static void handle_backspace(string &current_line, int &cursor_index) {
    if (cursor_index > 0) {
        current_line.erase(cursor_index - 1, 1);
        cursor_index--;
        draw_prompt_with_line(current_line, cursor_index);
    }
}

//main input capture function
int input_capture_line(char *output_buffer) {
    if (!isatty(STDIN_FILENO)) {
        if (!cin.getline(output_buffer, CMD_MAX)) {
            return -1;
        }
        return 0;
    }

    int cursor_index{};
    char input_char{};
    string current_line;

    g_history_index = g_command_history.size();

    while (true) {
        int bytes_read = read(STDIN_FILENO, &input_char, 1);

        if (bytes_read < 0) {
            if (errno == EINTR) { output_buffer[0] = '\0'; return 0; }
            return -1;
        }
        if (bytes_read == 0) {
            if (current_line.empty()) return -1;
            break;
        }

        if (input_char == '\4') { // Ctrl+D
            if (current_line.empty()) return -1;
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        else if (input_char == '\n') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        else if (input_char == 127 || input_char == '\b') {
            handle_backspace(current_line, cursor_index);
        }
        else if (input_char == 27) { // Escape sequence
            char seq[2] = {0, 0};
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            handle_arrow_keys(seq[0], seq[1], current_line, cursor_index);
        }
        else if (input_char == '\t') {
            handle_autocomplete(current_line, cursor_index);
        }
        else if (isprint(static_cast<unsigned char>(input_char))) {
            if (static_cast<size_t>(cursor_index) < CMD_MAX - 1) {
                current_line.insert(cursor_index, 1, input_char);
                cursor_index++;
                draw_prompt_with_line(current_line, cursor_index);
            }
        }
    }

    strncpy(output_buffer, current_line.c_str(), CMD_MAX - 1);
    output_buffer[CMD_MAX - 1] = '\0';
    return 0;
}


// history management
static bool job_is_done(const Job &job) {
    return job.state == JobState::Done;
}

void jobs_status_refresh() {
    int child_status{};
    pid_t child_pid{};

    while ((child_pid = waitpid(-1, &child_status,
                                WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (auto &job : g_job_list) {
            if (job.pid != child_pid) continue;
            if (WIFSTOPPED(child_status)) {
                job.state = JobState::Stopped;
            } else if (WIFCONTINUED(child_status)) {
                job.state = JobState::Running;
            } else {
                job.state = JobState::Done;
            }
            break;
        }
    }

    for (auto it = g_job_list.begin(); it != g_job_list.end();) {
        if (job_is_done(*it)) {
            it = g_job_list.erase(it);
        } else {
            ++it;
        }
    }
}



volatile sig_atomic_t jobs_changed_flag = 0;

int main() {
    // setting up teh signals
    signal_wiring();
    update_cwd_cache();
    shell_home = global_cwd;
    global_prev_dir = shell_home;

    history_file_prepare();
    g_command_history = history_load_memory();

    enable_terminal_raw_mode();

    bool suppress_prompt_once = false;

    while (g_running) {

        if (jobs_changed_flag) {
            jobs_changed_flag = 0;
           jobs_status_refresh();
        }

        if (isatty(STDIN_FILENO) && !suppress_prompt_once) {
            prompt_show();
        } else {
            suppress_prompt_once = false;
        }

        char cmd_buffer[CMD_MAX] = {0};
        int input_status = input_capture_line(cmd_buffer);

        if (input_status == -1) {
            write(STDOUT_FILENO, "exit\n", 5);
            break;
        }
        string trimmed_line = remove_white_space(cmd_buffer);
        if (trimmed_line.empty()) {
            if (jobs_changed_flag) {
                jobs_changed_flag = 0;
                jobs_status_refresh();
                suppress_prompt_once = true;
            }
            continue;
        }

        add_command_to_history(trimmed_line);
        (void)commands_flow_dispatch(trimmed_line);

    }

    disable_raw_mode();
    return 0;
}
