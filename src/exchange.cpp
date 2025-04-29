// exchange.cpp
#include "exchange.h"
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

using namespace std::chrono_literals;

// ----------------------------------------------------------------
// 1) Define the buckets exactly as on your chart
//    (Not strictly required at runtime, but kept for reference.)
// ----------------------------------------------------------------
static const std::vector<std::string> BUCKETS = {
    "A","B","C","D",
    "EA-E","EF-Z",
    "F","G","H",
    "IA-E","IF-Z",
    "J","K","L","M","N","O",
    "PA-E","PF-Z",
    "Q","R",
    "SA-E","SF-N","SO-Z",
    "T","U","V","W","X","Y","Z"
};

// ----------------------------------------------------------------
// 2) Helper: map a ticker → bucket label
// ----------------------------------------------------------------
static std::string get_bucket(const std::string& sym) {
    if (sym.empty()) return "";
    char c0 = std::toupper(sym[0]);
    char c1 = sym.size()>1 ? std::toupper(sym[1]) : '\0';

    switch (c0) {
      case 'E': return (c1>='A' && c1<='E') ? "EA-E" : "EF-Z";
      case 'I': return (c1>='A' && c1<='E') ? "IA-E" : "IF-Z";
      case 'P': return (c1>='A' && c1<='E') ? "PA-E" : "PF-Z";
      case 'S':
        if      (c1>='A' && c1<='E') return "SA-E";
        else if (c1>='F' && c1<='N') return "SF-N";
        else                          return "SO-Z";
      default:
        if (c0>='A' && c0<='Z')
            return std::string(1, c0);
        return "";
    }
}

// ----------------------------------------------------------------
// 3) Per‐bucket storage of per‐ticker orderbooks
// ----------------------------------------------------------------
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
{
    std::cout << "[DEBUG] Exchange constructed\n";
}

Exchange::~Exchange() {
    stop();
    delete network_;
    std::cout << "[DEBUG] Exchange destructed\n";
}

void Exchange::start() {
    running_.store(true);
    std::cout << "[DEBUG] Exchange::start() – running_=true\n";
    publisher_->start();
    if (network_) network_->start();
}

void Exchange::stop() {
    if (!running_.exchange(false)) return;
    std::cout << "[DEBUG] Exchange::stop() – running_=false\n";
    if (network_) network_->stop();
    publisher_->stop();
    for (auto & [sym, bt] : bookThreads_)
      if (bt.thread.joinable()) bt.thread.join();
}

void Exchange::add_symbol(const char* symbol) {
    std::string sym(symbol, TICKER_LEN);
    std::cout << "[DEBUG] add_symbol: " << sym << "\n";

    auto [it, inserted] = bookThreads_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(sym),
      std::forward_as_tuple(
        orderbook(logger_),
        moodycamel::ConcurrentQueue<order_t>(),
        std::thread()
      )
    );
    if (!inserted) {
      std::cout << "[DEBUG] symbol already exists: " << sym << "\n";
      return;
    }

    auto &bt = it->second;
    bt.thread = std::thread(&Exchange::book_loop, this, &bt);
    std::cout << "[DEBUG] spawned book thread for " << sym << "\n";
}

void Exchange::on_msg_received(const uint8_t* data, size_t len) {
    std::cout << "[DEBUG] on_msg_received(len=" << len << ")\n";
    ParsedOrder parsed;
    if (!parser_->parse_message(data, len, parsed)) {
      std::cout << "[DEBUG] parse_message failed\n";
      return;
    }
    order_t order = parser_->convert_to_order(parsed);
    std::cout << "[DEBUG] parsed order id="
              << std::string(order.order_id, ORDER_ID_LEN)
              << " ticker=" << std::string(order.ticker, TICKER_LEN)
              << " price=" << order.price
              << " qty=" << order.qty << "\n";
    enqueue_order(order);
}

void Exchange::enqueue_order(const order_t& order) {
    std::string sym(order.ticker, TICKER_LEN);
    auto it = bookThreads_.find(sym);
    if (it == bookThreads_.end()) {
      std::cout << "[DEBUG] enqueue_order: no book for " << sym << "\n";
      return;
    }
    it->second.order_queue.enqueue(order);
    std::cout << "[DEBUG] enqueued order to queue for " << sym << "\n";
}

void Exchange::book_loop(BookThread* bt) {
    std::cout << "[DEBUG] book_loop started\n";
    order_t order;
    while (running_.load()) {
        if (bt->order_queue.try_dequeue(order)) {
            std::cout << "[DEBUG] book_loop dequeued order id="
                      << std::string(order.order_id, ORDER_ID_LEN) << "\n";

            order_id_key key;
            std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

            auto status = static_cast<order_status>(order.status);
            order_result res;

            switch (status) {
                case order_status::NEW:
                    res = bt->book.add(order);
                    if (res == order_result::SUCCESS) {
                        logger_->log_price_level_update(
                          order.timestamp,
                          order.order_id,
                          order.price,
                          order.qty,
                          static_cast<order_side>(order.side)
                        );
                        std::cout << "[DEBUG] logged NEW order\n";
                    }
                    break;

                case order_status::CANCELLED:
                    res = bt->book.cancel(key);
                    if (res == order_result::SUCCESS) {
                        logger_->log_cancel_order(
                          order.timestamp,
                          order.order_id,
                          order.price,
                          order.qty,
                          static_cast<order_side>(order.side)
                        );
                        std::cout << "[DEBUG] logged CANCEL order\n";
                    }
                    break;

                case order_status::PARTIALLY_FILLED:
                case order_status::FILLED:
                    res = bt->book.modify(key, order);
                    if (res == order_result::SUCCESS) {
                        logger_->log_trade_report(
                          order.timestamp,
                          order.order_id,
                          order.price,
                          order.qty,
                          order.order_id,
                          order.price
                        );
                        std::cout << "[DEBUG] logged TRADE/MODIFY\n";
                    }
                    break;

                default:
                    std::cout << "[DEBUG] unknown status\n";
                    break;
            }

            bt->book.execute();
        } else {
            std::this_thread::sleep_for(1ms);
        }
    }
    std::cout << "[DEBUG] book_loop exiting\n";
}
