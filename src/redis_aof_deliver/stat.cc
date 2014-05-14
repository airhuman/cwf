#include "redis_aof_deliver/stat.h"

#include "base3/once.h"

namespace qunar {


static Stat* stat_instance_ = 0;

static void InitStat() {
  stat_instance_ = new Stat();
}

Stat::Stat() : count_(0), finished_(0), 
               start_time_(boost::posix_time::second_clock::local_time()) {
}

Stat::~Stat() {

}

Stat& Stat::Instance() {
  using namespace base;
  static BASE_DECLARE_ONCE(once_guard_);
  BaseOnceInit(&once_guard_, &InitStat);
  return *stat_instance_;
}

} // qunar

