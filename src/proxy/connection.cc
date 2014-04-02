#include "proxy/connection.h"
#include <string.h>
#include <string>
#include <boost/bind.hpp>
#include "base3/logging.h"

namespace xce {

static const std::string kQuit = "quit\r";

Connection::Connection(boost::asio::io_service& io_service) 
                      :strand_(io_service), socket_(io_service) {

};

void Connection::Start() {
  //register async read
  socket_.async_read_some(boost::asio::buffer(buffer_),
    strand_.wrap(boost::bind(&Connection::HandleRead, shared_from_this(),
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred)));
}


void Connection::HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred) {
  //if bytes_transferred == 0, then quit, and close socket
  LOG(INFO) << "ec=" << ec << ", bytes_transferred=" << bytes_transferred;
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
      LOG(INFO) << "Read Data: " << buffer_.data();
      socket_.async_read_some(boost::asio::buffer(buffer_),
        strand_.wrap(boost::bind(&Connection::HandleRead, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred)));
    }
  }
}

//void Connection::HandleWrite(const boost::system::error& ec) {
//  if (!ec) {
//
//  }
//}


} // end xce

