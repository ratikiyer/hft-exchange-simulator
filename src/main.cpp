// main.cpp
#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include "exchange.h"
#include "logger.h"
#include "order_parser.h"
#include "market_data_publisher.h"
#include "network_server.h"
#include "types.h"

#ifdef __APPLE__
  #include <libkern/OSByteOrder.h>
  #define htobe64(x) OSSwapHostToBigInt64(x)
#else
  #include <endian.h>
#endif

int main() {
  return 0;
}
