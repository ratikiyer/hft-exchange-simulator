#include "order_parser.h"

using namespace detail;

bool OrderParser::parse_message(const uint8_t* data, size_t len,
                                ParsedOrder& out) {
    // ── 1) Quick size check ────────────────────────────────────────────────
    constexpr size_t BASE_LEN = 9 + ORDER_ID_LEN + TICKER_LEN;
    if (!data || len < BASE_LEN) return false;

    // ── 2) Timestamp & type ───────────────────────────────────────────────
    uint64_t raw_ts;
    std::memcpy(&raw_ts, data, sizeof raw_ts);
    out.timestamp = ntohll(raw_ts);
    out.msg_type  = data[8];

    // ── 3) Order-id & ticker ──────────────────────────────────────────────
    const uint8_t* p = data + 9;
    std::memcpy(out.order_id, p, ORDER_ID_LEN);
    p += ORDER_ID_LEN;
    std::memcpy(out.ticker,   p, TICKER_LEN);
    p += TICKER_LEN;

    // ── 4) Defaults for optional fields ──────────────────────────────────
    out.price  = 0;
    out.qty    = 0;
    out.is_buy = false;

    // ── 5) Type-specific parsing ──────────────────────────────────────────
    switch (out.msg_type) {
        case TYPE_LIMIT_BUY:
        case TYPE_LIMIT_SELL:
        case TYPE_MARKET_BUY:
        case TYPE_MARKET_SELL: {
            if (len < (p - data) + 8) return false;
            uint32_t raw_price, raw_qty;
            std::memcpy(&raw_price, p,     4);
            std::memcpy(&raw_qty,   p + 4, 4);
            out.price  = ntohl(raw_price);
            out.qty    = ntohl(raw_qty);
            out.is_buy = (out.msg_type == TYPE_LIMIT_BUY ||
                          out.msg_type == TYPE_MARKET_BUY);
            break;
        }

        case TYPE_UPDATE: {
            if (len < (p - data) + 9) return false;
            uint32_t raw_price, raw_qty;
            std::memcpy(&raw_price, p,     4);
            std::memcpy(&raw_qty,   p + 4, 4);
            out.price  = ntohl(raw_price);
            out.qty    = ntohl(raw_qty);
            out.is_buy = (p[8] == 'B');  // side byte
            break;
        }

        case TYPE_CANCEL:
            // nothing beyond order_id/ticker
            break;

        default:
            return false; // Unknown type
    }

    // ── 6) Sanity check for priced orders ─────────────────────────────────
    if (out.msg_type != TYPE_CANCEL && (out.price == 0 || out.qty == 0))
        return false;

    return true;
}

order_t OrderParser::convert_to_order(const ParsedOrder& p) {
    order_kind   kind   = order_kind::LMT;
    order_status status = order_status::NEW;
    order_side   side   = p.is_buy ? order_side::BUY
                                   : order_side::SELL;

    switch (p.msg_type) {
        case TYPE_LIMIT_BUY:
        case TYPE_LIMIT_SELL:
            kind   = order_kind::LMT;
            status = order_status::NEW;
            break;
        case TYPE_MARKET_BUY:
        case TYPE_MARKET_SELL:
            kind   = order_kind::MKT;
            status = order_status::NEW;
            break;
        case TYPE_UPDATE:
            status = order_status::PARTIALLY_FILLED;
            break;
        case TYPE_CANCEL:
            status = order_status::CANCELLED;
            break;
        default:
            status = order_status::NEW;
            break;
    }

    return order_t(
        p.timestamp,
        p.order_id,
        p.ticker,
        kind,
        side,
        status,
        p.price,
        p.qty,
        false
    );
}
