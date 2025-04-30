#pragma once

#include <unordered_map>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility> // for piecewise_construct
#include "orderbook.h"
#include "logger.h"
#include "types.h"

class Exchange {
private:
   struct BookData {
      BookData() = default;
      BookData(const BookData&) = delete;
      BookData& operator=(const BookData&) = delete;

      std::unique_ptr<orderbook> book;
      std::atomic<bool> running{false};
      std::thread bookThread;
   };

public:
   explicit Exchange(logger* log = nullptr)
      : log_(log)
   {}

   ~Exchange() {
      stop_all();
   }

   void add_order(const order_t& order) {
      std::string symbol = extract_symbol(order);

      {
         std::lock_guard<std::mutex> lock(mutex_);
         auto it = books_.find(symbol);
         if (it == books_.end()) {
            // *** Use piecewise_construct so we don't try to move BookData in a single step
            auto [insertIter, inserted] = books_.emplace(
               std::piecewise_construct,
               std::forward_as_tuple(symbol),
               std::forward_as_tuple()  // default-construct BookData in-place
            );

            if (inserted) {
               auto& data = insertIter->second;
               data.book = std::make_unique<orderbook>(log_);
               data.running.store(true, std::memory_order_relaxed);
               data.bookThread = std::thread(&Exchange::run_book_thread, this, symbol);
            }
         }
      }

      // For demonstration, do synchronous add/execute:
      books_.at(symbol).book->add(order);
      books_.at(symbol).book->execute();
   }

   void stop_all() {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto &kv : books_) {
         kv.second.running.store(false, std::memory_order_relaxed);
      }
      for (auto &kv : books_) {
         if (kv.second.bookThread.joinable()) {
            kv.second.bookThread.join();
         }
      }
   }

   // Debug methods

   // note 'const' -> we must lock a 'mutable' mutex
   size_t bookCount() const {
      std::lock_guard<std::mutex> lock(mutex_); 
      return books_.size();
   }

   bool hasSymbol(const std::string &symbol) const {
      std::lock_guard<std::mutex> lock(mutex_);
      return (books_.find(symbol) != books_.end());
   }

   orderbook& debug_get_orderbook(const std::string& symbol) {
      return *books_.at(symbol).book;
   }

   uint32_t debug_get_best_bid(const std::string& symbol) {
      auto opt = books_.at(symbol).book->best_bid();
      return opt.value_or(0);
   }

   uint32_t debug_get_best_ask(const std::string& symbol) {
      auto opt = books_.at(symbol).book->best_ask();
      return opt.value_or(0);
   }

private:
   void run_book_thread(const std::string &symbol) {
      while (books_.at(symbol).running.load(std::memory_order_relaxed)) {
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
   }

   std::string extract_symbol(const order_t &order) const {
      return std::string(order.ticker, TICKER_LEN);
   }

   mutable std::mutex mutex_;  // <=== 'mutable' so we can lock it in const methods
   std::unordered_map<std::string, BookData> books_;
   logger* log_;
};
