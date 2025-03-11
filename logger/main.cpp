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

// Template Consumer function
template <typename T>
void logger(boost::lockfree::queue<T*>& queue, const std::string& filename, std::atomic<bool>& done)
{
    T* value;
    std::ofstream out_file(filename, std::ios::out | std::ios::app); // Open file in append mode
    std::ostringstream buffer;

    if (out_file.fail())
    {
        std::cerr << "Error opening file!" << std::endl;
        return;
    }

    // Consumer loop
    while (!done || !queue.empty())
    {
        while (queue.pop(value))
        {
            buffer << *value << '\n';
            delete value;
            if (buffer.str().size() >= 65536)
            {
                out_file << buffer.str();
                buffer.str("");
                buffer.clear();
            }
        }
        std::this_thread::yield(); // Yield to avoid busy-waiting
    }

    // Write any remaining items in the batch to the file
    if (!buffer.str().empty())
    {
        out_file << buffer.str();
    }

    out_file.close(); // Close the file
}

std::string demangle(const char* name)
{
    int status = 0;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
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
    oss << std::put_time(&tm, "%Y-%m-%d");  // Format: YYYY-MM-DD

    std::string folderName = oss.str();  // The folder name is the current date

    try {
        // Create directory with the current date as its name
        if (std::filesystem::create_directory(folderName)) {
            std::cout << "Directory " << folderName << " created successfully!" << std::endl;
        }
        else {
            std::cout << "Directory " << folderName << " already exists or failed to create." << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::exit(1);
    }
    return folderName;
}

std::atomic<bool> done(false);

void handle_sig(int sig) {
    (void)sig;
    done = true;
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, handle_sig);   // Handle Ctrl+C
    std::signal(SIGTERM, handle_sig);  // Handle kill command
    std::signal(SIGQUIT, handle_sig); // Handle program exit (e.g., via abort)

    std::string folder = mkdir_today();
    std::string symbol = "BTCUSDT";
    if (argc > 1) {
        symbol = std::string(argv[1]);
    }
    boost::asio::io_context ioctx;
    binapi::ws::websockets ws{ ioctx, "stream.binance.com", "9443" };

    const std::string agg_trade_fn = folder + "/" + symbol + "_agg_trades";
    boost::lockfree::queue<binapi::ws::agg_trade_t*> agg_trade_queue(1024);
    std::thread agg_trade_logger_thread(logger<binapi::ws::agg_trade_t>, std::ref(agg_trade_queue), std::cref(agg_trade_fn), std::ref(done));

    const std::string klines_fn = folder + "/" + symbol + "_klines";
    boost::lockfree::queue<binapi::ws::kline_t*> klines_queue(1024);
    std::thread klines_logger_thread(logger<binapi::ws::kline_t>, std::ref(klines_queue), std::cref(klines_fn), std::ref(done));

    const std::string bticker_fn = folder + "/" + symbol + "_book_ticker";
    boost::lockfree::queue<binapi::ws::book_ticker_t*> bticker_queue(1024);
    std::thread bticker_logger_thread(logger<binapi::ws::book_ticker_t>, std::ref(bticker_queue), std::cref(bticker_fn), std::ref(done));

    const std::string pdepths_fn = folder + "/" + symbol + "_part_depths";
    boost::lockfree::queue<binapi::ws::part_depths_t*> pdepths_queue(1024);
    std::thread pdepths_logger_thread(logger<binapi::ws::part_depths_t>, std::ref(pdepths_queue), std::cref(pdepths_fn), std::ref(done));

    ws.klines(symbol.c_str(), "1s",
        [&klines_queue, &klines_logger_thread](const char* fl, int ec, std::string emsg, auto klines) {
            if (ec || done) {
                std::cerr << "subscribe klines error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                done = true;
                klines_logger_thread.join();
                return false;
            }
            binapi::ws::kline_t* value = new binapi::ws::kline_t(klines);
            klines_queue.push(value);
            return true;
        }
    );

    ws.agg_trade(symbol.c_str(),
        [&agg_trade_queue, &agg_trade_logger_thread](const char* fl, int ec, std::string emsg, auto trades)
        {
            if (ec || done)
            {
                std::cerr << "subscribe trades error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                done = true;
                agg_trade_logger_thread.join();
                return false;
            }
            binapi::ws::agg_trade_t* value = new binapi::ws::agg_trade_t(trades);
            agg_trade_queue.push(value);
            return true;
        });

    ws.book(symbol.c_str(),
        [&bticker_queue, &bticker_logger_thread](const char* fl, int ec, std::string emsg, auto book) {
            if (ec || done) {
                std::cerr << "subscribe book error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                done = true;
                bticker_logger_thread.join();
                return false;
            }
            // std::cout << "book type: " << demangle(typeid(book).name()) << std::endl;
            // std::cout << "book: " << book << std::endl;
            binapi::ws::book_ticker_t* value = new binapi::ws::book_ticker_t(book);
            bticker_queue.push(value);
            return true;
        }
    );

    ws.part_depth(symbol.c_str(), binapi::e_levels::_20, binapi::e_freq::_100ms,
        [&pdepths_queue, &pdepths_logger_thread](const char* fl, int ec, std::string emsg, auto depths) {
            if (ec || done) {
                std::cerr << "subscribe part_depth error: fl=" << fl << ", ec=" << ec << ", emsg=" << emsg << std::endl;
                done = true;
                pdepths_logger_thread.join();
                return false;
            }
            binapi::ws::part_depths_t* value = new binapi::ws::part_depths_t(depths);
            pdepths_queue.push(value);
            return true;
        }
    );

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

    // boost::asio::steady_timer timer2{ ioctx, std::chrono::steady_clock::now() + std::chrono::seconds(60) };
    // timer2.async_wait([&ws, &done, &agg_trade_logger_thread, &klines_logger_thread, &bticker_logger_thread, &pdepths_logger_thread](const auto& /*ec*/)
    //     {
    //         std::cout << "Closing open websockets" << std::endl;
    //         ws.async_unsubscribe_all();
    //         done = true;
    //         agg_trade_logger_thread.join();
    //         klines_logger_thread.join();
    //         bticker_logger_thread.join();
    //         pdepths_logger_thread.join();
    //     });

    ioctx.run();

    return EXIT_SUCCESS;
}


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

// #if 1
//     boost::asio::steady_timer timer0{ioctx, std::chrono::steady_clock::now() + std::chrono::seconds(5)};
//     timer0.async_wait([&ws, book_handler](const auto &/*ec*/){
//         std::cout << "unsubscribing book_handler: " << book_handler << std::endl;
//         ws.unsubscribe(book_handler);
//     });
// #endif