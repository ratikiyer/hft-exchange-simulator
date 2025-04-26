// main.cpp
#include <iostream>
#include <thread>
#ifdef __APPLE__
  #include <libkern/OSByteOrder.h>
  #define htobe64(x) OSSwapHostToBigInt64(x)
#else
  #include <endian.h>
#endif

#include <boost/asio.hpp>
#include "exchange.h"
#include "logger.h"
#include "order_parser.h"
#include "market_data_publisher.h"
#include "network_server.h"
#include "types.h"

int main() {
    boost::asio::io_context io_ctx;
    logger log("exchange.log");
    OrderParser parser;
    MarketDataPublisher publisher(io_ctx, "239.255.0.1", 15000);
    Exchange ex(&log, &parser, &publisher);

    ex.start();                     // start threads
    ex.add_symbol("ABCD");
    ex.add_symbol("WXYZ");

    NetworkServer server(io_ctx, &ex, 12345);
    server.start();

    std::cout << "Exchange listening on 0.0.0.0:12345\n";
    io_ctx.run();
    return 0;
}
