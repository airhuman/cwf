#include "redis_aof_dump_key/dumper.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "base3/logging.h"

namespace qunar {

Dumper::Dumper(const std::string& aof_file_path, 
               const std::string& output)
              : aof_file_path_(aof_file_path), 
                output_(output) {
  track_aof_fd_ = open(aof_file_path_.c_str(), O_APPEND, O_RDONLY);
  if (track_aof_fd_ < 0) {
    LOG(ERROR) << "Can't open file! aof_file_path:" << aof_file_path_;
    exit(-1);
  }
  start_max_offset_ = lseek(track_aof_fd_, 0, SEEK_END);
  track_pos_ = lseek(track_aof_fd_, 0, SEEK_SET);
  out_ = new std::ofstream(output_.c_str());
}

Dumper::~Dumper() {
  if (out_) {
    out_->flush();
    out_->close();
    delete out_;
  }
}

void Dumper::Start() {
  long argc;
  int multi = 0;
  while (track_pos_ != start_max_offset_) {
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
        //TODO: write key to output_
        *out_ << key << "\n";
      }
    } // for
  } // while
}

int Dumper::ConsumeNewline(const char* buf) {
  if (strncmp(buf, "\r\n", 2) != 0) {
    LOG(ERROR) << "Expected \\r\\n, got:" << buf[0] << buf[1];
    return 0;
  }
  return 1;
}

//return the first "\r\n" position
int Dumper::ReadUntilNewline(int fd, off_t offset, char* buf, size_t buflen) {
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

int Dumper::ReadLong(int fd, char prefix, long* target, std::string* cmd) {
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


int Dumper::ReadArgc(int fd, long* target, std::string* cmd) { 
  return ReadLong(fd, '*', target, cmd);
}

int Dumper::ReadBytes(int fd, std::string* cmd, long length) { 
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

int Dumper::ReadString(int fd, std::string* cmd) { 
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

} // qunar

