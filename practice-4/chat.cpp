#include <iostream>
#include <string>
#include <csignal>

#include "server.h"
#include "client.h"

int main(int argc, char* argv[]) {
    int optional = 3;
    if (argc < optional) {
        std::cerr << "usage: chat <port> <threads> [<nickname> <target host> <target port>]\n";
        return 1;
    }

    chat::group_t group("blacklist.config");
    io_context io_ctx;
    chat::displayer_t displayer(std::cout);
    
    tcp::endpoint my_endpt(tcp::v4(), std::atoi(argv[1]));
    auto server = std::make_shared<chat::Server>(io_ctx, my_endpt, &group);
    int thr_num = std::stoi(argv[2]);

    boost::asio::signal_set signals(io_ctx, SIGINT, SIGTERM);
    signals.async_wait([&](err_code const&, int){ io_ctx.stop(); });

    if (argc > optional) {
        std::string nickname(argv[optional]);
        displayer.display(
            chat::color_t::CYAN, "Hi, " + nickname + "! Welcome to the chat room.\n"
        );
        
        tcp::resolver resolver(io_ctx);
        auto my_addr = chat::parse_addr(my_endpt.address().to_v4().to_string().c_str(), argv[1]);
        auto tg_endpts = resolver.resolve(argv[optional + 1], argv[optional + 2]);
        chat::Client client(io_ctx, tg_endpts, my_addr);
        group.client = &client;
        client.group = &group;
    
        std::vector<std::thread> thr;
        for (int i = 0; i < thr_num; ++i) {
            thr.emplace_back([&io_ctx](){ io_ctx.run(); });
        }
        
        int msg_id = 0;
        std::string buff;
        chat::timer_t timer; timer.touch();
        while(std::getline(std::cin, buff)) {
            buff = nickname + ": " + buff;
            chat::msg_t msg(my_addr, buff, msg_id++);
            if (timer.within(500)) {
                chat::displayer_t(std::cout).display(
                    chat::color_t::RED, "(you've sent messages too frequently. please try again later)"
                );
                continue;
            }
            client.send(msg);
            timer.touch();
        }
        
        client.close();
        for (auto &t: thr) t.join();

        displayer.display(
            chat::color_t::CYAN, "GoodBye! ;)\n"
        );
    } else {
        group.client = nullptr;
        displayer.display(
            chat::color_t::CYAN, "chat server mode\n"
        );

        std::vector<std::thread> thr;
        for (int i = 0; i < thr_num - 1; ++i) {
            thr.emplace_back([&io_ctx](){ io_ctx.run(); });
        }
        io_ctx.run();
        for (auto &t: thr) t.join();
        
        displayer.display(
            chat::color_t::CYAN, "server shutdown.\n"
        );
    }

    return 0;
}
