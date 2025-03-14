#include <binapi/api.hpp>
#include <binapi/websocket.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/json.hpp>
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

boost::asio::io_context ioctx;

void handle_sig(int sig)
{
    (void)sig;
    ioctx.stop();
}

boost::json::object load_keys()
{
    std::ifstream file("../keys.json");
    if (!file)
    {
        std::cerr << "Error: Cannot open file" << std::endl;
        return EXIT_FAILURE;
    }
    std::string jsonStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    boost::json::value jv = boost::json::parse(jsonStr);
    return jv.as_object();
}

int main(int argc, char *argv[])
{
    std::signal(SIGINT, handle_sig);  // Handle Ctrl+C
    std::signal(SIGTERM, handle_sig); // Handle kill command
    std::signal(SIGQUIT, handle_sig); // Handle program exit (e.g., via abort)
    std::string symbol = "BNBUSDC";
    if (argc > 1)
    {
        symbol = std::string(argv[1]);
    }
    binapi::ws::websockets ws{ioctx, "stream.binance.com", "9443"}; //"testnet.binance.vision" "443"
    boost::json::object keys = load_keys();
    binapi::rest::api api{
        ioctx, "testnet.binance.vision", "443", keys["pk"].as_string().c_str(),
        keys["sk"].as_string().c_str(),
        1000 // recvWindow
    };

    auto res0 = api.exchange_info(symbol.c_str());
    if (!res0)
    {
        std::cerr << "exchange_info error: " << res0.errmsg << std::endl;
        return EXIT_FAILURE;
    }
    binapi::rest::exchange_info_t exchange_info = std::move(res0.v);
    std::cout << "exchange info: " << exchange_info.get_by_symbol(symbol.c_str()) << std::endl;

    auto res = api.account_info();
    if (!res)
    {
        std::cerr << "account_info error: " << res.errmsg << std::endl;
        return EXIT_FAILURE;
    }
    binapi::rest::account_info_t account_info = std::move(res.v);
    std::cout << " EUR balance: " << account_info.get_balance("EUR") << std::endl;
    std::cout << " EURI balance: " << account_info.get_balance("EURI") << std::endl;

    auto start_uds = api.start_user_data_stream();
    assert(start_uds);
    std::cout << "start_uds=" << start_uds.v << std::endl
              << std::endl;

    ws.book(symbol.c_str(),
            [&api, &symbol](const char *fl, int ec, std::string emsg, auto book)
            {
                if (ec)
                {
                    std::cerr << "subscribe book error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                    return false;
                }
                // std::cout << "book type: " << demangle(typeid(book).name()) << std::endl;
                binapi::ws::book_ticker_t book_ticker = std::move(book);
                std::cout << "book: " << book << std::endl;
                auto order_res = api.new_order(symbol, binapi::e_side::buy, binapi::e_type::limit, 
                                            binapi::e_time::GTC, binapi::e_trade_resp_type::ACK, book.b);

                return true;
            });

    ws.userdata(start_uds.v.listenKey.c_str(), [](const char *fl, int ec, std::string errmsg, binapi::userdata::account_update_t msg) -> bool
                {
            if ( ec ) {
                std::cout << "account update: fl=" << fl << ", ec=" << ec << ", errmsg: " << errmsg << ", msg: " << msg << std::endl;
                return false;
            }

            std::cout << "account update:\n" << msg << std::endl;
            return true; }, [](const char *fl, int ec, std::string errmsg, binapi::userdata::balance_update_t msg) -> bool
                {
            if ( ec ) {
                std::cout << "balance update: fl=" << fl << ", ec=" << ec << ", errmsg: " << errmsg << ", msg: " << msg << std::endl;
                return false;
            }

            std::cout << "balance update:\n" << msg << std::endl;
            return true; }, [](const char *fl, int ec, std::string errmsg, binapi::userdata::order_update_t msg) -> bool
                {
            if ( ec ) {
                std::cout << "order update: fl=" << fl << ", ec=" << ec << ", errmsg: " << errmsg << ", msg: " << msg << std::endl;
                return false;
            }

            std::cout << "order update:\n" << msg << std::endl;
            return true; });

    ioctx.run();

    // while ( true ) {
    //     try {
    //         ioctx.run();
    //         break;
    //     } catch (const std::exception &ex) {
    //         std::cerr << "std::exception: what: " << ex.what() << std::endl;

    //         ioctx.restart();
    //     }
    // }
    return EXIT_SUCCESS;
}
