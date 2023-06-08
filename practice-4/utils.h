#ifndef UTILS_H_
#define UTILS_H_

#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <memory>
#include <list>
#include <thread>
#include <boost/asio.hpp>
#include <chrono>

using err_code = boost::system::error_code;
using boost::asio::ip::tcp;
using boost::asio::io_context;

namespace chat {    

const int MAX_BUFF_SIZE = 1024;
const int DEFAULT_TTL = 10;

typedef enum Color {
    PLAIN, BLUE, GREEN, CYAN, RED
} color_t;

typedef struct Displayer {
    std::ostream &os;

    Displayer(std::ostream &os_): os(os_) {}

    void do_display() {}
    template <typename Arg, typename... Args>
    void do_display(Arg&& arg, Args&&... args) {
        os << std::forward<Arg>(arg) << " ";
        do_display(std::forward<Args>(args)...);
    }

    template <typename Arg, typename... Args>
    void display_plain(Arg&& arg, Args&&... args) {
        do_display(std::forward<Arg>(arg), std::forward<Args>(args)...);
        os << std::endl;
    }

    template <typename Arg, typename... Args>
    void display(color_t color, Arg&& arg, Args&&... args) {
        switch(color) {
            case color_t::PLAIN: os << "\033[0m"; break;
            case color_t::BLUE: os << "\033[1;34m"; break;
            case color_t::GREEN: os << "\033[1;32m"; break;
            case color_t::CYAN: os << "\033[1;36m"; break;
            case color_t::RED: os << "\033[1;31m"; break;
        }
        do_display(std::forward<Arg>(arg), std::forward<Args>(args)...);
        os << "\033[0m\n";
    }

} displayer_t;

struct Logger {
    
    typedef enum Level {
        TRACE, INFO, WARN, FATAL
    } level_t;
    
    displayer_t displayer;
    level_t level;

    Logger(std::ostream &os_, level_t level_): displayer(os_), level(level_) {}

    template <typename... Args>
    void trace(Args&&... args) {
        if (level <= TRACE) {
            displayer.display_plain(std::move("[TRACE]"), std::forward<Args>(args)...);
        }
    }
    
    template <typename... Args>
    void info(Args&&... args) {
        if (level <= INFO) {
            displayer.display_plain(std::move("[INFO]"), std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void warn(Args&&... args) {
        if (level <= WARN) {
            displayer.display(color_t::RED, std::move("[WARN]"), std::forward<Args>(args)...);
        }
    }
    
    template <typename... Args>
    void fatal(Args&&... args) {
        if (level <= FATAL) {
            displayer.display(color_t::RED, std::move("[FATAL]"), std::forward<Args>(args)...);
            exit(-1);
        }
    }

};

Logger log(std::cout, Logger::Level::TRACE);

typedef struct Timer {
    std::chrono::system_clock::time_point tp;

    void touch() {
        tp = std::chrono::system_clock::now();
    }

    bool exceed(int millis) {
        auto cur = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(cur - tp).count() > millis;
    }
    
    bool within(int millis) {
        auto cur = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(cur - tp).count() < millis;
    }

} timer_t;

}

#endif