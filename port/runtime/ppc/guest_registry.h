// Registry ABI shared with Legacy's generated tables. Source supplies empty tables.
// No generated function declarations are needed by the shared host.
#pragma once
#include "ppc.h"
#include <cstddef>
namespace guest {
struct FnEntry { uint32_t addr; ppc::Fn fn; };
extern const FnEntry fn_table[];
extern const size_t fn_table_count;
struct NameEntry { uint32_t addr; const char* name; };
extern const NameEntry name_table[];
extern const size_t name_table_count;
}
