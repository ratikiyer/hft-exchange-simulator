// test_exchange.cpp

#include "local_exchange.h"
#include <thread>
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>

// 1) Fake parser that hands back a preloaded sequence of orders
typedef std::lock_guard<std::mutex> lock_t;
struct TestParser : public OrderParser {
    std::vector<order_t> orders;
    size_t               idx = 0;
    std::mutex           mtx;

    TestParser(std::vector<order_t> v)
      : orders(std::move(v)) {}

    bool parse_message(const uint8_t*, size_t, ParsedOrder&) override {
        lock_t lock(mtx);
        return idx < orders.size();  // more orders available
    }

    order_t convert_to_order(const ParsedOrder&) override {
        lock_t lock(mtx);
        order_t o = (idx < orders.size() ? orders[idx++] : order_t{});
        // Print the thread ID and order ID for visibility
        std::cout << "[Parser " << std::this_thread::get_id()
                  << "] delivered order " << o.order_id << '\n';
        return o;
    }
};

// 2) Helper to build an order_t
order_t make_order(const char* id,
                   const char* ticker,
                   order_side side,
                   order_status status,
                   uint32_t price,
                   size_t qty,
                   uint64_t ts)
{
    order_t o{};
    std::memcpy(o.order_id, id, std::min(strlen(id), ORDER_ID_LEN));
    std::memcpy(o.ticker,   ticker, std::min(strlen(ticker),   TICKER_LEN));
    o.side      = static_cast<uint8_t>(side);
    o.status    = static_cast<uint8_t>(status);
    o.price     = price;
    o.qty       = qty;
    o.timestamp = ts;
    return o;
}

// Helper function to run one test sequence
void run_sequence(const std::vector<order_t>& seq,
                  const std::string&          log_path,
                  const std::string&          test_name)
{
    using namespace std::chrono_literals;
    std::cout << "\n=== Running " << test_name << " ===\n";

    TestParser parser(seq);
    logger     log(log_path);
    Exchange   exch(&log, &parser);
    exch.start();

    // Launch 2 client threads to inject orders concurrently
    std::vector<std::thread> clients;
    for (int i = 0; i < 2; ++i) {
        clients.emplace_back([&]() {
            ParsedOrder dummy;
            while (parser.parse_message(nullptr, 0, dummy)) {
                exch.on_msg_received(nullptr, 0);
                std::this_thread::sleep_for(1ms);
            }
        });
    }

    for (auto& th : clients) th.join();
    std::this_thread::sleep_for(20ms);
    exch.stop();

    // Dump the log
    std::ifstream in(log_path);
    if (!in) {
        std::cerr << "Cannot open " << log_path << "\n";
        return;
    }
    std::cout << "--- Log (" << log_path << ") ---\n";
    std::string line;
    while (std::getline(in, line)) {
        std::cout << line << "\n";
    }
}

int main() {
    using namespace std::chrono_literals;

    // Test 1: your original AAPL sequence
    std::vector<order_t> seq1 = {
        make_order("B1","AAPL",order_side::BUY, order_status::NEW,            100, 5,  1),
        make_order("S1","AAPL",order_side::SELL,order_status::NEW,            100, 5,  2),
        make_order("B2","AAPL",order_side::BUY, order_status::NEW,             50,10,  3),
        make_order("B2","AAPL",order_side::BUY, order_status::CANCELLED,       50,10,  4),
        make_order("B3","AAPL",order_side::BUY, order_status::NEW,             60, 8,  5),
        make_order("B4","AAPL",order_side::BUY, order_status::NEW,             55, 7,  6),
        make_order("S2","AAPL",order_side::SELL,order_status::NEW,             55, 5,  7),
        make_order("B4","AAPL",order_side::BUY, order_status::PARTIALLY_FILLED,55, 4,  8),
    };
    run_sequence(seq1, "test1.log", "Test #1: AAPL matching + cancels");

    // Test 2: large buy vs multiple smaller sells (partial fills)
    std::vector<order_t> seq2 = {
        make_order("B10","GOOG",order_side::BUY, order_status::NEW, 1000, 20, 1),
        make_order("S10","GOOG",order_side::SELL,order_status::NEW, 1000, 5,  2),
        make_order("S11","GOOG",order_side::SELL,order_status::NEW, 1000,15, 3),
        make_order("S12","GOOG",order_side::SELL,order_status::NEW, 1000,10, 4),  // leaves unfilled residue
    };
    run_sequence(seq2, "test2.log", "Test #2: GOOG partial‐fill cascade");

    // Test 3: orders on different tickers interleaved (no cross‐matching)
    std::vector<order_t> seq3 = {
        make_order("B20","MSFT",order_side::BUY, order_status::NEW, 200, 10, 1),
        make_order("S20","AAPL",order_side::SELL,order_status::NEW,150, 10, 2),
        make_order("S21","MSFT",order_side::SELL,order_status::NEW,200, 10, 3),
        make_order("B21","AAPL",order_side::BUY, order_status::NEW,150,  5, 4),
    };
    run_sequence(seq3, "test3.log", "Test #3: Mixed‐ticker isolation");

    return 0;
}