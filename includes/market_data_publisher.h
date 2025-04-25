#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <variant>
#include <boost/asio.hpp>
#include "concurrentqueue.h"

#include "types.h"

struct PriceLevelUpdateMD {
   uint64_t    timestamp;
   char        order_id[ORDER_ID_LEN];
   uint32_t    price;
   size_t      qty;
   order_side  side;   
};

struct TradeReportMD {
   uint64_t    timestamp;

   char        order_id[ORDER_ID_LEN];
   uint32_t    price;
   size_t      qty;
   order_side  side;

   char        order_id_secondary[ORDER_ID_LEN]; 
   uint32_t    price_secondary;
   size_t      qty_secondary;
   order_side  side_secondary;
};

struct ModifyMD {
   uint64_t    timestamp;

   char        order_id[ORDER_ID_LEN];
   uint32_t    price;
   size_t      qty;
   order_side  side;

   char        order_id_secondary[ORDER_ID_LEN];
   uint32_t    price_secondary;
   size_t      qty_secondary;
   order_side  side_secondary;
};

struct CancelMD {
   uint64_t    timestamp;
   char        order_id[ORDER_ID_LEN];
   uint32_t    price;
   size_t      qty;
   order_side  side;
};

enum class MarketInfoType : uint8_t {
   PRICE_LEVEL_UPDATE,
   TRADE_REPORT,
   MODIFY,
   CANCEL
};

struct MarketDataEvent {
   MarketInfoType type;
   std::variant<PriceLevelUpdateMD, TradeReportMD, ModifyMD, CancelMD> data;
};

class MarketDataPublisher {
// member variables
private:
   // ref to a Boost.Asio context
   boost::asio::io_context& io_context_;

   // UDP socket for sending updates
   boost::asio::ip::udp::socket socket_;

   // multicast endpoint: IP + port we send to
   boost::asio::ip::udp::endpoint multicast_endpoint_;

   // options for TTL, loopback, etc.
   uint8_t multicast_TTL_{1};
   bool loopback_enabled_{true};

   // lock-free queue of all event variants
   moodycamel::ConcurrentQueue<MarketDataEvent> updateQueue_;

   // runs 'run()' function
   std::thread thread_;

   // current state
   std::atomic<bool> running_{false};

public:
   
   MarketDataPublisher(boost::asio::io_context& ctx, const std::string& multicast_ip, unsigned short port);

   ~MarketDataPublisher();

   void start();

   void stop();

   void publish_price_level_update(const PriceLevelUpdateMD& plu);
   void publish_trade_report(const TradeReportMD& tr);
   void publish_modify_event(const ModifyMD& me);
   void publish_cancel_event(const CancelMD& ce);

   /**
    * Sets the time-to-live for multicast packets (router hops).
    * Must be called before start().
    */
   void set_multicast_TTL(uint8_t ttl);

   /**
    * setLoopback(bool enable)
    * Enables/disables loopback of multicast packets on the local machine.
    * Must be called before start().
    */
   void set_loopback(bool enable);

private:

   /**
    * Background thread method:
    *     1) Blocks on the update_queue_ for new MarketDataEvent messages.
    *     2) Serializes them.
    *     3) Sends them via the UDP socket to the multicast endpoint.
    */
   void run();

   // serialize into text-based or binary-based buffer (TBD)
   std::vector<uint8_t> serializePriceLevelUpdate(const PriceLevelUpdateMD& e);
   std::vector<uint8_t> serializeTradeReport(const TradeReportMD& e);
   std::vector<uint8_t> serializeModify(const ModifyMD& e);
   std::vector<uint8_t> serializeCancel(const CancelMD& e);

   // non-copyable
   MarketDataPublisher(const MarketDataPublisher&) = delete;
   MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;
};

