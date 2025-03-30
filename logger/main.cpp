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

std::atomic<bool> done(false);

int logger_order_book(boost::lockfree::queue<binapi::ws::diff_depths_t *> &ddepths_queue,
                      const std::string &order_book_fn, binapi::rest::api &api,
                      const std::string &symbol)
{
    std::ofstream order_book_file(order_book_fn, std::ios::out | std::ios::app);
    binapi::ws::diff_depths_t *diff_depths;
    std::ostringstream order_book_buffer;
    binapi::order_book book;
    std::size_t U;

    if (order_book_file.fail())
    {
        std::cerr << "Error opening file!" << std::endl;
        done = true;
        return EXIT_FAILURE;
    }
    while (!ddepths_queue.pop(diff_depths))
        std::this_thread::yield();
    U = diff_depths->U;
    while (!done && book.lastUpdateId < U)
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
    book.eventTime = diff_depths->E;
    order_book_buffer << book << std::endl;
    while (!done)
    {
        while (ddepths_queue.pop(diff_depths))
        {
            if (book.lastUpdateId >= diff_depths->u)
            {
                delete diff_depths;
                continue;
            }
            if (book.lastUpdateId >= U && book.lastUpdateId <= diff_depths->u)
            {
                for (const auto &bid : diff_depths->b)
                {
                    book.update(bid.price, bid.amount, true);
                }
                for (const auto &ask : diff_depths->a)
                {
                    book.update(ask.price, ask.amount, false);
                }
                book.lastUpdateId = diff_depths->u;
                book.eventTime = diff_depths->E;
                order_book_buffer << *diff_depths << std::endl;
                delete diff_depths;
                if (order_book_buffer.str().size() >= 65536)
                {
                    order_book_file << order_book_buffer.str();
                    order_book_buffer.str("");
                    order_book_buffer.clear();
                }
            }
            else
            {
                std::cerr << "order_book error " << std::endl;
                delete diff_depths;
                done = true;
                if (!order_book_buffer.str().empty())
                {
                    order_book_file << order_book_buffer.str();
                }
                order_book_file.close();
                return EXIT_FAILURE;
            }
        }
        std::this_thread::yield();
    }
    order_book_file.close();
    return EXIT_SUCCESS;
}

int logger_all(boost::lockfree::queue<binapi::ws::agg_trade_t *> &agg_trade_queue, const std::string &agg_trade_fn,
               boost::lockfree::queue<binapi::ws::kline_t *> &kline_queue, const std::string &klines_fn,
               boost::lockfree::queue<binapi::ws::book_ticker_t *> &bticker_queue, const std::string &bticker_fn,
               binapi::ws::websockets &ws)
{
    binapi::ws::agg_trade_t *agg_trade;
    std::ofstream agg_trade_file(agg_trade_fn, std::ios::out | std::ios::app);
    std::ostringstream agg_trade_buffer;

    binapi::ws::kline_t *kline;
    std::ofstream klines_file(klines_fn, std::ios::out | std::ios::app);
    std::ostringstream klines_buffer;

    binapi::ws::book_ticker_t *bticker;
    std::ofstream bticker_file(bticker_fn, std::ios::out | std::ios::app);
    std::ostringstream bticker_buffer;

    if (agg_trade_file.fail() || klines_file.fail() || bticker_file.fail())
    {
        std::cerr << "Error opening file!" << std::endl;
        done = true;
        return EXIT_FAILURE;
    }

    // Consumer loop
    while (!done)
    {
        while (agg_trade_queue.pop(agg_trade))
        {
            agg_trade_buffer << *agg_trade << std::endl;
            delete agg_trade;
            if (agg_trade_buffer.str().size() >= 65536)
            {
                agg_trade_file << agg_trade_buffer.str();
                agg_trade_buffer.str("");
                agg_trade_buffer.clear();
            }
        }

        while (kline_queue.pop(kline))
        {
            klines_buffer << *kline << std::endl;
            delete kline;
            if (klines_buffer.str().size() >= 65536)
            {
                klines_file << klines_buffer.str();
                klines_buffer.str("");
                klines_buffer.clear();
            }
        }

        while (bticker_queue.pop(bticker))
        {
            bticker_buffer << *bticker << std::endl;
            delete bticker;
            if (bticker_buffer.str().size() >= 65536)
            {
                bticker_file << bticker_buffer.str();
                bticker_buffer.str("");
                bticker_buffer.clear();
            }
        }
    }
    // CleanUp
    ws.unsubscribe_all();
    while (agg_trade_queue.pop(agg_trade))
    {
        agg_trade_buffer << *agg_trade << std::endl;
        delete agg_trade;
    }
    while (kline_queue.pop(kline))
    {
        klines_buffer << *kline << std::endl;
        delete kline;
    }
    while (bticker_queue.pop(bticker))
    {
        bticker_buffer << *bticker << std::endl;
        delete bticker;
    }
    if (!agg_trade_buffer.str().empty())
    {
        agg_trade_file << agg_trade_buffer.str();
    }
    if (!klines_buffer.str().empty())
    {
        klines_file << klines_buffer.str();
    }
    if (!bticker_buffer.str().empty())
    {
        bticker_file << bticker_buffer.str();
    }

    agg_trade_file.close();
    klines_file.close();
    bticker_file.close();
    return EXIT_SUCCESS;
}

std::string demangle(const char *name)
{
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result(demangled);
    free(demangled);
    return result;
}

std::string mkdir_today()
{
    auto now = std::chrono::system_clock::now();
    std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

    // Format the current time as a string (e.g., "2025-03-07")
    std::tm tm = *std::localtime(&currentTime);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d"); // Format: YYYY-MM-DD

    std::string folderName = oss.str(); // The folder name is the current date

    try
    {
        // Create directory with the current date as its name
        if (std::filesystem::create_directory(folderName))
        {
            std::cout << "Directory " << folderName << " created successfully!" << std::endl;
        }
        else
        {
            std::cout << "Directory " << folderName << " already exists or failed to create." << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        std::exit(1);
    }
    return folderName;
}

void handle_sig(int sig)
{
    (void)sig;
    done = true;
}

std::string getEnv(const char *var)
{
    const char *val = std::getenv(var);
    return val ? std::string(val) : "";
}

int main(int argc, char *argv[])
{
    std::string privateKey = getEnv("PRIVATE_KEY");
    std::string publicKey = getEnv("PUBLIC_KEY");

    if (privateKey.empty() || publicKey.empty())
    {
        std::cerr << "Error: Encryption keys are not set!" << std::endl;
        return EXIT_FAILURE;
    }
    std::signal(SIGINT, handle_sig);  // Handle Ctrl+C
    std::signal(SIGTERM, handle_sig); // Handle kill command
    std::signal(SIGQUIT, handle_sig); // Handle program exit (e.g., via abort)
    std::size_t queue_size = 2056;
    std::string folder = mkdir_today();
    std::string symbol = "BNBUSDC";
    if (argc > 1)
    {
        symbol = std::string(argv[1]);
    }
    boost::asio::io_context ioctx;
    binapi::ws::websockets ws{ioctx, "stream.binance.com", "9443"}; //"testnet.binance.vision", "443"
    const std::string agg_trade_fn = folder + "/" + symbol + "_agg_trades";
    boost::lockfree::queue<binapi::ws::agg_trade_t *> agg_trade_queue(queue_size);
    binapi::rest::api api{ioctx, "api.binance.com", "443", publicKey.c_str(), privateKey.c_str(), 1000};

    const std::string klines_fn = folder + "/" + symbol + "_klines";
    boost::lockfree::queue<binapi::ws::kline_t *> klines_queue(queue_size);

    const std::string bticker_fn = folder + "/" + symbol + "_book_ticker";
    boost::lockfree::queue<binapi::ws::book_ticker_t *> bticker_queue(queue_size);

    const std::string order_book_fn = folder + "/" + symbol + "_order_book";
    boost::lockfree::queue<binapi::ws::diff_depths_t *> ddepths_queue(queue_size);

    std::thread logger_thread(logger_all, std::ref(agg_trade_queue), std::cref(agg_trade_fn),
                              std::ref(klines_queue), std::cref(klines_fn),
                              std::ref(bticker_queue), std::cref(bticker_fn),
                              std::ref(ws));

    std::thread order_book_thread(logger_order_book, std::ref(ddepths_queue), std::cref(order_book_fn),
                                  std::ref(api), std::cref(symbol));

    ws.klines(symbol.c_str(), "1s",
              [&klines_queue, &logger_thread, &order_book_thread](const char *fl, int ec, std::string emsg, auto klines)
              {
                  if (ec || done)
                  {
                      if (ec)
                      {
                          std::cerr << "subscribe klines error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                          done = true;
                      }
                      logger_thread.join();
                      order_book_thread.join();
                      return false;
                  }
                  binapi::ws::kline_t *value = new binapi::ws::kline_t(klines);
                  klines_queue.push(value);
                  return true;
              });

    ws.agg_trade(symbol.c_str(),
                 [&agg_trade_queue, &logger_thread, &order_book_thread](const char *fl, int ec, std::string emsg, auto trades)
                 {
                     if (ec || done)
                     {
                         if (ec)
                         {
                             std::cerr << "subscribe trades error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                             done = true;
                         }
                         logger_thread.join();
                         order_book_thread.join();
                         return false;
                     }
                     binapi::ws::agg_trade_t *value = new binapi::ws::agg_trade_t(trades);
                     agg_trade_queue.push(value);
                     return true;
                 });

    ws.book(symbol.c_str(),
            [&bticker_queue, &logger_thread, &order_book_thread](const char *fl, int ec, std::string emsg, auto book)
            {
                if (ec || done)
                {
                    if (ec)
                    {
                        std::cerr << "subscribe book error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                        done = true;
                    }
                    logger_thread.join();
                    order_book_thread.join();
                    return false;
                }
                binapi::ws::book_ticker_t *value = new binapi::ws::book_ticker_t(book);
                bticker_queue.push(value);
                return true;
            });

    ws.diff_depth(symbol.c_str(), binapi::e_freq::_100ms,
                  [&ddepths_queue, &logger_thread, &order_book_thread](const char *fl, int ec, std::string emsg, auto depths)
                  {
                      if (ec || done)
                      {
                          if (ec)
                          {
                              std::cerr << "subscribe diff_depth error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                              done = true;
                          }
                          logger_thread.join();
                          order_book_thread.join();
                          return false;
                      }
                      binapi::ws::diff_depths_t *value = new binapi::ws::diff_depths_t(depths);
                      ddepths_queue.push(value);
                      return true;
                  });

    ioctx.run();
    return EXIT_SUCCESS;
}
