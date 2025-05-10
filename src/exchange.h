#pragma once

#include "orderbook.h"
#include "logger.h"
#include "order_parser.h"
#include "market_data_publisher.h"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include "concurrentqueue.h"

class Exchange {
public:
    struct BookThread {
        orderbook book;
        moodycamel::ConcurrentQueue<order_t> order_queue;
        std::thread thread;
    };

    Exchange(logger* logger_ptr,
            OrderParser* parser_ptr,
            MarketDataPublisher* publisher_ptr);
    ~Exchange();

    void start();
    void stop();
    void add_symbol(const char* symbol);
    void on_msg_received(const uint8_t* data, size_t len);

private:
    void enqueue_order(const order_t& order);
    void book_loop(BookThread* bt);
    void start_log_thread();
    void stop_log_thread();

    logger* logger_;
    OrderParser* parser_;
    MarketDataPublisher* publisher_;
    std::atomic<bool> running_;
    void* network_;  // Network interface pointer
    std::atomic<bool> log_running_;
    std::thread log_thread_;
    moodycamel::ConcurrentQueue<log_event_t> log_queue_;
    std::vector<order_t> order_batch_;
    std::unordered_map<std::string, BookThread> bookThreads_;
}; 