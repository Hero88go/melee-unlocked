// The player's own Gecko codes, read from GeckoCodes.ini (Dolphin's format: a [Gecko] section of
// "$Name" headers followed by "XXXXXXXX YYYYYYYY" lines; a [Gecko_Enabled] section is honoured on
// first read). Each code is switched in the PC settings panel.
//
// WHAT CAN WORK HERE
// Codes that write (types 00, 02, 04 and 06) work: they are applied every frame, as the Gecko
// handler does. The game's code is translated to PC code ahead of time, so an instruction written
// into RAM would never be executed; on the Static Recomp a write into the game's code therefore
// also switches the function it lands in to its bytes in RAM (ppc::redirect_function_at, the way a
// mod's changed functions run), and switching the code off puts the original bytes back. The Source
// Port runs no PowerPC and has no console memory: there a code works only when every byte it writes
// or reads lands inside one game variable that the native game keeps with the console's layout (a
// number, an array of numbers, a string, or a member of a struct; user_gecko_targets.h, generated
// by tools/gecko_targets.py). The game library stores those bytes in its own variable, in host byte
// order (set_native_writer), and hands them back the same way (set_native_reader). On the Source
// Port a code may use every Gecko type whose whole effect is such reads and writes: 00, 02, 04, 06,
// the serial write 08, the same through the pointer (10 to 18), a base address or pointer set to a
// number (42, 4A) or stored (44, 4C), the if types 20 to 2E with their endifs (E2, an address
// ending in 1) and the terminator E0, and the Gecko registers (80 to 88). Each line is turned into
// a step with its address worked out when the code is read (Code::plan), so nothing a code computes
// at run time can become an address. The addresses follow the console handler's own arithmetic
// (port/slippi_sys/codehandler.bin): a line counts from the pointer in full, but only from the top
// seven bits of the base address, so a base address inside game memory counts as 80000000; 32-bit
// writes and ifs round the address down to a word, 16-bit ifs to a half; a base or pointer store
// (44, 4C) and a register operation on memory (86 with 2) always count their address from the base.
// One thing differs from the handler, which runs every code as one list: here each code starts
// with the base address and the pointer at 80000000 and no condition open, whatever the code
// before it left behind, and an F0 line ends that code only (the note above Compiler in
// user_gecko.cpp). Any other code is listed with the reason, naming the function
// or variable it touches, and cannot be switched on: one that writes or injects PowerPC (C0, C2,
// C6), follows a pointer read from game memory (40, 48), loops or jumps (60 to 68), or compares
// registers (A0 to AE). Static Recomp instead runs the shipped console handler in reserved guest
// memory, including assembly injections, pointers, loops, searches and register operations.
// Changed functions execute their RAM instructions. Source never links this runner.
// The codes Slippi ships, and the port's own (widescreen, PAL stock icons, screen
// shake), are translated in and do not come from this file; a code made only of their patches is
// shown as built in (native_equivalent).
//
// Imported codes run offline only and are suspended during online sessions and replay playback.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace user_gecko {

// One step of a code on the Source Port, with its address already worked out (Code::plan).
struct Op {
  enum Kind : uint8_t { Write, If, Endif, End, GrSet, GrLoad, GrStore, GrOp, GrOpGr, GrOpMem };
  uint8_t kind = Write;
  uint8_t width = 0;    // bytes read or stored at once: 1, 2 or 4
  uint8_t reg = 0;      // the Gecko register, 0 to 15
  uint8_t aux = 0;      // If: 0 equal, 1 not equal, 2 greater, 3 less; Endif: 1 = then else;
                        // GrSet: 1 = add; GrOp, GrOpGr, GrOpMem: the operation, 0 to 10
  uint32_t addr = 0;    // console address read or written
  uint32_t value = 0;   // Write: where its bytes start in Code::plan_bytes; If: what to compare
                        // with; GrSet, GrOp: the number; GrOpGr: the other register
  uint32_t count = 0;   // Write: how many bytes; If: the mask kept before comparing; Endif: how
                        // many ifs end; GrStore: how many stores in a row
};

struct Code {
  std::string name;
  std::vector<std::string> notes;   // "*" lines under the header
  std::vector<std::pair<uint32_t, uint32_t>> lines;
  bool supported = false;
  std::string reason;               // why not, when unsupported
  bool enabled = false;
  bool patches_code = false;        // writes into the game's code: its functions run from RAM while on
  bool patch_live = false;          // those writes are in RAM now
  std::vector<std::pair<uint32_t, uint8_t>> original;   // the code bytes they replaced
  std::vector<Op> plan;             // the Source Port only: what apply() runs each frame
  std::vector<uint8_t> plan_bytes;  // the bytes of the plan's writes, in console order
  bool reads = false;               // the plan reads game variables (ifs, register loads)
};

// Whether a write into the game's code can take effect: yes on the Static Recomp (the function runs
// from RAM), no on the Source Port. Call before load(); the default is no.
void set_code_patches_allowed(bool allowed);

// The Source Port only: the game library's mu_user_gecko_write (shim/mu_gecko.c), which stores
// `length` big-endian bytes for console address `console_addr` in the native variable there.
// Returns 1 when stored, 0 when it has no such variable, -1 while a network match is running (the
// game takes no codes then). Until this is set, apply() does nothing on the Source Port.
using NativeWrite = int32_t (*)(uint32_t console_addr, const uint8_t* big_endian_bytes, uint32_t length);
void set_native_writer(NativeWrite write);
// The Source Port only: the game library's mu_user_gecko_read, which fills `length` big-endian
// bytes from the native variable at console address `console_addr`. The same three results as the
// writer. A game library without it leaves this null: a code that reads (an if, a register load)
// is then switched off with that reason the first time it would run.
using NativeRead = int32_t (*)(uint32_t console_addr, uint8_t* big_endian_bytes, uint32_t length);
void set_native_reader(NativeRead read);
// Only the Static main installs these callbacks. Source never links the runner.
using StaticRunner = void (*)(const std::vector<Code>&);
using StaticReserve = uint32_t (*)();
using StaticStart = void (*)(uint32_t, uint32_t);
void set_static_runtime(StaticRunner run, StaticReserve reserve, StaticStart start);
uint32_t static_memory_required();
void static_memory_start(uint32_t address, uint32_t bytes);

// Reads the file (missing is fine: no codes). `enabled_names` are the codes saved as on in the
// settings file; until the panel has saved a choice (`chosen`), the file's [Gecko_Enabled] is used.
void load(const std::string& path, const std::vector<std::string>& enabled_names, bool chosen);
const std::string& path();
std::vector<Code>& codes();
bool any_enabled();
// The Source Port runs no PowerPC. For one of Slippi's optional codes it carries as C (widescreen,
// screen shake, the L-cancel flash, Lagless FoD), the name of that built-in switch; else null. A
// code is recognised by its lines (the first line of those four, or the table below), never by
// what it is called.
// Also for any code made only of patches the Source Port carries as C (the General Codes, the
// optional codes above, PAL stock icons), matched patch by patch on the address and the exact
// words (kBuiltInUnits in user_gecko_targets.h).
const char* native_equivalent(const Code& c);

// Parses a pasted or typed code and appends it, so a player can bring in a code without editing
// GeckoCodes.ini by hand. `body` is the code's hex lines, one "XXXXXXXX YYYYYYYY" pair per line
// (blank lines and "*" note lines are fine and kept); if its first line is "$Name", that name is
// used instead of `name`. Returns empty on success, or why it was refused (a duplicate name, or no
// hex-pair line found). Does not write the file; call save() afterwards.
std::string add(const std::string& name, const std::string& body);
// Removes a code by name. Does not write the file; call save() afterwards.
void remove(const std::string& name);
// Writes every current code back to GeckoCodes.ini, in Dolphin's format, so the file on disk always
// matches what the panel shows (and stays readable by Dolphin or another tool). True on success.
bool save();
// Called once per game frame, as the game reads the pads (HLE(PADRead) on the Static Recomp, the
// host's pad_read on the Source Port), to apply the enabled codes' writes.
void apply();

}  // namespace user_gecko
