#include "guest_registry.h"
#include "gecko_data.h"
#include "host.h"
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace host {
[[noreturn]] void die(const char* fmt, ...) {
  char message[256];
  va_list args; va_start(args,fmt); std::vsnprintf(message,sizeof message,fmt,args); va_end(args);
  throw std::runtime_error(message);
}
}
int main() {
  if(guest::fn_table_count || guest::name_table_count || gecko::codehandler_bin_size ||
     gecko::bootloader_gct_size || gecko::slippi_gct_size || gecko::boot_hooks_count ||
     gecko::boot_writes_count || gecko::optional_writes_count) return 1;
  uint64_t calls=1,instructions=1;
  ppc::interpreter_stats(&calls,&instructions);
  if(calls || instructions) return 2;
  ppc::Context context{};
  try { ppc::interpret(context,nullptr,0x8000522c); }
  catch(const std::runtime_error& error) {
    std::string message=error.what();
    return message.find("rejected PowerPC execution at 8000522C")!=std::string::npos ? 0:3;
  }
  return 4; // execution must be rejected, never silently skipped
}
