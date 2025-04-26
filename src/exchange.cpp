// exchange.cpp
#include "exchange.h"
#include <chrono>
#include <thread>
#include <cstring>

using namespace std::chrono_literals;

Exchange::Exchange(logger* logger_ptr,
                   OrderParser* parser_ptr,
                   MarketDataPublisher* publisher_ptr)
  : logger_(logger_ptr)
  , parser_(parser_ptr)
  , publisher_(publisher_ptr)
  , running_(false)
  , network_(nullptr)
{}

Exchange::~Exchange() {
   stop();
   delete network_;
}

void Exchange::start() {
   running_.store(true);
   publisher_->start();
   if (network_) {
      network_->start();
   }
}

void Exchange::stop() {
   // flip the running flag
   if (!running_.exchange(false)) {
      // already stopped
      return;
   }

   if (network_) {
      network_->stop();
   }
   publisher_->stop();

   // join all book threads
   for (auto & [symbol, bt] : bookThreads_) {
      if (bt.thread.joinable()) {
         bt.thread.join();
      }
   }
}

void Exchange::add_symbol(const char* symbol) {
   std::string sym(symbol, TICKER_LEN);

   // only add once
   auto [it, inserted] = bookThreads_.try_emplace(
      sym,
      // aggregate‐initialize BookThread:
      BookThread{ orderbook(logger_), {}, {} }
   );
   if (!inserted) {
      // already have this symbol
      return;
   }

   // now spawn its processing thread
   BookThread & bt = it->second;
   bt.thread = std::thread(&Exchange::book_loop, this, &bt);
}

void Exchange::on_msg_received(const uint8_t* data, size_t len) {
   ParsedOrder parsed;
   if (!parser_->parse_message(data, len, parsed)) {
      // drop invalid messages
      return;
   }

   // turn into full order_t
   order_t order = parser_->convert_to_order(parsed);
   enqueue_order(order);
}

void Exchange::enqueue_order(const order_t& order) {
   // ticker field is not null‐terminated; copy exact bytes
   std::string sym(order.ticker, TICKER_LEN);

   auto it = bookThreads_.find(sym);
   if (it == bookThreads_.end()) {
      // no book for this symbol: drop
      return;
   }

   it->second.order_queue.enqueue(order);
}

void Exchange::book_loop(BookThread* bt) {
   order_t order;

   while (running_.load()) {
      // try non‐blocking dequeue
      if (bt->order_queue.try_dequeue(order)) {
         order_id_key key;
         std::memcpy(key.order_id, order.order_id, ORDER_ID_LEN);

         order_result res;
         auto status = static_cast<order_status>(order.status);

         switch (status) {
               case order_status::NEW:
                  res = bt->book.add(order);
                  break;

               case order_status::CANCELLED:
                  res = bt->book.cancel(key);
                  break;

               case order_status::PARTIALLY_FILLED:
               case order_status::FILLED:
                  // treat fills/partial fills as a modify
                  res = bt->book.modify(key, order);
                  break;

               default:
                  // unknown status: skip
                  continue;
         }

         // afterwards, run matching
         bt->book.execute();

         // TODO: inspect the book for generated events
         // and call publisher_->publish_*(...) as appropriate
      }
      else {
         // back off briefly if no work
         std::this_thread::sleep_for(1ms);
      }
   }
}
