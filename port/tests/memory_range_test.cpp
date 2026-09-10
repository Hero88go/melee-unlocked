#include "memory_range.h"
static_assert(host::valid_range(0, 0x1800000, 0x1800000));
static_assert(host::valid_range(0x17fffff, 1, 0x1800000));
static_assert(!host::valid_range(0x17fffff, 2, 0x1800000));
static_assert(!host::valid_range(0x100, 0xffffff00, 0x1800000));
static_assert(!host::valid_range(0xffffffff, 2, 0x1800000));
static_assert(host::valid_range(0x1800000, 0, 0x1800000));
int main() {}
