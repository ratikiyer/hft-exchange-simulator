// network_server.cpp
#include "network_server.h"
#include "exchange.h"

#include <iostream>
#include <boost/asio/ip/tcp.hpp>

using boost::asio::ip::tcp;

// message length is always 37 bytes
static constexpr std::size_t FULL_MSG_LEN =
    sizeof(uint64_t) + 1 + ORDER_ID_LEN + TICKER_LEN + 4 + 4;

NetworkServer::NetworkServer(boost::asio::io_context& io_ctx,
                             Exchange* exchange,
                             unsigned short port)
  : io_context_(io_ctx),
    acceptor_(io_ctx, tcp::endpoint(tcp::v4(), port)),
    exchange_(exchange),
    running_(false)
{}

void NetworkServer::start() {
    running_ = true;
    do_accept();
}

void NetworkServer::stop() {
    running_ = false;
    boost::system::error_code ec;
    acceptor_.close(ec);
}

void NetworkServer::do_accept() {
    auto sock = std::make_shared<tcp::socket>(io_context_);
    acceptor_.async_accept(
        *sock,
        [this, sock](auto ec) {
            if (!ec && running_) {
                auto session = std::make_shared<Session>(std::move(*sock), exchange_);
                session->start_reading();
                do_accept();
            }
            else if (ec && running_) {
                std::cerr << "Accept error: " << ec.message() << "\n";
            }
        }
    );
}

NetworkServer::Session::Session(tcp::socket socket, Exchange* exchange)
  : socket_(std::move(socket)),
    exchange_(exchange)
{
    buffer_.fill(0);
}

void NetworkServer::Session::start_reading() {
    auto self = shared_from_this();
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(buffer_, FULL_MSG_LEN),
        [this, self](auto ec, std::size_t n) {
            if (!ec && n == FULL_MSG_LEN) {
                exchange_->on_msg_received(buffer_.data(), n);
                start_reading();
            }
            else if (ec == boost::asio::error::eof) {
                // clean close
            }
            else {
                std::cerr << "Session read error: " << ec.message() << "\n";
            }
        }
    );
}
