#ifndef REDIS_AOF_DELIVER_TRANSPORTER_H_
#define REDIS_AOF_DELIVER_TRANSPORTER_H_

#include <string>
#include <list>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

#include "base3/consistenthash.h"

namespace qunar {

struct Endpoint {
  std::string cluster_name_; //required
  std::string host_; //required
  int port_; //required
  std::string pwd_; //required
  int timeout_; //required
  int core_; //optional
  int max_;  //optional
};

struct Command;
class Worker;

class Transporter {
 public:
  explicit Transporter(const std::list<std::string>& cluster_list);
  virtual ~Transporter();
  void SendCommand(Command* cmd);

 private:
  bool AddEndpoint(const std::string& config);

 private:
   boost::asio::io_service io_service_;
   base::Continuum continuum_;
   boost::array<char, 8192> buffer_;
   typedef std::map<std::string, boost::shared_ptr<Worker> > WorkerMap;
   WorkerMap worker_map_;
};

struct Command {
  Command(const std::string& data, const std::string& key, 
          bool ignore): data_(data), key_(key),  ignore_(ignore) {}
  std::string data_;
  std::string key_;
  bool ignore_;
};

class Worker {
 public:
  explicit Worker(boost::asio::ip::tcp::socket*);
  void Post(Command* cmd) {
    if (cmd) {
      boost::mutex::scoped_lock lock(task_mutex_);
      task_queue_.push_back(cmd);
    }
    task_cond_.notify_all();
  }
  virtual ~Worker();
  void Stop();

 private:
  void Handle();

 private:
  boost::mutex task_mutex_;
  boost::condition_variable_any task_cond_;
  std::list<Command*> task_queue_; 
  boost::asio::ip::tcp::socket* socket_;
  boost::thread* work_thread_;

};

} // qunar
#endif
