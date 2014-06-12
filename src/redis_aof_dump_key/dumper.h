#ifndef REDIS_AOF_DUMP_KEY_DUMPER_H_
#define REDIS_AOF_DUMP_KEY_DUMPER_H_

#include <string>
#include <fstream>
#include <boost/thread.hpp>

namespace qunar {

class Dumper {
 public:
  Dumper(const std::string&, const std::string&);
  virtual ~Dumper();
  void Start();

  int ConsumeNewline(const char*);
  int ReadLong(int fd, char prefix, long* target, std::string* cmd);
  int ReadArgc(int fd, long* target, std::string* cmd);
  int ReadBytes(int fd, std::string* cmd, long length);
  int ReadString(int fd, std::string* cmd);
  int ReadUntilNewline(int fd, off_t offset, char* buf, size_t buflen);

 private:
  std::string aof_file_path_;
  std::string output_;
  std::ofstream* out_;
  //boost::thread* notify_thread_;
  //int watch_fd_;
  //int notify_fd_;
  int track_aof_fd_;
  off_t track_pos_;
  off_t start_max_offset_;
  boost::mutex track_mutex_;
  boost::condition_variable_any track_cond_;

};

} // qunar

#endif

