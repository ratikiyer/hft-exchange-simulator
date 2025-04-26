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
#include "network_server.h"
#include "market_data_publisher.h"

/**
 * The Exchange class orchestrates:
 *   - Maintenance of multiple OrderBooks (one per symbol).
 *   - Processing of incoming orders (parsed via OrderParser).
 *   - Publishing of market data via MarketDataPublisher.
 *   - Communication with NetworkServer.
 *
 * It uses a 'thread-per-orderbook' model, where each symbol's orders
 * are processed in a dedicated thread, thus avoiding locks within
 * the orderbook logic.
 */
class Exchange {
public:
   /**
    * Constructor
    * @param logger_ptr: a pointer to an existing logger (for logging).
    * @param parser_ptr: a pointer to an order parser.
    * @param publisher_ptr: a pointer to a market data publisher.
    */
   Exchange(logger* logger_ptr,
         OrderParser* parser_ptr,
         MarketDataPublisher* publisher_ptr);

   /**
    * Destructor - stops all threads and resources cleanly.
    */
   ~Exchange();

   void start();

   void stop();

   /**
    * Creates an orderbook for the given symbol,
    * sets up a dedicated thread and a queue for incoming orders.
    */
   void add_symbol(const char* symbol);

   /**
    * Called by NetworkServer when a raw message arrives.
    * Parses the message using OrderParser.
    * Dispatches the parsed order to the correct symbol's queue, if valid.
    */
   void on_msg_received(const uint8_t* data, size_t len);

private:
    /**
     * A lightweight struct to hold:
     *   - The actual OrderBook for a symbol.
     *   - A concurrent queue of parsed orders waiting to be processed.
     *   - A dedicated thread that pops from the queue and calls orderbook.add/modify/cancel/execute.
     */
    struct BookThread {
        orderbook book;
        moodycamel::ConcurrentQueue<order_t> order_queue;
        std::thread thread;
    };

    /**
     * Thread procedure that continuously pops from bt->orderQueue
     * and processes orders on bt->book.
     * 
     * Also calls book.execute() as needed, and can publish updates.
     */
    void book_loop(BookThread* bt);

    /**
     * Private helper to route an order_t to the correct BookThread queue,
     * based on order_t.ticker.
     */
    void enqueue_order(const order_t& order);

private:
   logger* logger_;
   OrderParser* parser_;
   MarketDataPublisher* publisher_;

   // Map from symbol -> BookThread
   std::unordered_map<std::string, BookThread> bookThreads_;

   // Flag controlling whether threads are running
   std::atomic<bool> running_;

   // For network I/O (see NetworkServer), we hold a pointer or friend
   // but definition is in "NetworkServer.h"
   NetworkServer* network_;

   // non-copyable
   Exchange(const Exchange&) = delete;
   Exchange& operator=(const Exchange&) = delete;
};

