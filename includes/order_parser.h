// order_parser.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

#include "types.h"

struct ParsedOrder {
    uint64_t timestamp {0}; 
    uint8_t  msg_type  {0}; 
    char     order_id[ORDER_ID_LEN] {0};
    char     ticker  [TICKER_LEN]   {0};
    uint32_t price   {0};
    size_t   qty     {0};
    bool     is_buy  {false};
};

#ifndef ntohll
static inline uint64_t ntohll(uint64_t v) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return (static_cast<uint64_t>(ntohl(static_cast<uint32_t>(v & 0xFFFFFFFF))) << 32) |
           ntohl(static_cast<uint32_t>(v >> 32));
#else
    return v;
#endif
}
#endif

namespace detail {
    constexpr uint8_t TYPE_LIMIT_BUY   = 0x01;
    constexpr uint8_t TYPE_LIMIT_SELL  = 0x02;
    constexpr uint8_t TYPE_MARKET_BUY  = 0x03;
    constexpr uint8_t TYPE_MARKET_SELL = 0x04;
    constexpr uint8_t TYPE_UPDATE      = 0x05;
    constexpr uint8_t TYPE_CANCEL      = 0x06;
}

class OrderParser {
public:
    virtual ~OrderParser() = default; 

    /**
     * Parse raw data into ParsedOrder. Returns false if invalid or truncated.
     */
    virtual bool parse_message(const uint8_t* data, size_t len,
                               ParsedOrder& out);

    virtual order_t convert_to_order(const ParsedOrder& p);
};
