#ifndef PROXY_CONNECTION_H_
#define PROXY_CONNECTION_H_

#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>

namespace xce {



class Connection : public boost::enable_shared_from_this<Connection>, 
                   private boost::noncopyable {
 public:
  explicit Connection(boost::asio::io_service& io_service);
  void Start();

  boost::asio::ip::tcp::socket& socket() {
    return socket_;
  }

 private:
  void HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred);
  void HandleWrite(const boost::system::error_code& ec, std::size_t bytes_transferred);

 private:
  boost::array<char, 1024> buffer_;
  boost::asio::io_service::strand strand_;
  boost::asio::ip::tcp::socket socket_;

};

typedef boost::shared_ptr<Connection> ConnectionPtr;

} // end xce

#endif
