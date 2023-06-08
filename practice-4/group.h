#ifndef GROUP_H_
#define GROUP_H_

#include <iostream>
#include <fstream>
#include <cstring>
#include <memory>
#include <set>
#include <vector>
#include <deque>

#include "utils.h"
#include "message.h"

namespace chat {

struct GroupMember {
    virtual ~GroupMember() {}
    virtual void send(const msg_t &msg) = 0;
};
typedef GroupMember member_t;

typedef struct Group {
    static const int MAX_RECENT_MSG_NUM = 20;
    std::list<msg_t> recent_msg;
    cache_t cache;
    std::set<std::shared_ptr<member_t>> members;
    member_t *client;
    std::mutex mutex;
    std::vector<addr_t> black_list;

    Group(std::string config_filename): cache(), members(), mutex() {
        std::fstream fs(config_filename, std::ios::in);
        if (fs.is_open()) {
            std::string host, port;
            while(fs >> host >> port) {
                black_list.emplace_back(parse_addr(host.c_str(), port.c_str()));
            }
        }
    }

    void join(std::shared_ptr<member_t> new_member) {
        const std::lock_guard<std::mutex> lock(mutex);
        log.info(std::this_thread::get_id(), "<group> member join", new_member);
        members.insert(new_member);
        for (auto msg: recent_msg) new_member->send(msg);
    }

    void leave(std::shared_ptr<member_t> quit_member) {
        const std::lock_guard<std::mutex> lock(mutex);
        log.info(std::this_thread::get_id(), "<group> member leave", quit_member);
        members.erase(quit_member);
    }

    void broadcast(const msg_t &msg) {
        auto find_in_black_list = [this](addr_t addr) -> bool {
            for (auto &x: black_list) if (addr == x) return true;
            return false;
        };
        msg_t send_msg(msg);
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (--send_msg.ttl <= 0 || cache.hit(msg) || find_in_black_list(msg.addr)) {
                log.trace(std::this_thread::get_id(), "<group> broadcast blocked", send_msg);
                return ;
            }
            log.trace(std::this_thread::get_id(), "<group> broadcast", send_msg);
            cache.add(send_msg);
        }

        for (auto &member: members) member->send(send_msg);
        if (client) client->send(send_msg);
        
        {
            const std::lock_guard<std::mutex> lock(mutex);
            recent_msg.push_back(send_msg);
            while(recent_msg.size() > MAX_RECENT_MSG_NUM) {
                recent_msg.pop_front();
            }
        }
    }

} group_t;

}

#endif