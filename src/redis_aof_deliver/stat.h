#ifndef REDIS_AOF_DELIVER_STAT_H_
#define REDIS_AOF_DELIVER_STAT_H_

#include <boost/thread.hpp>
#include <boost/date_time.hpp>

namespace qunar {

class Stat {
 public:
  Stat();
  virtual ~Stat();
  static Stat& Instance();

  int count()  {
    boost::mutex::scoped_lock alock(mutex_);
    return count_;
  }

  int IncrCount() {
    boost::mutex::scoped_lock alock(mutex_);
    return ++count_;
  }
 
  int finished() {
    boost::mutex::scoped_lock alock(mutex_);
    return finished_;
  }

  int IncrFinished() {
    boost::mutex::scoped_lock alock(mutex_);
    return ++finished_;
  }

  boost::posix_time::time_duration::tick_type TimeCost() const {
    return (boost::posix_time::second_clock::local_time() - start_time_).total_milliseconds();
  }

 private:
  boost::mutex mutex_;
  int count_;
  int finished_;
  boost::posix_time::ptime start_time_;
};

} //qunar

#endif

