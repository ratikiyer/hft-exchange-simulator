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
    /**
     * Parse raw data into ParsedOrder. Returns false if invalid or truncated.
     */
    bool parse_message(const uint8_t* data, size_t len, ParsedOrder& out) const {
        using namespace detail;
        if (!data || len < 9) return false;  // need ts(8)+type(1)

        size_t off = 0;
        uint64_t raw_ts;
        std::memcpy(&raw_ts, data + off, sizeof(raw_ts)); off += sizeof(raw_ts);
        out.timestamp = ntohll(raw_ts);
        out.msg_type = data[off++];
        if (len < off + ORDER_ID_LEN) return false;
        std::memcpy(out.order_id, data + off, ORDER_ID_LEN);
        off += ORDER_ID_LEN;

        if (len < off + TICKER_LEN) return false;
        std::memset(out.ticker, 0, TICKER_LEN);
        std::memcpy(out.ticker, data + off, TICKER_LEN);
        off += TICKER_LEN;

        out.price  = 0;
        out.qty    = 0;
        out.is_buy = false;

        switch (out.msg_type) {
            case TYPE_LIMIT_BUY:
            case TYPE_LIMIT_SELL:
            case TYPE_MARKET_BUY:
            case TYPE_MARKET_SELL: {

                if (len < off + 8) return false;
                uint32_t raw_px, raw_qty;
                std::memcpy(&raw_px,  data + off, 4); off += 4;
                std::memcpy(&raw_qty, data + off, 4); off += 4;
                out.price  = ntohl(raw_px);
                out.qty    = ntohl(raw_qty);
                out.is_buy = (out.msg_type == TYPE_LIMIT_BUY || out.msg_type == TYPE_MARKET_BUY);
                break;
            }
            case TYPE_UPDATE: {

                if (len < off + 9) return false;
                uint32_t raw_px, raw_qty;
                std::memcpy(&raw_px,  data + off, 4); off += 4;
                std::memcpy(&raw_qty, data + off, 4); off += 4;
                out.price    = ntohl(raw_px);
                out.qty      = ntohl(raw_qty);
                out.is_buy   = (data[off] == 'B');
                break;
            }
            case TYPE_CANCEL:
                break;
            default:
                return false;
        }

        if (out.msg_type != TYPE_CANCEL && (out.price == 0 || out.qty == 0))
            return false;
        return true;
    }

    order_t convert_to_order(const ParsedOrder& p) const {
        // Convert to Order Here
    }
};
