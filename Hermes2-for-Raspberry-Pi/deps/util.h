#ifndef __UTIL_H__
#define __UTIL_H__
// a C++ implimentation of the old deps/util.h
#include <cinttypes>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
using std::uint64_t;
using std::string;
using std::vector;
using std::cerr;
using std::cin;
using std::cout;
using std::endl;

// macro for easy debugging
#ifdef DEBUG
#define PERROR(x)          \
    do {                   \
        cerr << x << endl; \
    } while(false)
#else
#define PERROR(x)
#endif

enum COMMAND_TYPES {
    C_NONE                 = 0,
    C_GET_REMOTE_FILE_LIST = 65,
    C_RESPONSE_REMOTE_FILE_LIST,
    C_OPEN_FILE_REQUEST,
    C_RESPONSE_FILE_INFO,
    C_PUSH_LINE_FRONT,
    C_PUSH_LINE_BACK,
    C_UPDATE_LINE_CONTENT,
    C_SET_CURSOR_POS,
    C_SWITCH_TO_BROWSING_MODE,
    C_SWITCH_TO_EDITING_MODE,
    C_INSERT_LINE,
    C_DELETE_LINE,
    C_SAVE_FILE,
    C_ADD_LINE_BACK,
    C_OTHER = 122,
};

enum STATUS_TYPES {
    S_NONE = 0,
    S_DIR_MODE,
    S_WAITING_MODE,
    S_FILE_MODE,
    S_BROWSING_MODE,  // no editing
};

uint64_t get_timestamp();
string base64_encode(const string& data);
string base64_decode(const string& data);
vector<string> get_file_list(const char* const base_directory);

string str_implode(const vector<string>& svec, char seperator = '&');

struct ClientLineEntry {
    ClientLineEntry() = default;
    ClientLineEntry(const string& line, size_t line_num = 0)
        : linenum(line_num), s(line) {}
    operator string&() { return s; }
    size_t linenum;
    string s;
};

struct ServerLineEntry {
    ServerLineEntry() = default;
    ServerLineEntry(const ServerLineEntry& other) : s(other.s) {}
    ServerLineEntry(const string& line) : s(line) {}
    operator string&() { return s; }
    // std::mutex m;
    string s;
};
#endif