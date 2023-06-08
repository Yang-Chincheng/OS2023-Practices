#ifndef SERVER_H_
#define SERVER_H_

#include "utils.h"
#include "group.h"

namespace chat {

struct Session: public GroupMember, public std::enable_shared_from_this<Session> {
public:
    tcp::socket socket;
    group_t *group;
    std::list<msg_t> pending;
    char rbuff[MAX_BUFF_SIZE];
    char wbuff[MAX_BUFF_SIZE];
    std::mutex mutex;

    Session(tcp::socket socket_, group_t *group_): 
        socket(std::move(socket_)), group(group_), mutex() {
            memset(rbuff, 0, sizeof(rbuff));
            memset(wbuff, 0, sizeof(wbuff));
            log.info(std::this_thread::get_id(), "<session> created");
        }

    void run() {
        group->join(shared_from_this());
        log.info(std::this_thread::get_id(), "<session> run");
        do_read();
    }

    void send(const msg_t &msg) {
        mutex.lock();
        log.info(std::this_thread::get_id(), "<session> try sending", msg);
        bool write_in_progress = !pending.empty();
        pending.push_back(msg);
        if (!write_in_progress) {
            strncpy(wbuff, pending.front().serialize().c_str(), MAX_BUFF_SIZE);
            log.trace(std::this_thread::get_id(), "<session> msg -> wbuff", pending.front());
            mutex.unlock();
            do_write();
        } else {
            mutex.unlock();
        }
    }

    void do_read() {
        auto on_read = [this](err_code ec, size_t /*place holder*/) {
            if (ec) {
                group->leave(shared_from_this());
                log.warn(std::this_thread::get_id(), "<session> on_read", ec);
            }
            else {
                msg_t msg;
                msg.deserialize(rbuff);
                log.trace(std::this_thread::get_id(), "<session> rbuff -> msg", msg);
                group->broadcast(msg);
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
                group->leave(shared_from_this());
                log.warn(std::this_thread::get_id(), "<session> on_write", ec);
            } else {
                mutex.lock();
                pending.pop_front();
                if (!pending.empty()) {
                    strncpy(wbuff, pending.front().serialize().c_str(), MAX_BUFF_SIZE);
                    log.trace(std::this_thread::get_id(), "<session> msg -> wbuff", pending.front());
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

};

using session_t = Session;

struct Server {
    tcp::acceptor acceptor;
    group_t *group;
    
    Server(boost::asio::io_context &io_ctx, const tcp::endpoint &endpt, group_t *group_):
        acceptor(io_ctx, endpt), group(group_) {
            do_accept();
        } 
    
    void do_accept() {
        auto on_accept = [this](err_code ec, tcp::socket socket) {
            if (!ec) {
                log.info(std::this_thread::get_id(), "<server> accept");
                std::make_shared<session_t>(std::move(socket), group)->run();
            }
            do_accept();
        };
        acceptor.async_accept(on_accept);
    }
};

using server_t = Server;

}

#endif 