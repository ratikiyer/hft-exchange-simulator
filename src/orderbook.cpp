#include "orderbook.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <sys/types.h>
#include <chrono>
#include <algorithm>
#include <map>

static inline uint64_t get_current_time_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

bool orderbook::contains(const order_id_key& id) const {
    return order_id_lookup_.contains(id);
}

void orderbook::log_event(const log_event_t& event) {
    if (log_) log_->push(event);
}

std::optional<uint32_t> orderbook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

std::optional<uint32_t> orderbook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

order_result orderbook::add(const order_t& order) {
    order_id_key key;
    std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);
    if (order_id_lookup_.contains(key)) return order_result::DUPLICATE_ID;
    order_side side = static_cast<order_side>(order.side);
    if (side != order_side::BUY && side != order_side::SELL) return order_result::INVALID_SIDE;
    if (order.price > MAX_PRICE) return order_result::INVALID_PRICE;

    auto& container = (side == order_side::BUY ? bids_ : asks_);
    price_level& level = container[order.price];  // constructs on-demand
    auto it = level.orders.insert(order);
    level.total_qty += order.qty;

    order_location loc{order.price, it};
    order_id_lookup_[key] = loc;

    if (log_) {
        log_->log_price_level_update(
            order.timestamp,
            order.order_id,
            order.price,
            order.qty,
            side
        );
    }
    return order_result::SUCCESS;
}

order_result orderbook::modify(const order_id_key& id, const order_t& new_order) {
    auto it_lookup = order_id_lookup_.find(id);
    if (it_lookup == order_id_lookup_.end()) return order_result::ORDER_NOT_FOUND;

    order_location& loc = it_lookup->second;
    const order_t& old_order = *loc.location_in_hive;
    order_side new_side = static_cast<order_side>(new_order.side);
    if (new_side != order_side::BUY && new_side != order_side::SELL) return order_result::INVALID_SIDE;
    if (new_order.price > MAX_PRICE) return order_result::INVALID_PRICE;

    // Remove from old level
    auto& old_container = (static_cast<order_side>(old_order.side) == order_side::BUY ? bids_ : asks_);
    auto old_it = old_container.find(old_order.price);
    price_level& old_level = old_it->second;
    old_level.total_qty -= old_order.qty;
    old_level.orders.erase(loc.location_in_hive);
    if (old_level.orders.empty()) {
        old_container.erase(old_it);
    }

    // Insert into new level
    auto& new_container = (new_side == order_side::BUY ? bids_ : asks_);
    price_level& new_level = new_container[new_order.price];
    auto new_it = new_level.orders.insert(new_order);
    new_level.total_qty += new_order.qty;

    loc = {new_order.price, new_it};

    if (log_) {
        log_->log_modify_order(
            new_order.timestamp,
            old_order.order_id,
            old_order.price,
            old_order.qty,
            static_cast<order_side>(old_order.side),
            new_order.order_id,
            new_order.price,
            new_order.qty,
            new_side
        );
    }
    return order_result::SUCCESS;
}

order_result orderbook::cancel(const order_id_key& id) {
    auto it_lookup = order_id_lookup_.find(id);
    if (it_lookup == order_id_lookup_.end()) return order_result::ORDER_NOT_FOUND;

    order_location loc = it_lookup->second;
    auto& container = (static_cast<order_side>(loc.location_in_hive->side) == order_side::BUY ? bids_ : asks_);
    auto map_it = container.find(loc.price);
    price_level& level = map_it->second;
    const order_t& stored_order = *loc.location_in_hive;

    level.total_qty -= stored_order.qty;
    level.orders.erase(loc.location_in_hive);
    if (level.orders.empty()) {
        container.erase(map_it);
    }
    order_id_lookup_.erase(it_lookup);

    if (log_) {
        log_->log_cancel_order(
            stored_order.timestamp,
            stored_order.order_id,
            stored_order.price,
            stored_order.qty,
            static_cast<order_side>(stored_order.side)
        );
    }
    return order_result::SUCCESS;
}

void orderbook::execute() {
    const uint64_t match_ts = get_current_time_ns();
    while (true) {
        auto bid_it = bids_.empty() ? bids_.end() : std::prev(bids_.end());
        auto ask_it = asks_.empty() ? asks_.end() : asks_.begin();
        if (bid_it == bids_.end() || ask_it == asks_.end() || bid_it->first < ask_it->first) break;

        price_level& bid_level = bid_it->second;
        price_level& ask_level = ask_it->second;
        auto b_it = bid_level.orders.begin();
        auto a_it = ask_level.orders.begin();
        if (b_it == bid_level.orders.end() || a_it == ask_level.orders.end()) break;

        order_t& buy = *b_it;
        order_t& sell = *a_it;
        size_t m = std::min(buy.qty, sell.qty);
        buy.qty -= m;
        sell.qty -= m;
        bid_level.total_qty -= m;
        ask_level.total_qty -= m;

        if (log_) {
            log_->log_trade_report(
                match_ts,
                buy.order_id,
                bid_it->first,
                m,
                sell.order_id,
                ask_it->first
            );
        }

        if (buy.qty == 0) {
            order_id_key bk; std::memcpy(bk.order_id, buy.order_id, ORDER_ID_LEN);
            bid_level.orders.erase(b_it);
            order_id_lookup_.erase(bk);
        }
        if (sell.qty == 0) {
            order_id_key sk; std::memcpy(sk.order_id, sell.order_id, ORDER_ID_LEN);
            ask_level.orders.erase(a_it);
            order_id_lookup_.erase(sk);
        }
        if (bid_level.orders.empty()) bids_.erase(bid_it);
        if (ask_level.orders.empty()) asks_.erase(ask_it);
    }
}
