// The real host's stdio transport, driven by a synthetic local app. No game or sockets.
#include "yougame_link.h"
#include "host.h"
#include <cstdio>
int main() {
  yougame::start();
  const auto identity = yougame::wait_identity(5000);
  const bool ok = yougame::stdio_bridge() && identity.ok && identity.name == "Mac Spike" && identity.code == "MAC#1";
  std::printf("Mac stdio bridge integration: %s\n", ok ? "PASS" : "FAIL");
  host::end_process(ok ? 0 : 1);  // the bridge reader intentionally lives until process exit
}
