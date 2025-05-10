// exchange.cpp
#include "exchange.h"
#include "orderbook.h"
#include "logger.h"
#include "order_parser.h"
#include "market_data_publisher.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <utility>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <array>
#include <memory>
#include <atomic>

using namespace std::chrono_literals;

// Constants for optimization
static constexpr size_t ORDER_BATCH_SIZE = 100;
static constexpr size_t LOG_QUEUE_SIZE = 10000;
static constexpr size_t MAX_CACHE_SIZE = 10000;
static constexpr size_t INITIAL_BUCKET_CAPACITY = 100;
static constexpr size_t INITIAL_SYMBOL_CAPACITY = 1000;

// Optimized bucket lookup table
static const std::array<std::string, 26> FIRST_CHAR_BUCKETS = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
};

// Thread-safe bucket cache with size limit
class BucketCache {
private:
    std::unordered_map<std::string, std::string> cache_;
    std::mutex mutex_;
    const size_t max_size_;

public:
    BucketCache(size_t max_size) : max_size_(max_size) {
        cache_.reserve(max_size);
    }

    std::string get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        return it != cache_.end() ? it->second : "";
    }

    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.size() < max_size_) {
            cache_[key] = value;
        }
    }
};

static BucketCache bucket_cache(MAX_CACHE_SIZE);

// Optimized bucket calculation
static std::string get_bucket(const std::string& sym) {
    if (sym.empty()) return "";
    
    // Check cache first
    std::string cached = bucket_cache.get(sym);
    if (!cached.empty()) {
        return cached;
    }

    char c0 = std::toupper(sym[0]);
    char c1 = sym.size() > 1 ? std::toupper(sym[1]) : '\0';
    std::string result;

    if (c0 >= 'A' && c0 <= 'Z') {
        switch (c0) {
            case 'E':
                result = (c1 >= 'A' && c1 <= 'E') ? "EA-E" : "EF-Z";
                break;
            case 'I':
                result = (c1 >= 'A' && c1 <= 'E') ? "IA-E" : "IF-Z";
                break;
            case 'P':
                result = (c1 >= 'A' && c1 <= 'E') ? "PA-E" : "PF-Z";
                break;
            case 'S':
                if (c1 >= 'A' && c1 <= 'E') result = "SA-E";
                else if (c1 >= 'F' && c1 <= 'N') result = "SF-N";
                else result = "SO-Z";
                break;
            default:
                result = FIRST_CHAR_BUCKETS[c0 - 'A'];
        }
    }

    bucket_cache.put(sym, result);
    return result;
}

// Optimized bucket books storage
static std::unordered_map<
    std::string,                                 // bucket label
    std::unordered_map<std::string, orderbook>   // symbol → orderbook
> bucket_books;

Exchange::Exchange(logger* logger_ptr,
                  OrderParser* parser_ptr,
                  MarketDataPublisher* publisher_ptr)
    : logger_(logger_ptr)
    , parser_(parser_ptr)
    , publisher_(publisher_ptr)
    , running_(false)
    , network_(nullptr)
    , log_running_(false)
    , order_batch_()
{
    order_batch_.reserve(ORDER_BATCH_SIZE);
    log_queue_.reserve(LOG_QUEUE_SIZE);
    start_log_thread();
}

Exchange::~Exchange() {
    stop();
    stop_log_thread();
    delete network_;
}

void Exchange::start() {
    running_.store(true);
    publisher_->start();
    if (network_) network_->start();
}

void Exchange::stop() {
    if (!running_.exchange(false)) return;
    if (network_) network_->stop();
    publisher_->stop();
    for (auto& [sym, bt] : bookThreads_) {
        if (bt.thread.joinable()) {
            bt.thread.join();
        }
    }
}

void Exchange::add_symbol(const char* symbol) {
    std::string sym(symbol, TICKER_LEN);
    
    auto [it, inserted] = bookThreads_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(sym),
        std::forward_as_tuple(
            orderbook(logger_),
            moodycamel::ConcurrentQueue<order_t>(),
            std::thread()
        )
    );

    if (!inserted) return;

    auto& bt = it->second;
    bt.thread = std::thread(&Exchange::book_loop, this, &bt);
}

void Exchange::on_msg_received(const uint8_t* data, size_t len) {
    ParsedOrder parsed;
    if (!parser_->parse_message(data, len, parsed)) {
        return;
    }
    
    order_t order = parser_->convert_to_order(parsed);
    enqueue_order(order);
}

void Exchange::enqueue_order(const order_t& order) {
    std::string sym(order.ticker, TICKER_LEN);
    auto it = bookThreads_.find(sym);
    if (it == bookThreads_.end()) return;
    
    it->second.order_queue.enqueue(order);
}

void Exchange::start_log_thread() {
    log_running_.store(true);
    log_thread_ = std::thread([this]() {
        log_event_t event;
        std::vector<log_event_t> batch;
        batch.reserve(LOG_QUEUE_SIZE);

        while (log_running_.load()) {
            // Batch process log events
            while (batch.size() < LOG_QUEUE_SIZE && log_queue_.try_dequeue(event)) {
                batch.push_back(event);
            }

            if (!batch.empty()) {
                for (const auto& e : batch) {
                    logger_->process_event(e);
                }
                batch.clear();
            } else {
                std::this_thread::sleep_for(1us);
            }
        }
    });
}

void Exchange::stop_log_thread() {
    log_running_.store(false);
    if (log_thread_.joinable()) {
        log_thread_.join();
    }
}

void Exchange::book_loop(BookThread* bt) {
    std::vector<order_t> batch;
    batch.reserve(ORDER_BATCH_SIZE);

    while (running_.load()) {
        // Batch process orders
        while (batch.size() < ORDER_BATCH_SIZE && bt->order_queue.try_dequeue(batch.emplace_back())) {
            // Continue filling batch
        }

        if (batch.empty()) {
            std::this_thread::sleep_for(1us);
            continue;
        }

        // Process the batch
        for (const auto& order : batch) {
            order_id_key key;
            std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

            auto status = static_cast<order_status>(order.status);
            order_result res;

            switch (status) {
                case order_status::NEW:
                    res = bt->book.add(order);
                    if (res == order_result::SUCCESS) {
                        log_queue_.enqueue(log_event_t{
                            .type = log_event_type::PRICE_LEVEL_UPDATE,
                            .timestamp = order.timestamp,
                            .order_id = std::string(order.order_id, ORDER_ID_LEN),
                            .price = order.price,
                            .qty = order.qty,
                            .side = static_cast<order_side>(order.side)
                        });
                    }
                    break;

                case order_status::CANCELLED:
                    res = bt->book.cancel(key);
                    if (res == order_result::SUCCESS) {
                        log_queue_.enqueue(log_event_t{
                            .type = log_event_type::CANCEL_ORDER,
                            .timestamp = order.timestamp,
                            .order_id = std::string(order.order_id, ORDER_ID_LEN),
                            .price = order.price,
                            .qty = order.qty,
                            .side = static_cast<order_side>(order.side)
                        });
                    }
                    break;

                case order_status::PARTIALLY_FILLED:
                case order_status::FILLED:
                    res = bt->book.modify(key, order);
                    if (res == order_result::SUCCESS) {
                        log_queue_.enqueue(log_event_t{
                            .type = log_event_type::TRADE_REPORT,
                            .timestamp = order.timestamp,
                            .order_id = std::string(order.order_id, ORDER_ID_LEN),
                            .price = order.price,
                            .qty = order.qty
                        });
                    }
                    break;
            }
        }

        batch.clear();
    }
}

