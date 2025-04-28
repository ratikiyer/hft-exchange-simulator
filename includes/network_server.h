#pragma once

#include <string>
#include <memory>
#include <boost/asio.hpp>

constexpr unsigned int BUFFER_SIZE = 4096;
class Exchange;

class NetworkServer {
public:

   NetworkServer(boost::asio::io_context& io_ctx, Exchange* exchange, unsigned short port);

   void start();

   void stop();

private:

   void do_accept();

   class Session : public std::enable_shared_from_this<Session> {
   public:

      Session(boost::asio::ip::tcp::socket socket, Exchange* exchange);

      void start_reading();
   
   private:
      
      void do_read();

      boost::asio::ip::tcp::socket socket_;
      Exchange* exchange_;
      std::array<uint8_t, BUFFER_SIZE> buffer_;
   };

private:
   boost::asio::io_context& io_context_;
   boost::asio::ip::tcp::acceptor acceptor_;
   Exchange* exchange_;
   bool running_;
};