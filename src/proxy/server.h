#ifndef PROXY_SERVER_H_
#define PROXY_SERVER_H_

#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "base3/logging.h"

#include "proxy/connection.h"


namespace xce {

class ProxyServer : public boost::enable_shared_from_this<ProxyServer>, 
                    private boost::noncopyable {
 public:
  ProxyServer(const std::string& address, const std::string& port, std::size_t thread_pool_size);
    
  void Start();
  void Stop();

  boost::asio::io_service& GetService() {
    return io_service_;
  }

 private:
  void HandleSignal(const boost::system::error_code& ec, int sig_num);
  void StartAccept();
  void HandleAccept(const boost::system::error_code& ec);

  //void JoinAll();

 private:
  std::string address_;
  std::string port_;
  std::size_t thread_pool_size_;
  boost::thread_group thread_group_;
  boost::asio::io_service io_service_;
  boost::asio::io_service::work work_;
  boost::asio::signal_set signals_;
  boost::asio::ip::tcp::acceptor acceptor_;
  ConnectionPtr new_conn_;


};

} // end xce

#endif
