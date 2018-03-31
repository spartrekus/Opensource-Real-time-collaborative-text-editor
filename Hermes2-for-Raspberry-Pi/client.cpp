// Xiaoyan Wang
// A new text editor written from starch
// Based on C++ && ncurses
#include <ncurses.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "client.h"
#include "editor.h"
#include "socket.h"
#include "util.h"
#include "window.h"

using std::vector;
using std::to_string;
using std::list;
using std::string;
using std::lock;
// using std::mutex;
// using std::thread;
// using std::getchar;

int max_row, max_col;
volatile std::sig_atomic_t running = 0;
vector<string> file_list;
list<ClientLineEntry> file_contents;
std::mutex file_list_mutex;
std::mutex status_mutex;
std::condition_variable status_cv;
Socket server;  // contains socket, ip, port number, etc.
Editor editor;  // for windows info, etc.

int main() {
    // --------------- init --------------------
    initscr();             // setup ncurses screen
    raw();                 // enable raw mode so we can capture ctrl+c, etc.
    keypad(stdscr, true);  // to capture arrow key, etc.
    noecho();              // so that escape characters won't be printed
    getmaxyx(stdscr, max_row, max_col);  // get max windows size
    start_color();                       // enable coloring
    init_colors();                       // add color configurations
    std::signal(SIGSEGV, segfault_handler);

    // -------------- done init ----------------

    // ---------- establish connection ---------
    print_welcome_screen();  // let user enter ip and port
    // ---------- connection established -------

    // ---------- run editor ------------------

    // use multithread to handle message
    std::thread handler_thread(message_handler);
    run_editor();

    // ---------- exit editor ----------------
    handler_thread.join();
    endwin();  // end curse mode and restore screen

    return 0;
}


void init_colors() {
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_WHITE, COLOR_RED);
}


void message_handler() {
    // TODO
    ssize_t status = 1;
    string message;  // message received
    int command;     // command received

    // getting file list before starting editor
    server.send("init", C_GET_REMOTE_FILE_LIST);
    if(server.receive(message, command) < 0) {
        mvprintw(0, max_col / 2 - 10, "receive() fails.");
        return;
    }
    size_t num_message = std::stoul(message);
    file_list.reserve(num_message);
    for(size_t i = 0; i < num_message; ++i) {
        server.receive(message, command);
        file_list.push_back(message);
    }

    running = 1;

    while(running && status) {
        status = server.receive(message, command);
        if(status <= 0) {
            PERROR("receive() fails.");
            running = 0;
            break;
        }

        switch(command) {
            case C_RESPONSE_FILE_INFO: {
                num_message = std::stoul(message);
                server.receive(message, command);
                editor.file.set_num_file_lines(std::stoul(message));
                for(int i = 0; i < num_message; ++i) {
                    server.receive(message, command);
                    file_contents.emplace_back(std::move(message), i);
                }
                running = S_FILE_MODE;
                break;
            }
            case C_PUSH_LINE_BACK: {
                file_contents.pop_front();
                file_contents.emplace_back(std::move(message),
                                           file_contents.back().linenum + 1);
                running = S_FILE_MODE;
                break;
            }
            case C_ADD_LINE_BACK: {
                file_contents.emplace_back(std::move(message),
                                           file_contents.back().linenum + 1);
                running = S_FILE_MODE;
                break;
            }
            case C_PUSH_LINE_FRONT: {
                file_contents.pop_back();
                file_contents.emplace_front(std::move(message),
                                            file_contents.front().linenum - 1);
                running = S_FILE_MODE;
                break;
            }
            case C_UPDATE_LINE_CONTENT: {
                size_t line_to_insert = std::stoul(message);
                server.receive(message, command);
                int row = 0;
                for(auto& line : file_contents) {
                    if(line.linenum == line_to_insert) {
                        line.s = std::move(message);
                        editor.file.refresh_file_content(line.s, row);
                        break;
                    }
                    ++row;
                }
                break;
            }
            case C_INSERT_LINE: {
                size_t line_to_insert = std::stoul(message);
                server.receive(message, command);
                editor.file.insert_line(message, line_to_insert);
                break;
            }
            case C_DELETE_LINE: {
                int r, c;
                getyx(static_cast<WINDOW*>(editor.file), r, c);
                if(editor.file.delete_line(std::stoul(message)) == 1) {
                    server.send(to_string(file_contents.back().linenum + 1),
                                C_ADD_LINE_BACK);
                    server.receive(message, command);
                    file_contents.emplace_back(
                        std::move(message), file_contents.back().linenum + 1);
                    editor.file.refresh_file_content(-1);
                    editor.file.set_pos(r - 1, c);
                }
                break;
            }
            case C_SAVE_FILE: {
                running = S_FILE_MODE;  // wake up editor from waiting mode
            }
        }
    }
}

void run_editor() {
    int y, x;
    getyx(stdscr, y, x);
    mvprintw(y + 1, max_col / 2 - 11, "Retrieving file list...");
    getyx(stdscr, y, x);
    while(!running)  // wait until file list is ready
        ;

    // init editor
    editor.init(max_row, max_col);
    erase();
    refresh();
    editor.status.print_filename("~");  // denotes that we haven't select file
    editor.status.print_status(
        "Press Enter to select a file. Press Ctrl+Q to quit.");
    editor.dir.print_filelist(file_list);
    int c;
    while(running) {
        while(running == S_DIR_MODE) {  // directory mode
            c = wgetch(editor.dir);
            switch(c) {
                case KEY_UP:
                    editor.dir.scroll_up(file_list);
                    break;
                case KEY_DOWN:
                    editor.dir.scroll_down(file_list);
                    break;
                case KEY_CTRL_Q:
                    endwin();
                    running = 0;
                    std::exit(0);
                case KEY_ENTER:
                case '\n':
                    editor.status.print_filename(
                        file_list[editor.dir.get_selection()]);
                    editor.switch_mode();
                    running = S_WAITING_MODE;
                    break;
            }
        }
        editor.status.print_status("Retrieving file contents...");
        // send file name and number of rows
        server.send(editor.status.get_filename(), C_OPEN_FILE_REQUEST);
        server.send(to_string(max_row - 2));
        while(running == S_WAITING_MODE)  // waiting for file content
            ;
        editor.status.print_status(
            "Welcome to Hermes. Press Ctrl+O to switch to editing mode.");
        editor.file.set_file_content(&file_contents);
        editor.file.refresh_file_content(-1);
        // int y = 0;
        // for(const string& line : file_contents)
        //     editor.file.printline(line, y++);

        while(running == S_FILE_MODE) {  // file mode
            c = wgetch(editor.file);
            if(std::isprint(c)) {
                if(!editor.file.isediting)
                    continue;
                editor.file.insertchar(c);
                // server.send(to_string(editor.file.get_row()),
                //             C_UPDATE_LINE_CONTENT);
                server.send(editor.file.get_currline(), C_UPDATE_LINE_CONTENT);

                // skip the following switch statement
                continue;
            }
            switch(c) {
                case KEY_CTRL_Q:
                    endwin();
                    running = 0;
                    std::exit(0);
                case KEY_UP: {
                    if(editor.file.scroll_up() == -1) {
                        // retrieve previous line from server
                        server.send(
                            to_string(file_contents.front().linenum - 1),
                            C_PUSH_LINE_FRONT);
                        running = S_WAITING_MODE;
                        while(running == S_WAITING_MODE)
                            ;
                        editor.file.refresh_file_content(-1);
                    }
                    server.send(to_string(editor.file.get_row()),
                                C_SET_CURSOR_POS);
                    break;
                }
                case KEY_DOWN: {
                    if(editor.file.scroll_down() == -1) {
                        // retrieve the line after from server
                        server.send(to_string(file_contents.back().linenum + 1),
                                    C_PUSH_LINE_BACK);
                        running = S_WAITING_MODE;
                        while(running == S_WAITING_MODE)
                            ;
                        editor.file.refresh_file_content(-1);
                    }
                    server.send(to_string(editor.file.get_row()),
                                C_SET_CURSOR_POS);
                    break;
                }
                case KEY_LEFT:
                    editor.file.scroll_left();
                    break;
                case KEY_RIGHT:
                    editor.file.scroll_right();
                    break;
                case KEY_CTRL_S:
                case KEY_SAVE: {
                    int curry, currx;
                    getyx(static_cast<WINDOW*>(editor.file), curry, currx);
                    // ask the server to dump what is in memory
                    // into a file
                    // No need to be in editing mode
                    server.send("", C_SAVE_FILE);
                    editor.status.print_status("Saving file on server...");
                    // since the thread for current client won't be
                    // ready to receive new message until the server
                    // is done saving file, we'll wait
                    running = S_WAITING_MODE;
                    while(running == S_WAITING_MODE)
                        ;
                    if(editor.file.isediting)
                        editor.status.print_status(
                            "Press Ctrl+O to switch to browsing mode. Press "
                            "Ctrl+Q to quit.");
                    else
                        editor.status.print_status(
                            "Press Ctrl+O to switch to editing mode. Press "
                            "Ctrl+Q to quit.");
                    wmove(editor.file, curry, currx);
                    break;
                }
                case '\n':
                case KEY_ENTER: {
                    if(!editor.file.isediting)
                        break;
                    // insert a new line
                    editor.file.add_line();
                    // first update the content of the original line
                    // then insert the new line
                    server.send(editor.file.get_prevline(),
                                C_UPDATE_LINE_CONTENT);
                    server.send(editor.file.get_currline(), C_INSERT_LINE);

                    break;
                }
                case KEY_BACKSPACE:
                case KEY_DC:
                case KEY_DELETE:
                case '\b':
                    if(!editor.file.isediting)
                        break;
                    editor.file.delchar();
                    // server.send(to_string(editor.file.get_row()),
                    //             C_UPDATE_LINE_CONTENT);
                    server.send(editor.file.get_currline(),
                                C_UPDATE_LINE_CONTENT);
                    break;
                case KEY_CTRL_O: {
                    editor.file.isediting = !editor.file.isediting;
                    if(editor.file.isediting) {
                        int curry, currx;
                        getyx(static_cast<WINDOW*>(editor.file), curry, currx);
                        server.send(to_string(editor.file.get_row()),
                                    C_SWITCH_TO_EDITING_MODE);
                        editor.status.print_status(
                            "Press Ctrl+O to switch to browsing mode. Press "
                            "Ctrl+Q to quit.");
                        wmove(editor.file, curry, currx);
                    } else {
                        int curry, currx;
                        getyx(static_cast<WINDOW*>(editor.file), curry, currx);
                        server.send(to_string(editor.file.get_row()),
                                    C_SWITCH_TO_BROWSING_MODE);
                        editor.status.print_status(
                            "Press Ctrl+O to switch to editing mode. Press "
                            "Ctrl+Q to quit.");
                        wmove(editor.file, curry, currx);
                    }
                    break;
                }
                case KEY_CTRL_X: {
                    if(!editor.file.isediting)
                        break;
                    int curry, currx;
                    getyx(static_cast<WINDOW*>(editor.file), curry, currx);
                    ssize_t retval = editor.file.del_line();
                    if(retval == -1) {
                        editor.file.set_pos(curry - 1, 0);
                        break;
                    }
                    server.send(to_string(editor.file.get_row() + 1),
                                C_DELETE_LINE);
                    if(retval == 1) {

                        server.send(to_string(file_contents.back().linenum + 1),
                                    C_ADD_LINE_BACK);
                        running = S_WAITING_MODE;
                        while(running == S_WAITING_MODE)
                            ;
                    }
                    editor.file.refresh_file_content(-1);
                    editor.file.set_pos(curry - 1, 0);
                    server.send(to_string(editor.file.get_row()),
                                C_SET_CURSOR_POS);
                    break;
                }
            }
        }
    }
}


void print_welcome_screen() {
    int y = 0;
    if(max_row > welcome_screen.size() + 9 &&
       max_col > welcome_screen.front().size() + 2) {
        // if terminal is large enough to print that character image
        attron(COLOR_PAIR(2));
        for(const string& s : welcome_screen) {
            mvprintw(y++,
                     (max_col - welcome_screen.front().size() + 1) / 2,
                     "%s",
                     s.c_str());
        }
        attroff(COLOR_PAIR(2));
        attron(COLOR_PAIR(1));
        ++y;
    }

    mvprintw(y, max_col / 2 - 11, "Welcome to Hermes.");
    y += 2;

    if(!server.isconnected()) {
        int ip_y, ip_x, port_y, port_x;
        mvprintw(y++, max_col / 2 - 11, "Server ip: ");
        getyx(stdscr, ip_y, ip_x);
        mvprintw(y, max_col / 2 - 11, "Port number: ");
        getyx(stdscr, port_y, port_x);

        y += 2;
        mvprintw(y, max_col / 2 - 11, "Press CTRL+Q to quit.");
        move(ip_y, ip_x);  // ready to receive ip addrress
        string temp;
        wgetline(stdscr, temp);
        server.set_ip(temp);
        move(port_y, port_x);
        wgetline(stdscr, temp);
        server.set_port(temp);

        while(!server.connect()) {  // if connection fails
            move(max_row - 1, 0);
            clrtoeol();
            attron(COLOR_PAIR(3));  // print error message
            if(server.get_port() == "" || server.get_ip() == "")
                mvprintw(max_row - 1,
                         max_col / 2 - 20,
                         "Invalid ip/port number, please try again.");
            else
                mvprintw(max_row - 1,
                         max_col / 2 - 20,
                         "Connection to %s fails, please try again.",
                         server.get_ip().c_str());
            attroff(COLOR_PAIR(3));  // end printing error message

            move(port_y, port_x);
            clrtoeol();
            move(ip_y, ip_x);
            clrtoeol();
            refresh();
            wgetline(stdscr, temp);
            server.set_ip(temp);
            move(port_y, port_x);
            wgetline(stdscr, temp);
            server.set_port(temp);
        }

        move(y, 0);
        clrtoeol();
        mvprintw(y,
                 max_col / 2 - 17,
                 "Successfully connects to %s",
                 server.get_ip().c_str());
    } else {
        mvprintw(
            y++, max_col / 2 - 11, "Connected to: %s", server.get_ip().c_str());
        mvprintw(y++,
                 max_col / 2 - 11,
                 "Port number: %s",
                 server.get_port().c_str());
    }

    refresh();
}


bool wgetline(WINDOW* w, string& s, size_t n) {
    s.clear();
    int orig_y, orig_x;
    getyx(stdscr, orig_y, orig_x);
    int curr;  // current character to read
    while(!n || s.size() != n) {
        curr = wgetch(w);
        if(std::isprint(curr)) {
            ++orig_x;
            if(orig_x <= max_col) {
                waddch(w, curr);
                wrefresh(w);
            }

            s.push_back(curr);

        } else if(!s.empty() && (curr == KEY_BACKSPACE || curr == KEY_DC ||
                                 curr == KEY_DELETE || curr == '\b')) {
            --orig_x;
            if(orig_x <= max_col) {
                mvwdelch(w, orig_y, orig_x);
                wrefresh(w);
            }
            s.pop_back();

        } else if(curr == KEY_ENTER || curr == '\n' || curr == KEY_DOWN ||
                  curr == KEY_UP) {
            return true;
        } else if(curr == ERR) {
            if(s.empty())
                return false;
            return true;
        } else if(curr == KEY_CTRL_Q) {
            endwin();
            running = 0;
            std::exit(0);
            // return false;
        }
    }
    return true;
}

void segfault_handler(int sig) {
    // so that ncurses won't mess up our screen
    endwin();
}