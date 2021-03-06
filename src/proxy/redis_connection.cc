#include "proxy/redis_connection.h"
#include <string.h>
#include <string>
#include <boost/bind.hpp>
#include "base3/logging.h"

namespace xce {

static const std::string kQuit = "quit\n";
static const std::string kPONG = "+PONG\r\n";

RedisConnection::RedisConnection(boost::asio::io_service& io_service) 
                      :strand_(io_service), socket_(io_service) {
};

void RedisConnection::Start() {
  //register async read
  socket_.async_read_some(boost::asio::buffer(buffer_),
    strand_.wrap(boost::bind(&RedisConnection::HandleRead, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred)));
}


void RedisConnection::HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred) {
  //if bytes_transferred == 0, then quit, and close socket
  //LOG(INFO) << "ec=" << ec << ", bytes_transferred=" << bytes_transferred;
  if (ec && bytes_transferred == 0) {
    boost::system::error_code ignored_ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    return;
  }

  if (!ec) {
    if (bytes_transferred == 0 || (strncasecmp(buffer_.data(), kQuit.data(), kQuit.size()) == 0)) {
      boost::system::error_code ignored_ec;
      socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    } else {
      //LOG(INFO) << "Read Data: " << buffer_.data();
      //std::string write_buffer(buffer_.data(), bytes_transferred);
      std::string write_buffer(kPONG.data(), kPONG.size());
      socket_.async_write_some(boost::asio::buffer(write_buffer),
        strand_.wrap(boost::bind(&RedisConnection::HandleWrite, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred)));
      
    }
  }
}

void RedisConnection::HandleWrite(const boost::system::error_code& ec, 
                             std::size_t bytes_transferred) {
  socket_.async_read_some(boost::asio::buffer(buffer_),
    strand_.wrap(boost::bind(&RedisConnection::HandleRead, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred)));
}


} // end xce

