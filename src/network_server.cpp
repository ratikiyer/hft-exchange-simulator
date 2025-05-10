// network_server.cpp
#include "network_server.h"
#include "exchange.h"
#include <iostream>
#include <memory>
#include <boost/system/error_code.hpp>

using boost::asio::ip::tcp;

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
    if (ec) {
        std::cerr << "Error closing acceptor: " << ec.message() << "\n";
    }
}

void NetworkServer::do_accept() {
    auto sock = std::make_shared<tcp::socket>(io_context_);
    acceptor_.async_accept(
        *sock,
        [this, sock](const boost::system::error_code& ec) {
            if (!ec && running_) {
                // immediately spin up a session
                std::make_shared<Session>(std::move(*sock), exchange_)
                    ->start_reading();
            }
            else if (ec && running_) {
                std::cerr << "Accept error: " << ec.message() << "\n";
            }
            // re-arm accept loop
            if (running_) {
                do_accept();
            }
        }
    );
}

// ── Session Implementation ─────────────────────────────────────────────

NetworkServer::Session::Session(tcp::socket socket, Exchange* exchange)
  : socket_(std::move(socket)),
    exchange_(exchange)
{
    buffer_.fill(0);
}

void NetworkServer::Session::start_reading() {
    auto self = shared_from_this();
    socket_.async_read_some(
    boost::asio::buffer(buffer_.data(), buffer_.size()),
        [this, self](const boost::system::error_code& ec, std::size_t n) {
            if (!ec && n == buffer_.size()) {
                // dispatch to your exchange
                exchange->on_msg_received(
                    reinterpret_cast<const uint8_t*>(buffer_.data()), n);
                // read the next message
                start_reading();
            }
            else if (ec == boost::asio::error::eof) {
                // peer closed cleanly
            }
            else {
                std::cerr << "Session read error: " << ec.message() << "\n";
            }
        }
    );
}