#include "proxy/server.h"

#include <boost/bind.hpp>

#include "proxy/connection.h"

namespace xce {

ProxyServer::ProxyServer(const std::string& address, 
               const std::string& port, 
               std::size_t thread_pool_size) 
               : address_(address), port_(port), 
                 thread_pool_size_(thread_pool_size),
                 work_(io_service_), 
                 signals_(io_service_),
                 acceptor_(io_service_) {

    // set up signals
    signals_.add(SIGINT);
    signals_.add(SIGTERM);
    signals_.add(SIGQUIT);
    signals_.async_wait(boost::bind(&ProxyServer::HandleSignal, this, boost::asio::placeholders::error, boost::asio::placeholders::signal_number));

    // open acceptor
    boost::asio::ip::tcp::resolver resolver(io_service_);
    boost::asio::ip::tcp::resolver::query query(address_, port_);
    boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.set_option(boost::asio::socket_base::keep_alive(true));
    acceptor_.set_option(boost::asio::ip::tcp::no_delay(true));
    //acceptor_.non_blocking(true);
    acceptor_.bind(endpoint);
    acceptor_.listen();
    StartAccept();

}

void ProxyServer::HandleSignal(const boost::system::error_code& ec, int sig_num) {
    LOG(WARNING) << "HandleSignal() ec=" << ec << " sig_num=" << sig_num;
    Stop();
}



void ProxyServer::Start() {
  io_service_.run();
}

void ProxyServer::Stop() {
  io_service_.stop();
}

void ProxyServer::StartAccept() {
  new_conn_.reset(new Connection(io_service_));
  acceptor_.async_accept(new_conn_->socket(), 
    boost::bind(&ProxyServer::HandleAccept, this, 
    boost::asio::placeholders::error));
}

void ProxyServer::HandleAccept(const boost::system::error_code& ec) {
  if (!ec) {
    //LOG(INFO) << "new_conn_->socket().non_blocking(): " << new_conn_->socket().non_blocking();
    //LOG(INFO) << "acceptor_.non_blocking(): " << acceptor_.non_blocking();
    new_conn_->Start();
  }
  //continue to accept another socket
  StartAccept();
}

} //end xce


