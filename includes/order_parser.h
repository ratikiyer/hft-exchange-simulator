#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "types.h"

struct ParsedOrder {
   uint64_t timestamp;
   char order_id[ORDER_ID_LEN];
   char ticker[TICKER_LEN];
   uint32_t price;
   size_t qty;
   bool is_buy;
};

/**
 * Interprets raw incoming data (e.g. from the network) as an order.
 * Could parse JSON, binary (Protobuf, FlatBuffers), or a custom format.
 * Validates fields (e.g., price >= 0, qty > 0).
 */
class OrderParser {
public:
   OrderParser() = default;
   ~OrderParser() = default;

   /**
    * Attempts to parse 'len' bytes from 'data' into an order.
    * Returns true if parsing/validation succeeded, false otherwise.
    * This is a simple, synchronous parse. For performance, you might
    * use a specialized binary format (Protobuf/FlatBuffers).
    */
   bool parse_message(const uint8_t* data, size_t len, ParsedOrder& parsed_order);

   /**
    * Transforms ParsedOrder into final 'order_t' struct
    */
   order_t convert_to_order(const ParsedOrder& parsed);
};

