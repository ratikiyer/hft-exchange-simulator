// network_server.h
#pragma once

#include <array>
#include <cstddef>
#include <boost/asio.hpp>
#include <cstdint>
#include "types.h"

class Exchange; 

using boost::asio::ip::tcp;

// make this available in both .h and .cpp
static constexpr std::size_t FULL_MSG_LEN =
    sizeof(uint64_t) + 1 + ORDER_ID_LEN + TICKER_LEN + 4 + 4;

class NetworkServer {
public:
    NetworkServer(boost::asio::io_context& io_ctx,
                  Exchange* exchange,
                  unsigned short port);

    void start();
    void stop();

private:
    void do_accept();

    boost::asio::io_context&     io_context_;
    tcp::acceptor                acceptor_;
    Exchange*                    exchange_;
    bool                         running_;

    class Session
      : public std::enable_shared_from_this<Session>
    {
    public:
        Session(tcp::socket socket, Exchange* exchange);
        void start_reading();

    private:
        tcp::socket                       socket_;
        Exchange*                         exchange_;
        std::array<char, FULL_MSG_LEN>    buffer_;
    };
};