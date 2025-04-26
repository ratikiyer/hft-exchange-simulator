#pragma once

#if __cplusplus < 202002L
   #error "This code requires C++20 or higher. Please compile with -std=c++20."
#endif

#include <cstdint>
#include <cstring>
#include <array>
#include <optional>
#include <atomic>

#include "logger.h"
#include "plf_hive.h"
#include "robin_hood.h"

static constexpr uint32_t MAX_PRICE = 20000;

enum class order_result : uint8_t {
   SUCCESS=0,
   DUPLICATE_ID=10,
   ORDER_NOT_FOUND=20,
   INVALID_SIDE=30,
   INVALID_PRICE=40,
   NO_MATCH=50
};

struct order_location {
   uint32_t price;
   plf::hive<order_t>::iterator location_in_hive;
};

struct price_level {
   plf::hive<order_t> orders;
   size_t total_qty = 0;
};

class orderbook final {
private:
   std::array< price_level, MAX_PRICE + 1 > bids_;
   std::array< price_level, MAX_PRICE + 1 > asks_;

   uint32_t best_bid_price_ = 0;
   uint32_t best_ask_price_ = MAX_PRICE + 1;

   std::unordered_map< order_id_key, order_location, order_id_hasher > order_id_lookup_;

   logger* log_ = nullptr;

public:
   // not default constructable
   orderbook() = delete;

   explicit orderbook(logger* log_instance = nullptr) {
      log_ = log_instance;
   }

   // non-copyable
   orderbook(const orderbook&) = delete;
   orderbook& operator=(const orderbook&) = delete;

   // moveable
   orderbook(orderbook&& other) = default;
   orderbook& operator=(orderbook&& other) noexcept = default;

   ~orderbook() = default;

   /*
      Core functionality
   */
   order_result add(const order_t& order);
   order_result modify(const order_id_key& id, const order_t& new_order);
   order_result cancel(const order_id_key& id);
   void execute();

   std::optional<uint32_t> best_bid() const;
   std::optional<uint32_t> best_ask() const;
   bool contains(const order_id_key& id) const;

private:
   void update_best_bid_on_insert(uint32_t price);
   void update_best_ask_on_insert(uint32_t price);
   void update_best_bid_on_cancel(uint32_t price);
   void update_best_ask_on_cancel(uint32_t price);

   void log_event(const log_event_t& event);
};

