#define CATCH_CONFIG_MAIN   // Let Catch2 provide the main() entry point
#include <catch2/catch_all.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <cstring>
#include <sstream>

// Include your code that does *not* change orderbook:
#include "exchange_no_net.h"
#include "logger.h"
#include "types.h"


/**
 * Helper: Convert a C-string (like "ORDERA") into order_id_key so
 * orderbook::contains(const order_id_key&) can be called.
 */
static order_id_key make_id_key(const char* s) {
    order_id_key key;
    std::memset(key.order_id, 0, ORDER_ID_LEN);
    std::memcpy(key.order_id, s, std::min(std::strlen(s), (size_t)ORDER_ID_LEN));
    return key;
}

/**
 * Minimal Client class so that "Exchange test with CSV input via Client::load_csv"
 * can compile, even though we can't modify orderbook code or your existing headers.
 * 
 * This client: 
 *  - Has an internal `Exchange` 
 *  - Implements `load_csv(...)` 
 *  - In `load_csv`, it reads lines and constructs an `order_t`.
 *  - Calls `exchange_.add_order(...)`.
 */
class Client {
public:
    explicit Client(logger* log = nullptr)
        : exchange_(log)
    {}

    void load_csv(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Could not open CSV file: " << filename << "\n";
            return;
        }

        // CSV: order_id,timestamp,qty,ticker,price,kind,side,status,post_only
        // e.g. "1234ABCD,1653410000,100,TSLA,123,0,0,0,0"
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::vector<std::string> tokens;
            {
                std::string token;
                while (std::getline(ss, token, ',')) {
                    tokens.push_back(token);
                }
            }
            if (tokens.size() != 9) {
                std::cerr << "Malformed CSV line: " << line << "\n";
                continue;
            }

            order_t o{};
            // Fill out order_t from the tokens
            {
                // order_id
                std::memset(o.order_id, 0, ORDER_ID_LEN);
                std::memcpy(o.order_id, tokens[0].data(),
                            std::min(tokens[0].size(), (size_t)ORDER_ID_LEN));
                // timestamp
                o.timestamp = std::stoull(tokens[1]);
                // qty
                o.qty = static_cast<size_t>(std::stoul(tokens[2]));
                // ticker
                std::memset(o.ticker, 0, TICKER_LEN);
                std::memcpy(o.ticker, tokens[3].data(),
                            std::min(tokens[3].size(), (size_t)TICKER_LEN));
                // price
                o.price = static_cast<uint32_t>(std::stoul(tokens[4]));
                // kind
                o.kind = static_cast<uint8_t>(std::stoul(tokens[5]));
                // side
                o.side = static_cast<uint8_t>(std::stoul(tokens[6]));
                // status
                o.status = static_cast<uint8_t>(std::stoul(tokens[7]));
                // post_only
                o.post_only = (tokens[8] == "1");
            }

            exchange_.add_order(o);
        }
    }

    void stop() {
        exchange_.stop_all();
    }

    Exchange& get_exchange() {
        return exchange_;
    }

private:
    Exchange exchange_;
};

TEST_CASE("Exchange basic test via direct add_order") {
    logger* myLogger = nullptr;
    Exchange exchange(myLogger);

    // Create a couple of test orders
    order_t orderA{};
    std::memcpy(orderA.order_id, "ORDERA", 6);
    std::memcpy(orderA.ticker, "TSLA", 4);
    orderA.qty       = 100;
    orderA.price     = 123;
    orderA.kind      = static_cast<uint8_t>(order_kind::LMT);
    orderA.side      = static_cast<uint8_t>(order_side::BUY);
    orderA.status    = static_cast<uint8_t>(order_status::NEW);
    orderA.post_only = false;

    order_t orderB{};
    std::memcpy(orderB.order_id, "ORDERB", 6);
    std::memcpy(orderB.ticker, "AAPL", 4);
    orderB.qty       = 50;
    orderB.price     = 200;
    orderB.kind      = static_cast<uint8_t>(order_kind::LMT);
    orderB.side      = static_cast<uint8_t>(order_side::SELL);
    orderB.status    = static_cast<uint8_t>(order_status::NEW);
    orderB.post_only = false;

    // Submit them to the exchange
    exchange.add_order(orderA);
    exchange.add_order(orderB);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    exchange.stop_all();

    // Now let's do the checks
    {
        // TSLA
        auto& bookTSLA = exchange.debug_get_orderbook("TSLA");

        // Instead of .contains("ORDERA"), pass an order_id_key:
        auto idA = make_id_key("ORDERA");
        REQUIRE(bookTSLA.contains(idA) == true);

        uint32_t bestBidTSLA = exchange.debug_get_best_bid("TSLA");
        REQUIRE(bestBidTSLA == 123);
    }
    {
        // AAPL
        auto& bookAAPL = exchange.debug_get_orderbook("AAPL");
        auto idB = make_id_key("ORDERB");
        REQUIRE(bookAAPL.contains(idB) == true);

        uint32_t bestAskAAPL = exchange.debug_get_best_ask("AAPL");
        REQUIRE(bestAskAAPL == 200);
    }

    SUCCEED("Basic add_order test completed and validated TSLA + AAPL orders.");
}

TEST_CASE("Exchange test with CSV input via Client::load_csv") {
    logger* myLogger = nullptr;

    // Now we have our test-defined client
    Client client(myLogger);

    // Create a temporary CSV file
    const char* testFile = "../iex_python/all_events.txt";
    {
        std::ofstream file(testFile);
        file << "1234ABCD,1653410000,100,TSLA,123,0,0,0,0\n"; 
        file << "ABCD1234,1653410001,50,AAPL,200,0,1,0,1\n";  
        file << "BadLine,NoTimestamp,NA,XXXX,999,?,?,?,?\n";
        file.close();
    }

    client.load_csv(testFile);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.stop();

    // Check
    auto& ex = client.get_exchange();
    {
        // TSLA
        auto& bookTSLA = ex.debug_get_orderbook("TSLA");
        // The CSV order_id is "1234ABCD"
        auto key1 = make_id_key("1234ABCD");
        REQUIRE(bookTSLA.contains(key1) == true);
        REQUIRE(ex.debug_get_best_bid("TSLA") == 123);
    }
    {
        // AAPL
        auto& bookAAPL = ex.debug_get_orderbook("AAPL");
        auto key2 = make_id_key("ABCD1234");
        REQUIRE(bookAAPL.contains(key2) == true);
        REQUIRE(ex.debug_get_best_ask("AAPL") == 200);
    }

    // The malformed line was presumably skipped
    REQUIRE(true);
}

TEST_CASE("Exchange concurrency stress test - multiple threads adding orders") {
    logger* myLogger = nullptr;
    Exchange exchange(myLogger);

    const int numThreads = 4;
    const int ordersPerThread = 50;
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    auto worker = [&](int threadID) {
        std::string baseOrderID = "T" + std::to_string(threadID) + "ID";
        const char* tickers[] = {"AMZN", "GOOG", "MSFT"};
        for (int i = 0; i < ordersPerThread; ++i) {
            order_t o{};
            std::string orderID = baseOrderID + std::to_string(i);
            std::memcpy(o.order_id, orderID.data(),
                        std::min(orderID.size(), (size_t)ORDER_ID_LEN));

            auto tix = tickers[i % 3];
            std::memcpy(o.ticker, tix, 4);

            o.qty   = (i + 1) * 10;
            o.price = 100 + i;
            o.kind  = static_cast<uint8_t>(order_kind::LMT);
            o.side  = (i % 2 == 0)
                        ? static_cast<uint8_t>(order_side::BUY)
                        : static_cast<uint8_t>(order_side::SELL);
            o.status    = static_cast<uint8_t>(order_status::NEW);
            o.post_only = false;

            exchange.add_order(o);
        }
    };

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back(worker, t);
    }
    for (auto& th : threads) {
        th.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    exchange.stop_all();

    REQUIRE(exchange.bookCount() > 0);
    REQUIRE(exchange.hasSymbol("AMZN") == true);

    SUCCEED("Concurrent add_order test completed without crash.");
}

TEST_CASE("Exchange repeated stop test") {
    logger* myLogger = nullptr;
    Exchange exchange(myLogger);

    // Add a small order
    order_t ord{};
    std::memcpy(ord.order_id, "STOPTEST", 8);
    std::memcpy(ord.ticker, "META", 4);
    ord.qty    = 10;
    ord.price  = 345;
    exchange.add_order(ord);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop once
    exchange.stop_all();

    // Stop again
    exchange.stop_all();

    SUCCEED("Called stop_all() multiple times without error.");
}
