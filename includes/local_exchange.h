// local_exchange.h
#pragma once

#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <vector>

#include "types.h"
#include "orderbook.h"
#include "concurrentqueue.h"
#include "order_parser.h"
// #include "network_server.h"
// #include "market_data_publisher.h"

/**
 * Exchange class now uses one thread per bucket of tickers.
 */
class Exchange {
public:
   Exchange(logger* logger_ptr,
            OrderParser* parser_ptr);
   ~Exchange();

   void start();
   void stop();

   /**
    * Registers a symbol into its bucket.  Spawns the bucket‐thread
    * on first symbol for that bucket.
    */
   void add_symbol(const char* symbol);

   /**
    * Parse incoming raw message and route into the correct bucket queue.
    */
   void on_msg_received(const uint8_t* data, size_t len);

private:
   struct BucketThread {
      std::unordered_map<std::string, orderbook> books;
      moodycamel::ConcurrentQueue<order_t> order_queue;
      std::thread thread;
   };

   void book_loop(BucketThread* bt);
   void enqueue_order(const order_t& order);

   logger* logger_;
   OrderParser* parser_;

   // key = bucket label (e.g. "A", "EA-E", "SF-N", …)
   std::unordered_map<std::string, BucketThread> bucketThreads_;

   std::atomic<bool> running_{false};

   // non-copyable
   Exchange(const Exchange&) = delete;
   Exchange& operator=(const Exchange&) = delete;
};