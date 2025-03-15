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

    ioctx.run();
    return EXIT_SUCCESS;
}
