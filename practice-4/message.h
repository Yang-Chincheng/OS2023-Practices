#ifndef MESSAGE_H_
#define MESSAGE_H_

#include <cstring>
#include <string>
#include <iostream>
#include <sstream>

#include "utils.h"

namespace chat {

typedef union IPv4 {
    unsigned char addr[4];
    int ip;
} ipv4_t;

typedef struct Address {
    ipv4_t ip;
    int port;

    Address() {}
    Address(ipv4_t ip_, int port_): ip(ip_), port(port_) {}

    std::string to_string() const {
        std::stringstream ss;
        ss << int(ip.addr[0]) << "." << int(ip.addr[1]) << ".";
        ss << int(ip.addr[2]) << "." << int(ip.addr[3]) << ":" << port;
        return ss.str();
    }

} addr_t;

bool operator == (const addr_t &h1, const addr_t &h2) {
    return h1.ip.ip == h2.ip.ip && h1.port == h2.port;
}
bool operator != (const addr_t &h1, const addr_t &h2) {
    return h1.ip.ip != h2.ip.ip || h1.port != h2.port;
}

addr_t parse_addr(const char* ip, const char* port) {
    int tmp[5];
    sscanf(ip, "%d.%d.%d.%d", &tmp[0], &tmp[1], &tmp[2], &tmp[3]);
    sscanf(port, "%d", &tmp[4]);
    addr_t addr;
    addr.ip.addr[0] = tmp[0];
    addr.ip.addr[1] = tmp[1];
    addr.ip.addr[2] = tmp[2];
    addr.ip.addr[3] = tmp[3];
    addr.port = tmp[4];
    return addr;
}

typedef struct Message {
    addr_t addr;
    int ttl;
    int id;
    std::string content;

    std::string encode(const std::string &str) const {
        std::string ret = "";
        auto enc = [&](int x) -> int { return (x + addr.port) % 26; };
        for(char ch: str) {
            int x;
            if ('a' <= ch && ch <= 'z') ch = 'a' + enc(ch - 'a');
            else if ('A' <= ch && ch <= 'Z') ch = 'A' + enc(ch - 'A');
            ret += ch;
        }
        return ret;
    }

    std::string decode(const std::string &str) const {
        std::string ret = "";
        auto dec = [&](int x) -> int { return (x - addr.port % 26 + 26) % 26; };
        for (char ch: str) {
            int x;
            if ('a' <= ch && ch <= 'z') ch = 'a' + dec(ch - 'a');
            else if ('A' <= ch && ch <= 'Z') ch = 'A' + dec(ch - 'A');
            ret += ch;
        }
        return ret;
    }

    Message() {}
    Message(const addr_t &addr_, const std::string &content_, int id_, int ttl_ = DEFAULT_TTL):
        addr(addr_), ttl(ttl_), id(id_), content(content_) {}
    Message(const Message &msg): 
        addr(msg.addr), ttl(msg.ttl), id(msg.id), content(msg.content) {}

    std::string serialize() const {
        std::stringstream ss;
        ss << addr.ip.ip << " " << addr.port << " " << id << " " << ttl << ";" << encode(content) << "\0";
        return ss.str();
    }
    void deserialize(std::string str) {
        std::stringstream ss(str);
        ss >> addr.ip.ip >> addr.port >> id >> ttl;
        ss.ignore(MAX_BUFF_SIZE, ';');
        std::getline(ss, content);
        content = decode(content);
    }

    void display(color_t color) {
        displayer_t(std::cout).display(
            color, "[", addr.to_string(), "]", content
        );
    }

} msg_t;

std::ostream& operator << (std::ostream &os, const msg_t &msg) {
    os << "{ address: " << msg.addr.to_string();
    os << ", ttl: " << msg.ttl << ", id: " << msg.id;
    os << ", content: " << msg.content << " }";
    return os;
}

typedef struct MessageCache {
    const static int MAX_CACHE_SIZE = 100;
    std::list<msg_t> cache;
    
    bool hit(const msg_t &msg) {
        for (auto x: cache) {
            if (x.addr == msg.addr && x.id == msg.id) return true;
        }
        return false;
    }

    void add(const msg_t &msg) {
        if (!hit(msg)) cache.push_back(msg);
        while(cache.size() > MAX_CACHE_SIZE) cache.pop_front();
    }

} cache_t;

}

#endif 