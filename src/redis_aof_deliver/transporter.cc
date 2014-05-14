#include "redis_aof_deliver/transporter.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "base3/logging.h"
#include "redis_aof_deliver/stat.h"

namespace qunar {

static std::string GetRedisPass(const std::string& cipher) {
  std::string md5 = base::MD5(cipher + "6379");
  std::ostringstream otem;
  otem << md5[5];
  otem << md5[2];
  otem << md5[6];
  otem << md5[8];
  otem << md5[15];
  otem << md5[12];
  otem << md5[16];
  otem << md5[18];  
  return otem.str();
}


Transporter::Transporter(const std::list<std::string>& cluster_list) : continuum_("redis_aof_deliver") {
  BOOST_FOREACH(const std::string& config, cluster_list) {
    if (!AddEndpoint(config)) {
      LOG(ERROR) << "AddEndpoint failed!";
      exit(-1); //ugly!
    }
  }
  continuum_.Rebuild();
  LOG(INFO) << "continuum_.size():" << continuum_.size();
}

Transporter::~Transporter() {
  LOG(INFO) << "Transporter::~Transporter()";
  WorkerMap::const_iterator iter = worker_map_.begin();
  for (; iter != worker_map_.end(); ++iter) {
      iter->second->Stop();
  }
  worker_map_.clear();
  LOG(INFO) << "Transporter::~Transporter() done";
}

bool Transporter::AddEndpoint(const std::string& config) {
  // <nickname>:<host>:<port>:<pwd>:<timeout>:<core>:<max>
  //          0:     1:     2:    3:        4:     5:    6
  std::vector<std::string> split_vec;
  boost::split(split_vec, config, boost::is_any_of(":"));
  std::string cluster_name = split_vec[0];
  std::string host = split_vec[1];
  std::string port = split_vec[2];
  std::string pwd = GetRedisPass(split_vec[3]);
  //set up socket
  using namespace boost::asio::ip;
  tcp::resolver resolver(io_service_);
  tcp::resolver::query query(tcp::v4(), host, port);
  tcp::resolver::iterator iterator = resolver.resolve(query);

  //boost::shared_ptr<tcp::socket> socket_ptr;
  //socket_ptr.reset(new tcp::socket(io_service_));
  tcp::socket* socket_ptr = new tcp::socket(io_service_);
  boost::asio::connect(*socket_ptr, iterator);
  //socket_ptr->non_blocking(true);
  socket_ptr->set_option(tcp::no_delay(true));
  std::ostringstream otem;
  otem << "*2\r\n"
       << "$4\r\n"
       << "AUTH\r\n"
       << "$" << pwd.size() << "\r\n" 
       << pwd << "\r\n";
  const std::string auth_req = otem.str();
  // send auth command
  try {
  size_t req_length = socket_ptr->write_some(boost::asio::buffer(auth_req.data(), auth_req.size()));
  } catch (std::exception& e) {
    LOG(ERROR) << e.what();
    return false;
  }
  static const std::string kAuthOK = "+OK\r\n";
  // receive auth command
  char data[1024];
  size_t reply_length;
  try {
    reply_length = socket_ptr->read_some(boost::asio::buffer(data, 1024));
  } catch (std::exception& e) {
    LOG(INFO) << e.what();
    return false;
  }
  if (std::string(data, reply_length) != kAuthOK) {
    LOG(ERROR) << "auth Failed, host:" << host << ",port:" << port << ",pwd:" << pwd;
    return false;
  } else {
    //socket_map_.insert(std::make_pair(cluster_name, socket_ptr));
    boost::shared_ptr<Worker> worker;
    worker.reset(new Worker(socket_ptr));
    worker_map_.insert(std::make_pair(cluster_name, worker));
    continuum_.Add(cluster_name, 100);
  }
  return true;
}

void Transporter::SendCommand(Command* cmd) {
  using namespace boost::asio::ip;
  std::string nickname = continuum_.Locate(base::Continuum::Hash(cmd->key_));
  WorkerMap::const_iterator iter = worker_map_.find(nickname);
  if (iter != worker_map_.end()) {
    iter->second->Post(cmd);
  }
  //SocketMap::const_iterator iter = socket_map_.find(sock_name);
  //if (iter != socket_map_.end()) {
  //  try {
  //    iter->second->write_some(boost::asio::buffer(cmd.data_, cmd.data_.size()));
  //  } catch (std::exception& e) {
  //    LOG(ERROR) << "SendCommand failed! " << e.what();
  //  }
  //}
}
//----

Worker::Worker(boost::asio::ip::tcp::socket* socket): socket_(socket) {
  work_thread_ = new boost::thread(boost::bind(&Worker::Handle, this));
}

Worker::~Worker() {
  delete work_thread_;
}

void Worker::Stop() {
  try {
    task_cond_.notify_one();
    work_thread_->interrupt();
    boost::this_thread::yield();
    work_thread_->join();
  } catch (std::exception& e) {
    LOG(ERROR) << e.what();
  }
  try {
    socket_->close();
  } catch (std::exception& e) {
    LOG(ERROR) << e.what();
  }
}


void Worker::Handle() {
  while (true) {
    Command* cmd = NULL;
    {
      boost::mutex::scoped_lock alock(task_mutex_);
      if (task_queue_.empty()) {
        task_cond_.wait(task_mutex_);
      } else {
        cmd = task_queue_.front();
        task_queue_.pop_front();
      }
    }
    if (cmd) {
      try {
        socket_->write_some(boost::asio::buffer(cmd->data_, cmd->data_.size()));
        Stat::Instance().IncrFinished();
      } catch (std::exception& e) {
        LOG(ERROR) << "Handle() socket write_some failed! " << e.what();
      }
      delete cmd;
    }
  }
}

} // qunar

