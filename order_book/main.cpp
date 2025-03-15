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
std::atomic<bool> done(false);

void handle_sig(int sig)
{
    (void)sig;
    ioctx.stop();
    done = true;
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

int update_order_book(boost::lockfree::queue<binapi::ws::diff_depths_t *> &ddepths_queue,
                      std::shared_ptr<binapi::order_book> &book, binapi::rest::api &api,
                      const std::string &symbol)
{
    binapi::ws::diff_depths_t *diff_depths;
    std::size_t U;

    while (!ddepths_queue.pop(diff_depths))
        std::this_thread::yield();
    U = diff_depths->U;
    while (book->lastUpdateId < U)
    {
        auto res = api.depths(symbol, 5000);
        if (!res)
        {
            std::cerr << "exchange_info error " << res.errmsg << std::endl;
            return EXIT_FAILURE;
        }

        // Reset the book using the latest snapshot
        binapi::rest::depths_t orders = std::move(res.v);
        book = binapi::order_book::construct(orders);
    }
    book->eventTime = diff_depths->E;
    while (!done)
    {
        while (ddepths_queue.pop(diff_depths))
        {
            if (book->lastUpdateId >= diff_depths->u)
            {
                delete diff_depths;
                continue;
            }
            if (book->lastUpdateId >= U && book->lastUpdateId <= diff_depths->u)
            {
                for (const auto &bid : diff_depths->b)
                {
                    book->update(bid.price, bid.amount, true);
                }
                for (const auto &ask : diff_depths->a)
                {
                    book->update(ask.price, ask.amount, false);
                }
                book->lastUpdateId = diff_depths->u;
                book->eventTime = diff_depths->E;
                delete diff_depths;
            }
            else
            {
                std::cerr << "order_book error " << std::endl;
                delete diff_depths;
                done = true;
                return EXIT_FAILURE;
            }
        }
        std::this_thread::yield();
    }
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    std::signal(SIGINT, handle_sig);  // Handle Ctrl+C
    std::signal(SIGTERM, handle_sig); // Handle kill command
    std::signal(SIGQUIT, handle_sig); // Handle program exit (e.g., via abort)
    std::size_t queue_size = 2056;
    std::string symbol = "BTCUSDC";
    if (argc > 1)
    {
        symbol = std::string(argv[1]);
    }
    boost::lockfree::queue<binapi::ws::diff_depths_t *> ddepths_queue(queue_size);
    binapi::ws::websockets ws{ioctx, "testnet.binance.vision", "443"}; //"stream.binance.com", "9443"
    boost::json::object keys = load_keys();
    binapi::rest::api api{
        ioctx, "testnet.binance.vision", "443", keys["pk"].as_string().c_str(),
        keys["sk"].as_string().c_str(),
        1000 // recvWindow
    };

    auto res = api.depths(symbol.c_str(), 5000);
    if (!res)
    {
        std::cerr << "exchange_info error: " << res.errmsg << std::endl;
        return EXIT_FAILURE;
    }
    binapi::rest::depths_t orders = std::move(res.v);
    auto book = binapi::order_book::construct(orders);
    std::thread order_book_thread(update_order_book, std::ref(ddepths_queue),
                                  std::ref(book), std::ref(api), symbol.c_str());

    ws.diff_depth(symbol.c_str(), binapi::e_freq::_100ms,
                  [&ddepths_queue](const char *fl, int ec, std::string emsg, auto depths)
                  {
                      if (ec)
                      {
                          std::cerr << "subscribe diff_depth error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                          done = true;
                          return false;
                      }
                      binapi::ws::diff_depths_t *value = new binapi::ws::diff_depths_t(depths);
                      ddepths_queue.push(value);
                      return true;
                  });

    ws.klines(symbol.c_str(), "1s",
              [&book](const char *fl, int ec, std::string emsg, auto klines)
              {
                  if (ec)
                  {
                      std::cerr << "subscribe klines error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;

                      return false;
                  }

                  std::cout << "klines: " << klines << std::endl;
                  book->print();
                  return true;
              });

    ioctx.run();
    order_book_thread.join();
    return EXIT_SUCCESS;
}
