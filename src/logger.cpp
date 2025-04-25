#include "logger.h"
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <sstream>

logger::logger(const std::string& filename) : running_(true) {
    out_file_.open(filename);
    if (!out_file_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + filename);
    }
    thread_ = std::thread(&logger::run, this);
}

logger::~logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
    out_file_.flush();
    out_file_.close();
}

void logger::push(const log_event_t& event) {
    queue_.enqueue(event);
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_one();
}

void logger::log_price_level_update(uint64_t ts,
                                    const char* ord_id,
                                    uint32_t price,
                                    size_t qty,
                                    order_side side) {
    log_event_t ev;
    ev.timestamp = ts;
    ev.kind = log_event_kind::PRICE_LEVEL_UPDATE;
    std::memcpy(ev.order_id, ord_id, ORDER_ID_LEN);
    ev.price = price;
    ev.qty   = qty;
    ev.side  = side;
    push(ev);
}

void logger::log_trade_report(uint64_t ts,
                              const char* buy_id,
                              uint32_t buy_price,
                              size_t matched_qty,
                              const char* sell_id,
                              uint32_t sell_price) {
    log_event_t ev;
    ev.timestamp = ts;
    ev.kind = log_event_kind::TRADE_REPORT;

    std::memcpy(ev.order_id, buy_id, ORDER_ID_LEN);
    ev.price = buy_price;
    ev.qty = matched_qty;
    ev.side = order_side::BUY;

    std::memcpy(ev.order_id_secondary, sell_id, ORDER_ID_LEN);
    ev.price_secondary = sell_price;
    ev.qty_secondary   = matched_qty;
    ev.side_secondary  = order_side::SELL;

    push(ev);
}

void logger::log_modify_order(uint64_t ts,
                              const char* old_id,
                              uint32_t old_price,
                              size_t old_qty,
                              order_side old_side,
                              const char* new_id,
                              uint32_t new_price,
                              size_t new_qty,
                              order_side new_side) {
    log_event_t ev;
    ev.timestamp = ts;
    ev.kind = log_event_kind::MODIFY;

    std::memcpy(ev.order_id, new_id, ORDER_ID_LEN);
    ev.price = new_price;
    ev.qty   = new_qty;
    ev.side  = new_side;

    std::memcpy(ev.order_id_secondary, old_id, ORDER_ID_LEN);
    ev.price_secondary = old_price;
    ev.qty_secondary   = old_qty;
    ev.side_secondary  = old_side;

    push(ev);
}

void logger::log_cancel_order(uint64_t ts,
                                const char* ord_id,
                              uint32_t price,
                              size_t qty,
                              order_side side) {
    log_event_t ev;
    ev.timestamp = ts;
    ev.kind = log_event_kind::CANCEL;
    std::memcpy(ev.order_id, ord_id, ORDER_ID_LEN);
    ev.price = price;
    ev.qty   = qty;
    ev.side  = side;
    push(ev);
}

std::string logger::event_to_line(const log_event_t& ev) {
    std::ostringstream oss;
    oss << "{";

    switch (ev.kind) {
    case log_event_kind::PRICE_LEVEL_UPDATE:
        oss << "\"type\":\"price_level_update\"";
        break;
    case log_event_kind::TRADE_REPORT:
        oss << "\"type\":\"trade_report\"";
        break;
    case log_event_kind::MODIFY:
        oss << "\"type\":\"modify\"";
        break;
    case log_event_kind::CANCEL:
        oss << "\"type\":\"cancel\"";
        break;
    }

    oss << ",\"timestamp\":" << ev.timestamp;
    oss << ",\"order_id\":\"" << std::string(ev.order_id, ORDER_ID_LEN) << "\"";
    oss << ",\"price\":" << ev.price;
    oss << ",\"qty\":" << ev.qty;
    oss << ",\"side\":" << static_cast<int>(ev.side);

    if (ev.kind == log_event_kind::TRADE_REPORT || ev.kind == log_event_kind::MODIFY) {
        oss << ",\"order_id_secondary\":\"" 
            << std::string(ev.order_id_secondary, ORDER_ID_LEN) << "\"";
        oss << ",\"price_secondary\":" << ev.price_secondary;
        oss << ",\"qty_secondary\":" << ev.qty_secondary;
        oss << ",\"side_secondary\":" << static_cast<int>(ev.side_secondary);
    }

    oss << "}";
    return oss.str();
}

void logger::run() {
    while (true) {
        log_event_t ev;
        bool did_work = false;
        while (queue_.try_dequeue(ev)) {
            did_work = true;
            out_file_ << event_to_line(ev) << "\n";
        }
        if (did_work) {
            out_file_.flush();
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_) {
            break;
        }
        cv_.wait_for(lock, std::chrono::milliseconds(100));
    }

    log_event_t ev;
    while (queue_.try_dequeue(ev)) {
        out_file_ << event_to_line(ev) << "\n";
    }
    out_file_.flush();
}
