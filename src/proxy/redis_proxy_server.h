#ifndef REDIS_PROXY_SERVER_H_
#define REDIS_PROXY_SERVER_H_

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "base3/logging.h"

#include "proxy/redis_connection.h"


namespace xce {

class RedisProxyServer : private boost::noncopyable {
 public:
  //RedisProxyServer(const std::string& address, const std::string& port, std::size_t thread_pool_size);
  RedisProxyServer(std::size_t thread_pool_size, 
                   boost::asio::io_service& io_service,
                   boost::asio::ip::tcp::acceptor& acceptor);
    
  void Start();
  void Stop();

  boost::asio::io_service& GetService() {
    return io_service_;
  }

 private:
  //void HandleSignal(const boost::system::error_code& ec, int sig_num);
  void StartAccept();
  void HandleAccept(const boost::system::error_code& ec);

 private:
  std::string address_;
  std::string port_;
  std::size_t thread_pool_size_;
  boost::thread_group thread_group_;
  boost::asio::io_service& io_service_;
  boost::asio::io_service::work work_;
  //boost::asio::signal_set signals_;
  boost::asio::ip::tcp::acceptor& acceptor_;
  RedisConnectionPtr new_conn_;


};

} // end xce

#endif
