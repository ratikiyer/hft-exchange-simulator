#include "network_server.h"
#include "exchange.h"

NetworkServer::NetworkServer(boost::asio::io_context& io_ctx,
                           Exchange* exchange,
                           unsigned short port) :
                           io_context_(io_ctx),
                           acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)),
                           exchange_(exchange),
                           running_(false) { }

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
   auto new_socket = std::make_shared<tcp::socket>(io_context_);

   acceptor_.async_accept(
      *new_socket,
      [this, new_socket](const boost::system::error_code& ec) {
         if (!ec && running_) {
            auto session = std::make_shared<Session>(std::move(*new_socket), exchange_);
            session->start_reading();

            do_accept();
         } else {
            if (ec && running_) {
               std::cerr << "Accept error: " << ec.message() << '\n';
            }
         }
      }
   );
}

NetworkServer::Session::Session(boost::asio::ip::tcp::socket socket,
                                 Exchange* exchange) : 
                                 socket_(std::move(socket)),
                                 exchange_(exchange) {
   buffer_.fill(0);
}

void NetworkServer::Session::start_reading() {
   do_read();
}

void NetworkServer::Session::do_read() {
   auto self = shared_from_this();
   socket_.async_read_some(
      boost::asio::buffer(buffer_),
      [this, self](const boost::system::error_code& ec, size_t bytes_transferred) {
         if (!ec && bytes_transferred > 0) {
            exchange_->on_msg_received(buffer_.data(), bytes_transferred);
            do_read();
         } else {
            if (ec != boost::asio::error::operation_aborted) {
               std::cerr << "Session read error: " << ec.message() << "\n";
            }
         }
      }
   );
}
