#ifndef CLIENT_H_
#define CLIENT_H_

#include "utils.h"
#include "group.h"

namespace chat {

// group -> broadcast to all sessions and client, check if sent
// client -> send to group and via session, do not check

struct Client: public GroupMember {
    io_context &ctx;
    tcp::socket socket;
    char wbuff[MAX_BUFF_SIZE];
    char rbuff[MAX_BUFF_SIZE];
    std::list<msg_t> pending;
    group_t *group;
    cache_t cache;
    std::mutex mutex;
    addr_t addr;

    Client(io_context &ctx_, const tcp::resolver::results_type &endpts, addr_t addr_):
        ctx(ctx_), socket(ctx_), addr(addr_), mutex() {
            memset(rbuff, 0, sizeof(rbuff));
            memset(wbuff, 0, sizeof(wbuff));
            do_connect(endpts);
        }

    void send(const msg_t &msg) {
        log.trace(std::this_thread::get_id(), "<client> try sending", msg);
        auto on_post = [this, msg]() {
            mutex.lock();
            bool write_in_progress = !pending.empty();
            group->broadcast(msg);
            pending.push_back(msg);
            if (!write_in_progress) {
                strncpy(wbuff, pending.front().serialize().c_str(), MAX_BUFF_SIZE);
                log.trace(std::this_thread::get_id(), "<client> send msg -> wbuff", msg);
                mutex.unlock();
                do_write();
            } else {
                mutex.unlock();
            }
        };
        boost::asio::post(ctx, on_post);
    }

    void close() {
        auto on_post = [this]() { do_close(); };
        boost::asio::post(ctx, on_post);
    }

    void do_connect(const tcp::resolver::results_type &endpts) {
        auto on_connect = [this](err_code ec, tcp::endpoint) {
            if (ec) log.warn(std::this_thread::get_id(), "<client> on connect", ec);
            else {
                log.info(std::this_thread::get_id(), "<client> connected");
                do_read();
            }
        };
        boost::asio::async_connect(socket, endpts, on_connect);
    }

    void do_read() {
        auto on_read = [this](err_code ec, size_t) {
            if (ec) {
                log.warn(std::this_thread::get_id(), "<client> on_read", ec);
                do_close();
            }
            else {
                mutex.lock();
                msg_t msg; 
                msg.deserialize(rbuff);
                log.trace(std::this_thread::get_id(), "<client> rbuff -> msg", msg);
                if (msg.ttl > 0 && !cache.hit(msg)) {
                    cache.add(msg);
                    if (msg.addr == addr) msg.display(color_t::BLUE);
                    else msg.display(color_t::GREEN);
                    mutex.unlock();
                    if (--msg.ttl > 0) group->broadcast(msg);
                } else {
                    mutex.unlock();
                }
                do_read();
            }
        };
        boost::asio::async_read(
            socket,
            boost::asio::buffer(rbuff, MAX_BUFF_SIZE),
            on_read
        );
    }

    void do_write() {
        auto on_write = [this](err_code ec, size_t) {
            if (ec) {
                log.info(std::this_thread::get_id(), "<client> on_write", ec);
                do_close();
            } else {
                mutex.lock();
                pending.pop_front();
                if (!pending.empty()) {
                    strncpy(wbuff, pending.front().serialize().c_str(), MAX_BUFF_SIZE);
                    log.trace(std::this_thread::get_id(), "<client> write msg -> wbuff", pending.front());
                    mutex.unlock();
                    do_write();
                } else {
                    mutex.unlock();
                }
            }
        };
        boost::asio::async_write(
            socket,
            boost::asio::buffer(wbuff, MAX_BUFF_SIZE),
            on_write
        );
    }

    void do_close() {
        log.info(std::this_thread::get_id(), "<client> closed");
        socket.close();
    }
};

using client_t = Client;

}

#endif