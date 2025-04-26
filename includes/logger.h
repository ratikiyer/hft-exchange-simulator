#pragma once

#include <string>
#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "./types.h"
#include "./concurrentqueue.h"

enum class log_event_kind : uint8_t {
   PRICE_LEVEL_UPDATE,
   TRADE_REPORT,
   MODIFY,
   CANCEL
};

struct log_event_t {
   uint64_t timestamp;
   char order_id[ORDER_ID_LEN];
   log_event_kind kind;
   uint32_t price;
   size_t qty;
   order_side side;

   char order_id_secondary[ORDER_ID_LEN];
   uint32_t price_secondary;
   size_t qty_secondary;
   order_side side_secondary;

   log_event_t()
      : timestamp(0)
      , kind(log_event_kind::PRICE_LEVEL_UPDATE)
      , price(0)
      , qty(0)
      , side(order_side::BUY)
      , price_secondary(0)
      , qty_secondary(0)
      , side_secondary(order_side::BUY)
   {
      memset(order_id, 0, ORDER_ID_LEN);
      memset(order_id_secondary, 0, ORDER_ID_LEN);
   }
};

class logger {
public:
   // Constructor that opens a single log file
   explicit logger(const std::string& filename);

   // Destructor that joins thread, closes file
   ~logger();

   // Generic push of a log event
   void push(const log_event_t& event);

   // Specific logging methods that orderbook.cpp calls:
   void log_price_level_update(
      uint64_t ts,
      const char* ord_id,
      uint32_t price,
      size_t qty,
      order_side side
   );

   void log_trade_report(
      uint64_t ts,
      const char* buy_id,
      uint32_t buy_price,
      size_t matched_qty,
      const char* sell_id,
      uint32_t sell_price
   );

   void log_modify_order(
      uint64_t ts,
      const char* old_id,
      uint32_t old_price,
      size_t old_qty,
      order_side old_side,
      const char* new_id,
      uint32_t new_price,
      size_t new_qty,
      order_side new_side
   );

   void log_cancel_order(
      uint64_t ts,
      const char* ord_id,
      uint32_t price,
      size_t qty,
      order_side side
   );

private:
   // The background thread, queue, etc.
   moodycamel::ConcurrentQueue<log_event_t> queue_;
   std::atomic<bool> running_;
   std::thread thread_;

   std::ofstream out_file_;
   std::mutex mutex_;
   std::condition_variable cv_;

   // Worker that consumes the queue
   void run();
   // Convert each event to a line of text/JSON, etc.
   std::string event_to_line(const log_event_t& ev);
};
