#include "orderbook.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <sys/types.h>
#include <chrono>
#include <algorithm>

static inline uint64_t get_current_time_ns() {
   using namespace std::chrono;
   return static_cast<uint64_t>(
      duration_cast<nanoseconds>(
         steady_clock::now().time_since_epoch()
      ).count()
   );
}

bool orderbook::contains(const order_id_key& id) const {
   return (order_id_lookup_.find(id) != order_id_lookup_.end());
}

void orderbook::log_event(const log_event_t& event) {
   if (log_) {
      log_->push(event);
   }
}

std::optional<uint32_t> orderbook::best_bid() const {
   if (best_bid_price_ == 0 && bids_[0].orders.empty()) {
      return std::nullopt;
   } else {
      return std::optional<uint32_t>(best_bid_price_);
   }
}

std::optional<uint32_t> orderbook::best_ask() const {
   if (best_ask_price_ > MAX_PRICE || asks_[best_ask_price_].orders.empty()) {
      return std::nullopt;
   } else {
      return best_ask_price_;
   }
}

order_result orderbook::add(const order_t& order) {
   order_id_key key;
   std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

   if (order_id_lookup_.contains(key)) {
      return order_result::DUPLICATE_ID;
   }
   
   if (order.side != static_cast<uint8_t>(order_side::BUY) && order.side != static_cast<uint8_t>(order_side::SELL)) {
      return order_result::INVALID_SIDE;
   }

   order_side side = static_cast<order_side>(order.side);

   if (order.price > MAX_PRICE) {
      return order_result::INVALID_PRICE;
   }

   price_level& level = (side == order_side::BUY ? bids_[order.price] : asks_[order.price]);

   auto it = level.orders.insert(order);
   level.total_qty += order.qty;

   order_location loc;
   loc.price = order.price;
   loc.location_in_hive = it;
   order_id_lookup_[key] = loc;

   if (side == order_side::BUY) {
      update_best_bid_on_insert(order.price);
   } else {
      update_best_ask_on_insert(order.price);
   }

   if (log_) {
      log_->log_price_level_update(
         order.timestamp,
         order.order_id,
         order.price,
         order.qty,
         static_cast<order_side>(order.side)
      );
   }

   return order_result::SUCCESS;
}

order_result orderbook::modify(const order_id_key& id, const order_t& new_order) {
   auto it_lookup = order_id_lookup_.find(id);
   if (it_lookup == order_id_lookup_.end()) {
      return order_result::ORDER_NOT_FOUND;
   }

   order_location& loc = it_lookup->second;
   const order_t& old_order = *(loc.location_in_hive);

   if (new_order.price > MAX_PRICE) {
      return order_result::INVALID_PRICE;
   }
   order_side new_side = static_cast<order_side>(new_order.side);
   if (new_side != order_side::BUY && new_side != order_side::SELL) {
      return order_result::INVALID_SIDE;
   }

   if ((old_order.price != new_order.price) || (old_order.side != new_order.side)) {
      price_level& old_level =
         (static_cast<order_side>(old_order.side) == order_side::BUY
            ? bids_[old_order.price]
            : asks_[old_order.price]);
      old_level.total_qty -= old_order.qty;
      old_level.orders.erase(loc.location_in_hive);

      if (old_level.orders.empty()) {
         old_level.total_qty = 0;
         if (old_order.side == static_cast<uint8_t>(order_side::BUY)) {
            update_best_bid_on_cancel(old_order.price);
         } else {
            update_best_ask_on_cancel(old_order.price);
         }
      }

      price_level& new_level =
         (new_side == order_side::BUY ? bids_[new_order.price] : asks_[new_order.price]);
      plf::hive<order_t>::iterator new_it = new_level.orders.insert(new_order);
      new_level.total_qty += new_order.qty;

      if (new_side == order_side::BUY) {
         update_best_bid_on_insert(new_order.price);
      } else {
         update_best_ask_on_insert(new_order.price);
      }

      loc.price = new_order.price;
      loc.location_in_hive = new_it;
   } else {
      price_level& level =
         (static_cast<order_side>(old_order.side) == order_side::BUY
            ? bids_[old_order.price]
            : asks_[old_order.price]);

      level.total_qty -= old_order.qty;
      level.orders.erase(loc.location_in_hive);

      auto new_it = level.orders.insert(new_order);
      level.total_qty += new_order.qty;

      loc.location_in_hive = new_it;
   }

   // In order_result orderbook::modify(...)
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
         static_cast<order_side>(new_order.side)
      );
   }


   return order_result::SUCCESS;
}

order_result orderbook::cancel(const order_id_key& id) {
   auto it_lookup = order_id_lookup_.find(id);
   if (it_lookup == order_id_lookup_.end()) {
      return order_result::ORDER_NOT_FOUND;
   }

   order_location& loc = it_lookup->second;
   const order_t& stored_order = *(loc.location_in_hive);
   order_side side = static_cast<order_side>(stored_order.side);

   if (loc.price > MAX_PRICE) {
      return order_result::INVALID_PRICE;
   }

   price_level& level = (side == order_side::BUY ? bids_[loc.price] : asks_[loc.price]);
   level.total_qty -= stored_order.qty;
   level.orders.erase(loc.location_in_hive);

   if (level.orders.empty()) {
      level.total_qty = 0;
      if (side == order_side::BUY) {
         update_best_bid_on_cancel(loc.price);
      } else {
         update_best_ask_on_cancel(loc.price);
      }
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
   const uint64_t match_timestamp = get_current_time_ns();
   
   while (best_bid_price_ >= best_ask_price_) {
      price_level& bid_level = bids_[best_bid_price_];
      price_level& ask_level = asks_[best_ask_price_];
      
      if (bid_level.orders.empty() || ask_level.orders.empty()) {
         break;
      }

      plf::hive<order_t>::iterator bid_it = bid_level.orders.begin();
      plf::hive<order_t>::iterator ask_it = ask_level.orders.begin();

      if (bid_it == bid_level.orders.end() || ask_it == ask_level.orders.end()) {
         break;
      }

      order_t& bid_order = *bid_it;
      order_t& ask_order = *ask_it;

      const size_t match_qty = std::min(bid_order.qty, ask_order.qty);

      bid_order.qty -= match_qty;
      ask_order.qty -= match_qty;
      bid_level.total_qty -= match_qty;
      ask_level.total_qty -= match_qty;

      if (log_) {
         log_->log_trade_report(
            match_timestamp,
            bid_order.order_id,
            best_bid_price_,
            match_qty,
            ask_order.order_id,
            best_ask_price_
         );
      }

      const bool bid_fully_filled = (bid_order.qty == 0);
      const bool ask_fully_filled = (ask_order.qty == 0);
      
      if (bid_fully_filled) {
         order_id_key bid_key;
         std::memcpy(bid_key.order_id, bid_order.order_id, ORDER_ID_LEN);
         bid_level.orders.erase(bid_it);
         order_id_lookup_.erase(bid_key);
      }

      if (ask_fully_filled) {
         order_id_key ask_key;
         std::memcpy(ask_key.order_id, ask_order.order_id, ORDER_ID_LEN);
         ask_level.orders.erase(ask_it);
         order_id_lookup_.erase(ask_key);
      }

      const bool bid_level_now_empty = bid_level.orders.empty();
      const bool ask_level_now_empty = ask_level.orders.empty();
      
      if (bid_level_now_empty) {
         bid_level.total_qty = 0;
         update_best_bid_on_cancel(best_bid_price_);
      }
      
      if (ask_level_now_empty) {
         ask_level.total_qty = 0;
         update_best_ask_on_cancel(best_ask_price_);
      }
      
      if (bid_level_now_empty && ask_level_now_empty) {
         break;
      }
   }
}

inline void orderbook::update_best_bid_on_insert(uint32_t price) {
   if (price > best_bid_price_) {
      best_bid_price_ = price;
   }
}

inline void orderbook::update_best_ask_on_insert(uint32_t price) {
   if (price < best_ask_price_) {
      best_ask_price_ = price;
   }
}

inline void orderbook::update_best_bid_on_cancel(uint32_t price) {
   if (price == best_bid_price_) {
      while (best_bid_price_ > 0 && bids_[best_bid_price_].orders.empty()) {
         best_bid_price_--;
      }
      if (bids_[best_bid_price_].orders.empty()) {
         best_bid_price_ = 0;
      }
   }
}

inline void orderbook::update_best_ask_on_cancel(uint32_t price) {
   if (price == best_ask_price_) {
      while (best_ask_price_ <= MAX_PRICE &&
             asks_[best_ask_price_].orders.empty()) {
         best_ask_price_++;
      }
      if (best_ask_price_ > MAX_PRICE) {
         best_ask_price_ = MAX_PRICE + 1;
      }
   }
}
