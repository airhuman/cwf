#include <string>
#include <list>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include "base3/logging.h"
#include "base3/signals.h"
#include "base3/ptime.h"
#include "base3/consistenthash.h"
#include "redis_aof_deliver/deliver.h"

static qunar::RedisAofDeliver* deliver_ = 0;

void OpenLogger(const char * log_filename) {
  using namespace logging;
  if (!log_filename)  {// ugly check
    SetLogItems(true, true, true, false);
    return;
  }

  InitLogging(log_filename, LOG_ONLY_TO_FILE
    , DONT_LOCK_LOG_FILE
    , APPEND_TO_OLD_LOG_FILE);

  // pid, thread_id, timestamp, tickcount
  SetLogItems(true, true, true, false);
}

void SignalTerminate(int sig_num) {
  if (deliver_) {
    deliver_->Stop();
  }
}

int main(int argc, char* argv[]) {
  OpenLogger(NULL);
  if (argc < 2) {
    LOG(ERROR) << "argc < 2";
    LOG(ERROR) << "\n\tUsage: " << argv[0] << " <AOF_FILE> <REDIS_ENDPOINTS_FILE>";
    exit(-1);
  }
  LOG(INFO) << argv[0] << " start"; 
  PTIME(pt, "main", true, 0);

  std::ifstream fin(argv[2], std::ios_base::in);
  if (!fin) {
    LOG(ERROR) << "error REDIS_ENDPOINTS_FILE! path:" << argv[2];
    exit(-1);
  }

  std::list<std::string> cluster_list;
  while (!fin.eof()) {
    std::string line;
    std::getline(fin, line);
    if (boost::starts_with(line, "cluster")) {
      cluster_list.push_back(line);
    }
  }
  fin.close();

  boost::shared_ptr<qunar::Transporter> tran_ptr;
  tran_ptr.reset(new qunar::Transporter(cluster_list));
  
  deliver_ = new qunar::RedisAofDeliver(argv[1]);
  deliver_->SetUpTransporter(tran_ptr);

  base::InstallSignal(SIGINT, SignalTerminate);
  deliver_->Start();
  return 0;
}
