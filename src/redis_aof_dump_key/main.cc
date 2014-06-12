#include <stdlib.h>
#include <fstream>
#include <string>
#include "base3/logging.h"
#include "base3/ptime.h"
#include "redis_aof_dump_key/dumper.h"

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



int main(int argc, char* argv[]) {
  OpenLogger(NULL);
  if (argc < 2) {
    LOG(ERROR) << "\n\tUsage: " << argv[0] << " <AOF_FILE> <OUTPUT_FILE>";
    exit(-1);
  }
  LOG(INFO) << argv[0] << " start"; 
  PTIME(pt, "main", true, 0);

  std::ifstream fin(argv[1], std::ios_base::in);
  if (!fin) {
    LOG(ERROR) << "error AOF! path:" << argv[2];
    exit(-1);
  }
  fin.close();
  
  std::string aof = argv[1];
  std::string output = argv[2];

  qunar::Dumper dumper(aof, output);
  dumper.Start();
  LOG(INFO) << argv[0] << " end"; 
  return 0;
} //main

