// Minimal machine boundary for testing the production Static Recomp menu adapter on any CPU.
#pragma once
#include <cstdint>
namespace ppc {
struct Context { uint32_t r[32]{}, lr = 0; };
using Fn = void (*)(Context&, uint8_t*);
Fn lookup(uint32_t);
Fn set_hook(uint32_t, Fn);
bool redirect_to_interpreter(uint32_t);
}
namespace host {
uint8_t rd8(uint32_t);
uint32_t rd32(uint32_t);
void wr8(uint32_t, uint8_t);
void log(const char*, ...);
}
