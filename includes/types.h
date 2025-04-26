#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

constexpr size_t TICKER_LEN = 4;
constexpr size_t ORDER_ID_LEN = 16;

enum class order_kind : uint8_t { LMT=0, MKT=1 };
enum class order_side : uint8_t { BUY=0, SELL=1 };
enum class order_status : uint8_t { NEW=0, PARTIALLY_FILLED=1, FILLED=2, CANCELLED=3 };

// pack the order struct based on operating system
#if defined(_WIN32) || defined(_WIN64)
   #define BEGIN_PACKED __pragma(pack(push, 1))
   #define END_PACKED   __pragma(pack(pop))
   #define PACKED_STRUCT struct
#elif defined(__APPLE__) || defined(__linux__)
   #define BEGIN_PACKED
   #define END_PACKED
   #define PACKED_STRUCT struct __attribute__((packed))
#else
   #error "Platform not supported for packed struct"
#endif

struct order_id_key {
   char order_id[16];

   bool operator==(const order_id_key& other) const {
      return std::memcmp(order_id, other.order_id, 16) == 0;
   }
};

// FNV-1a 64-bit hash
struct order_id_hasher {
   size_t operator()(const order_id_key& key) const {
      constexpr size_t fnv_prime = 1099511628211u;
      size_t hash = 14695981039346656037u;

      for (size_t i = 0; i < 16; i++) {
         hash ^= static_cast<size_t>(key.order_id[ i ]);
         hash *= fnv_prime;
      }
      
      return hash;
   }
};

BEGIN_PACKED
PACKED_STRUCT order_t {
   char order_id[ ORDER_ID_LEN ]; // 16 bytes
   uint64_t timestamp;
   size_t qty;

   char ticker [ TICKER_LEN ]; // 4 bytes
   uint32_t price;
   
   uint8_t kind;
   uint8_t side;
   uint8_t status;

   bool post_only;

   order_t() = default;

   order_t(
      uint64_t _timestamp,
      const char* _order_id,
      const char* _ticker,
      order_kind _kind,
      order_side _side,
      order_status _status,
      uint32_t _price,
      size_t _qty,
      bool _post_only
   ) : 
      timestamp(_timestamp),
      qty(_qty),
      price(_price),
      kind(static_cast<uint8_t>(_kind)),
      side(static_cast<uint8_t>(_side)),
      status(static_cast<uint8_t>(_status)),
      post_only(_post_only)
   {
      std::memcpy(order_id, _order_id, ORDER_ID_LEN);
      std::memcpy(ticker, _ticker, TICKER_LEN);
   }
};
END_PACKED

struct orderbook_config_t {
   bool enable_logging;
   std::string log_filename;
};