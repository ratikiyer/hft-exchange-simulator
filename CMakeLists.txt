set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.14)
project(hft_exchange LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
if (NOT Boost_FOUND)
  message(FATAL_ERROR "Boost.System not found; install libboost-system-dev")
endif()

add_library(orderbook_lib
  src/orderbook.cpp
  src/logger.cpp
)
target_include_directories(orderbook_lib
  PUBLIC
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
)
target_link_libraries(orderbook_lib
  PUBLIC Boost::system
)

add_executable(hft-exchange
  src/main.cpp
)
target_link_libraries(hft-exchange
  PRIVATE orderbook_lib
)

include(FetchContent)
FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.3.2
)
FetchContent_MakeAvailable(catch2)

add_executable(test-orderbook
  tests/test_orderbook.cpp
)
target_link_libraries(test-orderbook
  PRIVATE
    orderbook_lib
    Catch2::Catch2WithMain
)
enable_testing()
add_test(NAME test-orderbook COMMAND test-orderbook)
