#include "local_exchange.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <utility>
#include <algorithm>
#include <cctype>

using namespace std::chrono_literals;

static constexpr bool ENABLE_DEBUG = false;
#define DBG(x) do { if (ENABLE_DEBUG) std::cout << "[DEBUG] " << x << std::endl; } while(0)

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
    DBG("Exchange constructed");
}

Exchange::~Exchange() {
    stop();
    DBG("Exchange destructed");
}

void Exchange::start() {
    running_.store(true);
    DBG("Exchange::start() – running_=true");
}

void Exchange::stop() {
    if (!running_.exchange(false)) return;
    DBG("Exchange::stop() – running_=false");
    for (auto & [bucket, bt] : bucketThreads_) {
      if (bt.thread.joinable()) bt.thread.join();
    }
}

void Exchange::add_symbol(const char* symbol) {
    std::string sym(symbol, TICKER_LEN);
    std::string bucket = get_bucket(sym);
    if (bucket.empty()) {
      DBG("add_symbol: invalid bucket for " << sym);
      return;
    }

    auto [it, inserted] = bucketThreads_.emplace(
      bucket,
      BucketThread{}
    );

    if (inserted) {
      DBG("creating bucket thread for " << bucket);
      it->second.thread = std::thread(&Exchange::book_loop, this, &it->second);
    }

    auto &bt = it->second;
    auto [bookIt, added] =
      bt.books.emplace(sym, orderbook(logger_));
    if (added) {
      DBG("added symbol " << sym << " into bucket " << bucket);
    } else {
      DBG("symbol " << sym << " already in bucket " << bucket);
    }
}

void Exchange::on_msg_received(const uint8_t* data, size_t len) {
    DBG("on_msg_received(len=" << len << ")");
    ParsedOrder parsed;
    if (!parser_->parse_message(data, len, parsed)) {
      DBG("parse_message failed");
      return;
    }
    order_t order = parser_->convert_to_order(parsed);
    DBG("parsed order id=" << std::string(order.order_id, ORDER_ID_LEN)
        << " ticker=" << std::string(order.ticker, TICKER_LEN)
        << " price=" << order.price
        << " qty=" << order.qty);
    enqueue_order(order);
}

void Exchange::enqueue_order(const order_t& order) {
    const std::string sym(order.ticker, TICKER_LEN);
    const std::string bucket = get_bucket(sym);
    if (bucket.empty()) {
        DBG("enqueue_order: invalid bucket for " << sym);
        return;
    }

    auto [bt_it, was_bucket_inserted] =
        bucketThreads_.emplace(bucket, BucketThread{});
    BucketThread &bt = bt_it->second;

    if (was_bucket_inserted) {
        DBG("auto‑creating bucket thread for " << bucket);
        bt.thread = std::thread(&Exchange::book_loop, this, &bt);
    }

    auto [book_it, was_book_inserted] =
        bt.books.emplace(sym, orderbook(logger_));
    if (was_book_inserted) {
        DBG("auto‑adding book for symbol " << sym << " into bucket " << bucket);
    }

    bt.order_queue.enqueue(order);
    DBG("enqueued order for " << sym << " into bucket " << bucket);
}

void Exchange::book_loop(BucketThread* bt) {
    DBG("bucket thread started");
    order_t order;
    while (running_.load()) {
        if (bt->order_queue.try_dequeue(order)) {
            std::string sym(order.ticker, TICKER_LEN);
            auto bookIt = bt->books.find(sym);
            if (bookIt == bt->books.end()) {
                DBG("no orderbook for " << sym);
                continue;
            }

            DBG("dequeued order for " << sym);
            order_id_key key;
            std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

            auto status = static_cast<order_status>(order.status);
            order_result res;

            switch (status) {
                case order_status::NEW:
                    res = bookIt->second.add(order);
                    break;
                case order_status::CANCELLED:
                    res = bookIt->second.cancel(key);
                    break;
                case order_status::PARTIALLY_FILLED:
                case order_status::FILLED:
                    res = bookIt->second.modify(key, order);
                    break;
                default:
                    DBG("unknown status");
                    break;
            }

            bookIt->second.execute();
        }
        else {
            std::this_thread::sleep_for(1ms);
        }
    }
    DBG("bucket thread exiting");
}
