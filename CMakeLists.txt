cmake_minimum_required(VERSION 3.14)
project(hft_exchange LANGUAGES CXX)

# Export compile_commands.json for editors/IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost.System
find_package(Boost REQUIRED COMPONENTS system)
if (NOT Boost_FOUND)
  message(FATAL_ERROR "Boost.System not found; install libboost-system-dev")
endif()

# ----------------------------------------------------------------------------
# orderbook + logger library
# ----------------------------------------------------------------------------
add_library(orderbook_lib
  src/orderbook.cpp
  src/logger.cpp
)

target_include_directories(orderbook_lib PUBLIC
  ${Boost_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/includes   # for types.h, json.hpp (if used here)
)

target_link_libraries(orderbook_lib PUBLIC Boost::system)

# ----------------------------------------------------------------------------
# exchange + parser library
# ----------------------------------------------------------------------------
add_library(exchange_lib
  src/local_exchange.cpp
  src/order_parser.cpp
)

target_include_directories(exchange_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/includes   # for local_exchange.h, order_parser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src        # if you include headers alongside .cpp
)

target_link_libraries(exchange_lib PUBLIC orderbook_lib)

# ----------------------------------------------------------------------------
# main exchange executable
# ----------------------------------------------------------------------------
add_executable(hft-exchange
  src/main.cpp
)

target_link_libraries(hft-exchange PRIVATE
  orderbook_lib
  exchange_lib
)

# ----------------------------------------------------------------------------
# client simulator
# ----------------------------------------------------------------------------
add_executable(client
  src/client.cpp
)

target_include_directories(client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/includes   # your json.hpp and headers
  ${CMAKE_CURRENT_SOURCE_DIR}/src        # for local_exchange.h, types.h
)

target_link_libraries(client PRIVATE exchange_lib)

# ----------------------------------------------------------------------------
# tests (using Catch2 via FetchContent)
# ----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.3.2
)
FetchContent_MakeAvailable(catch2)

# test exchange logic
add_executable(test_local_exchange
  tests/test_local_exchange.cpp
)

target_include_directories(test_local_exchange PRIVATE
  ${Boost_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/includes
)

target_link_libraries(test_local_exchange PRIVATE
  exchange_lib
  Catch2::Catch2WithMain
)

add_test(NAME test-local-exchange COMMAND test_local_exchange)

# test orderbook logic
add_executable(test-orderbook
  tests/test_orderbook.cpp
)

target_include_directories(test-orderbook PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/includes
)

target_link_libraries(test-orderbook PRIVATE
  orderbook_lib
  Catch2::Catch2WithMain
)

add_test(NAME test-orderbook COMMAND test-orderbook)