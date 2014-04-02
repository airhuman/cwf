#include "base3/logging.h"
#include "proxy/server.h"


int main() {
    
    logging::SetLogItems(true, true, true, false);
   
    LOG(INFO) << "hello proxy";

    try {
      xce::ProxyServer ps("127.0.0.1", "6400", 1);
      ps.Start();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

