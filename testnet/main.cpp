#include <binapi/api.hpp>
#include <binapi/websocket.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/lockfree/queue.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <typeinfo>
#include <cxxabi.h>

std::string demangle(const char *name)
{
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result(demangled);
    free(demangled);
    return result;
}

std::atomic<bool> done(false);

void handle_sig(int sig)
{
    (void)sig;
    done = true;
}

int main(int argc, char *argv[])
{
    std::signal(SIGINT, handle_sig);  // Handle Ctrl+C
    std::signal(SIGTERM, handle_sig); // Handle kill command
    std::signal(SIGQUIT, handle_sig); // Handle program exit (e.g., via abort)
    std::string symbol = "EUREURI";
    if (argc > 1)
    {
        symbol = std::string(argv[1]);
    }
    boost::asio::io_context ioctx;
    binapi::ws::websockets ws{ioctx, "stream.binance.com", "9443"}; //"testnet.binance.vision" "443"

    ws.book(symbol.c_str(),
            [](const char *fl, int ec, std::string emsg, auto book)
            {
                if (ec || done)
                {
                    if (ec)
                    {
                        std::cerr << "subscribe book error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                        done = true;
                    }
                    return false;
                }
                // std::cout << "book type: " << demangle(typeid(book).name()) << std::endl;
                std::cout << "book: " << book << std::endl;
                // binapi::ws::book_ticker_t *value = new binapi::ws::book_ticker_t(book);
                return true;
            });

    ioctx.run();
    return EXIT_SUCCESS;
}

