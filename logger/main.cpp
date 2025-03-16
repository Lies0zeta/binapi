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

int logger_all(boost::lockfree::queue<binapi::ws::agg_trade_t *> &agg_trade_queue, const std::string &agg_trade_fn,
               boost::lockfree::queue<binapi::ws::kline_t *> &kline_queue, const std::string &klines_fn,
               boost::lockfree::queue<binapi::ws::book_ticker_t *> &bticker_queue, const std::string &bticker_fn,
               boost::lockfree::queue<binapi::ws::part_depths_t *> &pdepths_queue, const std::string &pdepths_fn,
               std::atomic<bool> &done, binapi::ws::websockets &ws)
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

    binapi::ws::part_depths_t *pdepths;
    std::ofstream pdepths_file(pdepths_fn, std::ios::out | std::ios::app);
    std::ostringstream pdepths_buffer;

    if (agg_trade_file.fail() || klines_file.fail() || bticker_file.fail() || pdepths_file.fail())
    {
        std::cerr << "Error opening file!" << std::endl;
        return EXIT_FAILURE;
    }

    // Consumer loop
    while (!done)
    {
        while (agg_trade_queue.pop(agg_trade))
        {
            agg_trade_buffer << *agg_trade << '\n';
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
            klines_buffer << *kline << '\n';
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
            bticker_buffer << *bticker << '\n';
            delete bticker;
            if (bticker_buffer.str().size() >= 65536)
            {
                bticker_file << bticker_buffer.str();
                bticker_buffer.str("");
                bticker_buffer.clear();
            }
        }

        while (pdepths_queue.pop(pdepths))
        {
            pdepths_buffer << *pdepths << '\n';
            delete pdepths;
            if (pdepths_buffer.str().size() >= 65536)
            {
                pdepths_file << pdepths_buffer.str();
                pdepths_buffer.str("");
                pdepths_buffer.clear();
            }
        }
    }
    // CleanUp
    ws.unsubscribe_all();
    while (agg_trade_queue.pop(agg_trade))
    {
        agg_trade_buffer << *agg_trade << '\n';
        delete agg_trade;
    }
    while (kline_queue.pop(kline))
    {
        klines_buffer << *kline << '\n';
        delete kline;
    }
    while (bticker_queue.pop(bticker))
    {
        bticker_buffer << *bticker << '\n';
        delete bticker;
    }
    while (pdepths_queue.pop(pdepths))
    {
        pdepths_buffer << *pdepths << '\n';
        delete pdepths;
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
    if (!pdepths_buffer.str().empty())
    {
        pdepths_file << pdepths_buffer.str();
    }

    agg_trade_file.close();
    klines_file.close();
    bticker_file.close();
    pdepths_file.close();
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

boost::asio::io_context ioctx;
std::atomic<bool> done(false);

void handle_sig(int sig)
{
    (void)sig;
    ioctx.stop();
    done = true;
}

int main(int argc, char *argv[])
{
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
    binapi::ws::websockets ws{ioctx, "stream.binance.com", "9443"};

    const std::string agg_trade_fn = folder + "/" + symbol + "_agg_trades";
    boost::lockfree::queue<binapi::ws::agg_trade_t *> agg_trade_queue(queue_size);

    const std::string klines_fn = folder + "/" + symbol + "_klines";
    boost::lockfree::queue<binapi::ws::kline_t *> klines_queue(queue_size);

    const std::string bticker_fn = folder + "/" + symbol + "_book_ticker";
    boost::lockfree::queue<binapi::ws::book_ticker_t *> bticker_queue(queue_size);

    const std::string pdepths_fn = folder + "/" + symbol + "_part_depths";
    boost::lockfree::queue<binapi::ws::part_depths_t *> pdepths_queue(queue_size);

    std::thread logger_thread(logger_all, std::ref(agg_trade_queue), std::cref(agg_trade_fn),
                              std::ref(klines_queue), std::cref(klines_fn),
                              std::ref(bticker_queue), std::cref(bticker_fn),
                              std::ref(pdepths_queue), std::cref(pdepths_fn),
                              std::ref(done), std::ref(ws));

    ws.klines(symbol.c_str(), "1s",
              [&klines_queue, &logger_thread](const char *fl, int ec, std::string emsg, auto klines)
              {
                  if (ec || done)
                  {
                      if (ec)
                      {
                          std::cerr << "subscribe klines error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                          done = true;
                      }
                      logger_thread.join();
                      return false;
                  }
                  binapi::ws::kline_t *value = new binapi::ws::kline_t(klines);
                  klines_queue.push(value);
                  return true;
              });

    ws.agg_trade(symbol.c_str(),
                 [&agg_trade_queue, &logger_thread](const char *fl, int ec, std::string emsg, auto trades)
                 {
                     if (ec || done)
                     {
                         if (ec)
                         {
                             std::cerr << "subscribe trades error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                             done = true;
                         }
                         logger_thread.join();
                         return false;
                     }
                     binapi::ws::agg_trade_t *value = new binapi::ws::agg_trade_t(trades);
                     agg_trade_queue.push(value);
                     return true;
                 });

    ws.book(symbol.c_str(),
            [&bticker_queue, &logger_thread](const char *fl, int ec, std::string emsg, auto book)
            {
                if (ec || done)
                {
                    if (ec)
                    {
                        std::cerr << "subscribe book error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                        done = true;
                    }
                    logger_thread.join();
                    return false;
                }
                // std::cout << "book type: " << demangle(typeid(book).name()) << std::endl;
                // std::cout << "book: " << book << std::endl;
                binapi::ws::book_ticker_t *value = new binapi::ws::book_ticker_t(book);
                bticker_queue.push(value);
                return true;
            });

    ws.part_depth(symbol.c_str(), binapi::e_levels::_20, binapi::e_freq::_100ms,
                  [&pdepths_queue, &logger_thread](const char *fl, int ec, std::string emsg, auto depths)
                  {
                      if (ec || done)
                      {
                          if (ec)
                          {
                              std::cerr << "subscribe part_depth error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                              done = true;
                          }
                          logger_thread.join();
                          return false;
                      }
                      binapi::ws::part_depths_t *value = new binapi::ws::part_depths_t(depths);
                      pdepths_queue.push(value);
                      return true;
                  });

    ioctx.run();
    return EXIT_SUCCESS;
}

// ws.diff_depth("BTCUSDT", binapi::e_freq::_100ms,
//     [](const char *fl, int ec, std::string emsg, auto depths) {
//         if ( ec ) {
//             std::cerr << "subscribe diff_depth error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;

//             return false;
//         }

//         std::cout << "diff_depths: " << depths << std::endl;

//         return true;
//     }
// );

// ws.trade("BTCUSDT",
//     [](const char *fl, int ec, std::string emsg, auto trades) {
//         if ( ec ) {
//             std::cerr << "subscribe trades error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;

//             return false;
//         }

//         std::cout << "trades: " << trades << std::endl;

//         return true;
//     }
// );

// ws.mini_tickers(
//     [](const char *fl, int ec, std::string emsg, auto mini_tickers) {
//         if ( ec ) {
//             std::cerr << "subscribe mini_tickers error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;

//             return false;
//         }

//         std::cout << "mini_tickers: " << mini_tickers << std::endl;

//         return true;
//     }
// );
