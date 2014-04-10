#include <cstdio>
#include <list>
#include <iostream>
#include <fstream>

#if defined(OS_LINUX)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <netinet/in.h>
# include <sys/un.h>
# include <sys/wait.h>
#endif

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "base3/getopt_.h"
#include "base3/logging.h"
#include "base3/signals.h"
#include "proxy/redis_proxy_server.h"

#if defined(POSIX) || defined(OS_LINUX)
#include <unistd.h>
#include <errno.h>
#endif


static void ShowHelp() {
  puts("\nUsage: redis_proxy [options] \n" \
    "\n" \
    "Options:\n" \
    " -d <directory> chdir to directory before spawning\n" \
    " -a <address>   bind to IPv4/IPv6 address (defaults to 0.0.0.0)\n" \
    " -p <port>      bind to TCP-port\n" \
    " -l <file>      log file name\n" \
    " -t <thread>    thread count\n" \
    " -C <children>  (PHP only) numbers of childs to spawn (default: not setting\n" \
    "                the PHP_FCGI_CHILDREN environment variable - PHP defaults to 0)\n" \
    " -F <children>  number of children to fork (default 1)\n" \
    " -P <path>      name of PID-file for spawned process (ignored in no-fork mode)\n" \
    " -n             no fork \n" \
    " -v             show version\n" \
    " -?, -h         show this help\n" \
  );  
}

#if defined(OS_LINUX)
int Daemon() {
  switch (fork()) {
    case -1:
      PLOG(ERROR) << "fork() failed";
      return -1;

    case 0:
      break;

    default:
      exit(0);
  }

  pid_t pid = getpid();

  if (setsid() == -1) {
    PLOG(ERROR) << "setsid() failed";
    return -1;
  }

  umask(0);

  int fd = open("/dev/null", O_RDWR);
  if (fd == -1) {
    PLOG(ERROR) << "open(\"/dev/null\") failed";
    return -1;
  }

  if (dup2(fd, STDIN_FILENO) == -1) {
    PLOG(ERROR) << "dup2(STDIN) failed";
    return -1;
  }

  if (dup2(fd, STDOUT_FILENO) == -1) {
    PLOG(ERROR) << "dup2(STDOUT) failed";
    return -1;
  }

  if (dup2(fd, STDERR_FILENO) == -1) {
    LOG(ERROR) << "dup2(STDERR) failed";
    return -1;
  }
  return 0;
}


volatile int fork_count_ = 0;
volatile int quit_ = 0;
std::list<int> * children_ = 0;
boost::asio::io_service io_service_;
boost::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor_ptr_;

void KillChildren(int sig) {
  if (!children_)
    return;

  for (std::list<int>::const_iterator i=children_->begin();
      i!=children_->end(); ++i) {
    int pid = *i;
    int ret = kill(pid, sig);
    LOG(INFO) << "kill " << pid << " " << sig << " ret:" << ret;
  }
}

void SignalTerminate(int) {
  quit_ = 1;

  KillChildren(SIGKILL);
}

void SignalReopen(int) {
  logging::ReopenLogFile();

  KillChildren(SIGUSR1);
}

void SignalChildren(int) {
  if (!quit_) {
    fork_count_ = 1;
  }

  int status = 0;
  for (;;) {
    int pid = waitpid(-1, &status, WNOHANG);
    if (0 == pid)
      return;

    if (-1 == pid) {
      PLOG(INFO) << "master waitpid"; // TODO: errno = EINTR, ECHLD
      return;
    }

    if (pid > 0) {
      children_->remove(pid); // remove the child process which is already killed
    }

    if (WIFEXITED(status))
      LOG(INFO) << "master waitpid: " << WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      LOG(INFO) << "master waitpid: " << WTERMSIG(status);
    else
      LOG(INFO) << "master waitpid: exit status = " << status;
  }
}

// return > 1 for child pid
// return 0 for child
// return < 0 for error
int Fork(int thread_count, const char * log_filename) {
  int rc = 0;

  io_service_.notify_fork(boost::asio::io_service::fork_prepare);

  pid_t child = fork();

  if (-1 == child) {
    PLOG(ERROR) << "master fork failed";
    return -1;
  }

  // parent
  if (child) {
    io_service_.notify_fork(boost::asio::io_service::fork_parent);

    struct timeval tv = { 0, 100 * 1000 };
    select(0, NULL, NULL, NULL, &tv);

    int status = 0;
    switch (waitpid(child, &status, WNOHANG)) {
      case 0:
        LOG(INFO) << "child spawned successfully, PID: " << child;
        break;

      default:
        if (WIFEXITED(status))
          LOG(INFO) << "child exited with: " << WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
          LOG(INFO) << "child signaled: " << WTERMSIG(status);
        else
          LOG(INFO) << "child died somehow: exit status = " << status;
    }
    return child;
  }

  // child
  if (0 == child) {
    io_service_.notify_fork(boost::asio::io_service::fork_child);

    // ChildrenCycle for reopen log, quit
    LOG(INFO) << "child proccess begin";

    base::InstallSignal(SIGUSR1, SignalReopen);
    base::InstallSignal(SIGTERM, SIG_IGN);

    xce::RedisProxyServer redis_proxy_server(thread_count, io_service_, *acceptor_ptr_);
    redis_proxy_server.Start();
    return 0;
  }

  return rc;
}


int MasterCycle(int thread_count, const char * log_filename) {
  base::InstallSignal(SIGCHLD, SignalChildren);

  base::InstallSignal(SIGINT, SignalTerminate);
  base::InstallSignal(SIGTERM, SignalTerminate);

  base::InstallSignal(SIGUSR1, SignalReopen);

  //sigset_t           set;
  //sigemptyset(&set);
  //sigaddset(&set, SIGCHLD);
  //sigaddset(&set, SIGALRM);
  //sigaddset(&set, SIGINT);
  //sigaddset(&set, SIGHUP);
  //sigaddset(&set, SIGTERM);

  while (true) {
    int ret = pause();
    LOG(INFO) << "supspend once " << ret;

    if (fork_count_) {
      int child = Fork(thread_count, log_filename);
      if (0 == child)
        return 0;

      children_->push_back(child);

      fork_count_ = 0;
    }

    if (quit_) {
      // quit children
      LOG(INFO) << "normal quit";
      break;
    }
  }

  return 0;
}

#endif

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
  if (argc == 1) {
    ShowHelp();
  }

  int port = 4000;
  const char * addr = "0.0.0.0";
  char * redis_proxy_dir = 0;
  char * log_filename = 0;
  char * pid_file = 0;
  int thread_count = 0;
  int nofork = 0;
  int fork_count = 0;
  int o;

  while (-1 != (o = getopt(argc, argv, "a:d:F:p:t:l:P:?hen"))) {
    switch(o) {
    case 'd': redis_proxy_dir = optarg; break;
    case 'l': log_filename = optarg; break;
    case 'a': addr = optarg;/* ip addr */ break;
    case 'p': 
      {
        char *endptr = 0;
        port = strtol(optarg, &endptr, 10);/* port */
        if (*endptr) {
          fprintf(stderr, "spawn-fcgi: invalid port: %u\n", (unsigned int) port);
          return -1;
        }
      }
      break;
    case 't': thread_count = strtol(optarg, NULL, 10);/*  */ break;
    case 'F': fork_count = strtol(optarg, NULL, 10);/*  */ break;
    case 'n': nofork = 1; break;
    case 'P': pid_file = optarg; /* PID file */ break;
//    case 'v': show_version(); return 0;
    case '?':
    case 'h': ShowHelp(); return 0;
    default:
      ShowHelp();
      return 0;
    }
  }


  OpenLogger(log_filename);

#if defined(POSIX) || defined(OS_LINUX)
  if (redis_proxy_dir && -1 == chdir(redis_proxy_dir)) {
    fprintf(stderr, "spawn-fcgi: chdir('%s') failed: %s\n", redis_proxy_dir, strerror(errno));
    return -1;
  }
#endif


//socket
#if defined(OS_LINUX) 
   // bind socket
   acceptor_ptr_.reset(new boost::asio::ip::tcp::acceptor(io_service_));
   boost::asio::ip::tcp::resolver resolver(io_service_);
   boost::asio::ip::tcp::resolver::query query(addr,  boost::lexical_cast<std::string>(port));
   boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(query);
   acceptor_ptr_->open(endpoint.protocol());
   acceptor_ptr_->set_option(boost::asio::socket_base::reuse_address(true));
   acceptor_ptr_->set_option(boost::asio::socket_base::keep_alive(true));
   acceptor_ptr_->set_option(boost::asio::ip::tcp::no_delay(true));
   acceptor_ptr_->bind(endpoint);
   acceptor_ptr_->listen();

#endif

#if defined(OS_LINUX) 
  if (!nofork && 0 != Daemon()) {
    return -1;
  }

  if (pid_file) {
    // TODO: open with CLOSE_REMOVE
    std::ofstream pidfile(pid_file);
    if (pidfile)
      pidfile << getpid() << "\n";
  }

  if (fork_count) {
    std::list<int> children;

    int c = fork_count;
    while (c--) {
      int child = Fork(thread_count, log_filename);
      if (0 == child)
        return 0;

      children.push_back(child);
    }   

    children_ = new std::list<int>();
    children_->swap(children);

    return MasterCycle(thread_count, log_filename);
  }
#endif
  return 0;
}


/*
    logging::SetLogItems(true, true, true, false);
   
    LOG(INFO) << "hello proxy";

    try {
      xce::ProxyServer ps("127.0.0.1", "6400", 8);
      ps.Start();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
*/

