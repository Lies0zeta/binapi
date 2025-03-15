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
    std::string symbol = "EUREURI";
    if (argc > 1)
    {
        symbol = std::string(argv[1]);
    }
    binapi::ws::websockets ws{ioctx, "testnet.binance.vision", "443"}; //"stream.binance.com", "9443"
    boost::json::object keys = load_keys();
    binapi::rest::api api{
        ioctx, "testnet.binance.vision", "443", keys["pk"].as_string().c_str(),
        keys["sk"].as_string().c_str(),
        1000 // recvWindow
    };

    auto start_uds = api.start_user_data_stream();
    assert(start_uds);
    std::cout << "start_uds=" << start_uds.v << std::endl;

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

    while (true)
    {
        try
        {
            ioctx.run();
            break;
        }
        catch (const std::exception &ex)
        {
            std::cerr << "std::exception: what: " << ex.what() << std::endl;

            ioctx.restart();
        }
    }
    return EXIT_SUCCESS;
}
