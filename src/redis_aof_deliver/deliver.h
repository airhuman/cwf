#ifndef REDIS_AOF_DELIVER_DELIVER_H_
#define REDIS_AOF_DELIVER_DELIVER_H_

#include <stdio.h>
#include <string>
#include <list>
#include <map>
#include <boost/thread.hpp>
#include <boost/date_time.hpp>
#include <boost/asio.hpp>

#include "redis_aof_deliver/transporter.h"

namespace qunar {

class RedisAofDeliver {
 public:
  explicit RedisAofDeliver(const std::string& aof_file_path);
  virtual ~RedisAofDeliver();
  void Start();
  void Stop();
  void SetUpTransporter(boost::shared_ptr<qunar::Transporter> tran) {
    transporter_ = tran;
  }

 private:
  int ConsumeNewline(const char*);
  int ReadLong(int fd, char prefix, long* target, std::string* cmd);
  int ReadArgc(int fd, long* target, std::string* cmd);
  int ReadBytes(int fd, std::string* cmd, long length);
  int ReadString(int fd, std::string* cmd);
  int ReadUntilNewline(int fd, off_t offset, char* buf, size_t buflen);
  void ProcessDeliver();
  void ProcessStat();
  void ProcessTrack();
  void ProcessNotify();

 private:
  std::string aof_file_path_;
  std::list<Command*> commands_;
  boost::mutex command_mutex_;
  boost::condition_variable_any command_cond_;

  int track_aof_fd_;
  off_t track_pos_;
  boost::mutex track_mutex_;
  boost::condition_variable_any track_cond_;

  bool shutdown_;
  
 private:
  boost::thread* deliver_thread_;
  boost::thread* stat_thread_;
  boost::thread* track_aof_thread_;
  int watch_fd_;
  int notify_fd_;
  boost::thread_group worker_group_;
  boost::shared_ptr<qunar::Transporter> transporter_;
};

} // qunar

#endif
