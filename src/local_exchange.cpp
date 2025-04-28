// local_exchange.cpp
#include "local_exchange.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <utility>
#include <algorithm>
#include <cctype>

using namespace std::chrono_literals;

// ----------------------------------------------------------------
// 1) Define the buckets exactly as on your chart
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

Exchange::Exchange(logger* logger_ptr,
                   OrderParser* parser_ptr)
  : logger_(logger_ptr)
  , parser_(parser_ptr)
{
    std::cout << "[DEBUG] Exchange constructed\n";
}

Exchange::~Exchange() {
    stop();
    std::cout << "[DEBUG] Exchange destructed\n";
}

void Exchange::start() {
    running_.store(true);
    std::cout << "[DEBUG] Exchange::start() – running_=true\n";
}

void Exchange::stop() {
    if (!running_.exchange(false)) return;
    std::cout << "[DEBUG] Exchange::stop() – running_=false\n";
    for (auto & [bucket, bt] : bucketThreads_) {
      if (bt.thread.joinable()) bt.thread.join();
    }
}

void Exchange::add_symbol(const char* symbol) {
    std::string sym(symbol, TICKER_LEN);
    std::string bucket = get_bucket(sym);
    if (bucket.empty()) {
      std::cout << "[DEBUG] add_symbol: invalid bucket for " << sym << "\n";
      return;
    }

    auto [it, inserted] = bucketThreads_.emplace(
      bucket,
      BucketThread{}
    );

    // On first symbol for this bucket, spawn its thread
    if (inserted) {
      std::cout << "[DEBUG] creating bucket thread for " << bucket << "\n";
      it->second.thread = std::thread(&Exchange::book_loop, this, &it->second);
    }

    // register the symbol's orderbook in that bucket
    auto &bt = it->second;
    auto [bookIt, added] =
      bt.books.emplace(sym, orderbook(logger_));
    if (added) {
      std::cout << "[DEBUG] added symbol " << sym
                << " into bucket " << bucket << "\n";
    } else {
      std::cout << "[DEBUG] symbol " << sym
                << " already in bucket " << bucket << "\n";
    }
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
    const std::string sym(order.ticker, TICKER_LEN);
    const std::string bucket = get_bucket(sym);
    if (bucket.empty()) {
        std::cout << "[DEBUG] enqueue_order: invalid bucket for " << sym << "\n";
        return;
    }

    // ── 1) Lazy‐create the bucket/thread if needed ───────────────────────────────
    auto [bt_it, was_bucket_inserted] =
        bucketThreads_.emplace(bucket, BucketThread{});
    BucketThread &bt = bt_it->second;

    if (was_bucket_inserted) {
        std::cout << "[DEBUG] auto‐creating bucket thread for " << bucket << "\n";
        bt.thread = std::thread(&Exchange::book_loop, this, &bt);
    }

    // ── 2) Lazy‐create the symbol’s orderbook if needed ────────────────────────
    auto [book_it, was_book_inserted] =
        bt.books.emplace(sym, orderbook(logger_));
    if (was_book_inserted) {
        std::cout << "[DEBUG] auto‐adding book for symbol " << sym
                  << " into bucket " << bucket << "\n";
    }

    // ── 3) Finally enqueue the order ──────────────────────────────────────────
    bt.order_queue.enqueue(order);
    std::cout << "[DEBUG] enqueued order for " << sym
              << " into bucket " << bucket << "\n";
}

void Exchange::book_loop(BucketThread* bt) {
    std::cout << "[DEBUG] bucket thread started\n";
    order_t order;
    while (running_.load()) {
        if (bt->order_queue.try_dequeue(order)) {
            std::string sym(order.ticker, TICKER_LEN);
            auto bookIt = bt->books.find(sym);
            if (bookIt == bt->books.end()) {
                std::cout << "[DEBUG] no orderbook for " << sym << "\n";
                continue;
            }

            std::cout << "[DEBUG] dequeued order for " << sym << "\n";
            order_id_key key;
            std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

            auto status = static_cast<order_status>(order.status);
            order_result res;

            switch (status) {
                case order_status::NEW:
                    res = bookIt->second.add(order);
                    if (res == order_result::SUCCESS) {
                        // logger_->log_price_level_update(
                        //   order.timestamp,
                        //   order.order_id,
                        //   order.price,
                        //   order.qty,
                        //   static_cast<order_side>(order.side)
                        // );
                    }
                    break;

                case order_status::CANCELLED:
                    res = bookIt->second.cancel(key);
                    if (res == order_result::SUCCESS) {
                        // logger_->log_cancel_order(
                        //   order.timestamp,
                        //   order.order_id,
                        //   order.price,
                        //   order.qty,
                        //   static_cast<order_side>(order.side)
                        // );
                    }
                    break;

                case order_status::PARTIALLY_FILLED:
                case order_status::FILLED:
                    res = bookIt->second.modify(key, order);
                    if (res == order_result::SUCCESS) {
                        // logger_->log_trade_report(
                        //   order.timestamp,
                        //   order.order_id,
                        //   order.price,
                        //   order.qty,
                        //   order.order_id,
                        //   order.price
                        // );
                    }
                    break;

                default:
                    std::cout << "[DEBUG] unknown status\n";
                    break;
            }

            // execute any pending matches in this bucket
            bookIt->second.execute();
        }
        else {
            std::this_thread::sleep_for(1ms);
        }
    }
    std::cout << "[DEBUG] bucket thread exiting\n";
}