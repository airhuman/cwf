#include "redis_aof_deliver/deliver.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <stdlib.h>

#include <sstream>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base3/logging.h"
#include "base3/stringprintf.h"
#include "base3/stringdigest.h"

#include "redis_aof_deliver/stat.h"

namespace qunar {

//const int RedisAofDeliver::kTrackReadBufferSize = 1024 * 1024 * 20; //20MB


#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )


static int GetFileSize(const std::string& file_path, off_t *size) {
  struct stat st;
  if (stat(file_path.c_str(), &st) == -1) {
    *size = -1;
    return -1;
  }
  *size = st.st_size;
  return 0;
}
//---------------
RedisAofDeliver::RedisAofDeliver(const std::string& aof_file_path)
  :aof_file_path_(aof_file_path), shutdown_(false) {
  track_aof_fd_ = open(aof_file_path_.c_str(), O_APPEND, O_RDONLY);
  if (track_aof_fd_ < 0) {
    LOG(ERROR) << "Can't open file! aof_file_path:" << aof_file_path_;
    exit(-1);
  }
  //LOG(INFO) << "track_aof_fd_: " << track_aof_fd_;
  //LOG(INFO) << "track_pos_: " << track_pos_;
  //LOG(INFO) << "lseek(" << track_aof_fd_ << ", 0, SEEK_CUR): " << lseek(track_aof_fd_, 0, SEEK_CUR);
}

RedisAofDeliver::~RedisAofDeliver() {
  close(track_aof_fd_);

}

void RedisAofDeliver::Start() {
  deliver_thread_ = new boost::thread(boost::bind(&RedisAofDeliver::ProcessDeliver, this));
  stat_thread_ = new boost::thread(boost::bind(&RedisAofDeliver::ProcessStat, this));
  track_aof_thread_ = new boost::thread(boost::bind(&RedisAofDeliver::ProcessTrack, this));

  worker_group_.add_thread(deliver_thread_);
  worker_group_.add_thread(stat_thread_);
  worker_group_.add_thread(track_aof_thread_);

  ProcessNotify();
}

void RedisAofDeliver::Stop() {
  shutdown_ = true;
  LOG(INFO) << "Stopping...";
  close(notify_fd_);
}

void RedisAofDeliver::ProcessDeliver() {
  // deliver commands
  while (!shutdown_) {
    Command* cmd = NULL;
    {
      boost::mutex::scoped_lock alock(command_mutex_);
      if (commands_.empty()) {
        command_cond_.wait(alock);
      } else {
        cmd = commands_.front();
        commands_.pop_front();
      }

      if (cmd) {
        if (transporter_) {
          transporter_->SendCommand(cmd);
        }
      }
    }
  }
}


int RedisAofDeliver::ConsumeNewline(const char* buf) {
  if (strncmp(buf, "\r\n", 2) != 0) {
    LOG(ERROR) << "Expected \\r\\n, got:" << buf[0] << buf[1];
    return 0;
  }
  return 1;
}

//return the first "\r\n" position
int RedisAofDeliver::ReadUntilNewline(int fd, off_t offset, char* buf, size_t buflen) {
  size_t nread = pread(fd, buf, buflen, offset);
  if (nread == -1) {
    return -1;
  }

  if (nread == 0) {
    boost::mutex::scoped_lock alock(track_mutex_);
    track_cond_.wait(alock);
    return ReadUntilNewline(fd, offset, buf, buflen);
  }
  static char kNewline[2] = {'\r', '\n'};
  int index = -1;
  for (int i = 0; i < nread - 1; ++i) {
    if (buf[i] == '\r' && buf[i+1] == '\n') {
      index = i;
      break;
    } 
  }
  
  // one more try
  if (index == -1 && nread < buflen) {
    boost::mutex::scoped_lock alock(track_mutex_);
    track_cond_.wait(alock);
    index = ReadUntilNewline(fd, offset, buf, buflen);
  }
  return index;
}

int RedisAofDeliver::ReadLong(int fd, char prefix, long* target, std::string* cmd) {
  size_t buf_len = 128;
  char buf[buf_len], *eptr;
  track_pos_ = lseek(fd, 0, SEEK_CUR);
  int idx = ReadUntilNewline(fd, track_pos_, buf, buf_len);
  if (idx == -1) {
    return 0;
  }
  if (buf[0] != prefix) {
    LOG(ERROR) << "Expected prefix '" << prefix << "' got'" << buf[0];
    return 0;
  }
  *target = strtol(buf + 1, &eptr, 10);
  cmd->append(buf, eptr + 2);
  track_pos_ = lseek(fd, track_pos_ + idx + 2, SEEK_SET);
  return 1;
}


int RedisAofDeliver::ReadArgc(int fd, long* target, std::string* cmd) { 
  return ReadLong(fd, '*', target, cmd);
}

int RedisAofDeliver::ReadBytes(int fd, std::string* cmd, long length) { 
  long real;
  track_pos_ = lseek(fd, 0, SEEK_CUR);
  char *data = (char*) malloc(sizeof(char) * length);
  real = pread(fd, data, length, track_pos_);
  if (real == -1) {
    LOG(ERROR) << "pread failed!";
    return 0;
  }
  if (real < length) {
    boost::mutex::scoped_lock alock(track_mutex_);
    track_cond_.wait(alock);
    free(data);
    return ReadBytes(fd, cmd, length); 
  } else {
    cmd->append(data, length);
    free(data);
    track_pos_ = lseek(fd, track_pos_ + length, SEEK_SET);
  }
  return 1;
}

int RedisAofDeliver::ReadString(int fd, std::string* cmd) { 
  long len;
  if (!ReadLong(fd, '$', &len, cmd)) {
    return 0;
  }
  len += 2; // for \r\n
  if (!ReadBytes(fd, cmd, len)) {
    return 0;
  }
  if (!ConsumeNewline(cmd->data() + cmd->size() - 2)) {
    return 0;
  }
  return 1;
}

void RedisAofDeliver::ProcessTrack() {
  long argc;
  int multi = 0;
  while (!shutdown_) {
    std::string cmd;
    std::string key;
    if (!multi) {
      track_pos_ = lseek(track_aof_fd_, 0, SEEK_CUR);
    }
    if (!ReadArgc(track_aof_fd_, &argc, &cmd)) {
      break;
    }
    bool ignore = false;
    for (int i = 0; i < argc; ++i) {
      std::string step;
      if (!ReadString(track_aof_fd_, &step)) {
        break;
      }
      cmd.append(step);
      if (i == 0) {
        if (strncasecmp(step.data() + step.size() - 7, "multi", 5)
            == 0) {
          if (multi++) {
            LOG(ERROR) << "Unexpected MULTI";
            break;
          }
        } else if (strncasecmp(step.data() + step.size() - 6, "exec", 4)
            == 0) {
          if (--multi) {
            LOG(ERROR) << "Unexpected EXEC";
            break;
          }
        } else if (strncasecmp(step.data() + step.size() - 8, "SELECT", 6) == 0) {
          ignore = true;
        }
      } else if (i == 1) {
        int start = step.find("\r\n") + 2;
        int end = step.rfind("\r\n");
        key = step.substr(start, end - start);
      }
    } // for

    //1. add command to track command list
    //2. IncrNotDownCount
    //3. notify Deliver Thread to work
    {
      boost::mutex::scoped_lock alock(command_mutex_); 
      if (!ignore) {
        commands_.push_back(new Command(cmd, key, ignore));
        Stat::Instance().IncrCount();
      }
    }
    command_cond_.notify_all();
  } // while
}

void RedisAofDeliver::ProcessNotify() {
  notify_fd_ = inotify_init(); 
  if (notify_fd_ < 0) {
    LOG(ERROR) << "inotify_init() failed!";
    return;
  }
  watch_fd_ = inotify_add_watch(notify_fd_, aof_file_path_.c_str(), IN_MODIFY);
  if (watch_fd_ < 0) {
    LOG(ERROR) << "inotify_add_watch() failed!";
    return;
  }

  char buffer[BUF_LEN];
  while (!shutdown_) {
    int length = read(notify_fd_, buffer, BUF_LEN);
    if (length < 0) {
      if (!shutdown_) {
        LOG(ERROR) << "read(notify_fd_, buffer, BUF_LEN) failed!";
      }
      break;
    }
    int i = 0;
    while (i < length) {
      struct inotify_event * event = (struct inotify_event *) &buffer[i];
      if ((event->mask & IN_MODIFY) && (!(event->mask & IN_ISDIR))) {
        track_cond_.notify_all();
      } 
      i += EVENT_SIZE + event->len;
    } // while
  }//while
}

void RedisAofDeliver::ProcessStat() {
  std::string stat_str;
  base::StringAppendF(&stat_str, "\n\n%20s%20s%20s", "AOF_CMD_COUNT", "DONE_AOF_CMD_COUNT", "TIME_COST(ms)");
  LOG(INFO) << stat_str;
  while (!shutdown_) {
    printf("\x1b[0G\x1b[2K%20d%20d%20ld", 
           Stat::Instance().count(),
           Stat::Instance().finished(), 
           Stat::Instance().TimeCost());
    fflush(stdout);
    boost::this_thread::sleep(boost::posix_time::seconds(2));
  }
}

} //qunar

