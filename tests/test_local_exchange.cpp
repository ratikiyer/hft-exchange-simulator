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
        std::cout << "[Parser thread " << std::this_thread::get_id() 
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

int main() {
    using namespace std::chrono_literals;

    // Prepare a sequence of orders:
    std::vector<order_t> seq = {
        make_order("B1","AAPL",order_side::BUY, order_status::NEW,            100, 5,  1),
        make_order("S1","AAPL",order_side::SELL,order_status::NEW,            100, 5,  2),
        make_order("B2","AAPL",order_side::BUY, order_status::NEW,             50,10,  3),
        make_order("B2","AAPL",order_side::BUY, order_status::CANCELLED,       50,10,  4),
        make_order("B3","AAPL",order_side::BUY, order_status::NEW,             60, 8,  5),
        make_order("B4","AAPL",order_side::BUY, order_status::NEW,             55, 7,  6),
        make_order("S2","AAPL",order_side::SELL,order_status::NEW,             55, 5,  7),
        make_order("B4","AAPL",order_side::BUY, order_status::PARTIALLY_FILLED,55, 4,  8),
    };

    // 1) Hook up parser & real logger
    TestParser    parser(seq);
    logger        log("test.log");

    // 2) Start exchange (auto-creates bucket/book on first message)
    Exchange exch(&log, &parser);
    exch.start();

    // 3) Launch multiple client threads to inject orders concurrently
    const int num_clients = 3;
    std::vector<std::thread> clients;
    clients.reserve(num_clients);

    for (int i = 0; i < num_clients; ++i) {
        clients.emplace_back([&]() {
            ParsedOrder dummy;
            // Each thread calls on_msg_received while parser has orders
            while (parser.parse_message(nullptr, 0, dummy)) {
                exch.on_msg_received(nullptr, 0);
                std::this_thread::sleep_for(2ms);
            }
        });
    }

    // Wait for all client threads to finish
    for (auto& th : clients) {
        th.join();
    }

    // 4) Let matching threads drain
    std::this_thread::sleep_for(50ms);
    exch.stop();

    // 5) Read and dump the log
    std::ifstream in("test.log");
    if (!in) {
        std::cerr << "Failed to open test.log\n";
        return 1;
    }
    std::string line;
    std::cout << "=== LOG OUTPUT ===\n";
    while (std::getline(in, line)) {
        std::cout << line << "\n";
    }
    return 0;
}
