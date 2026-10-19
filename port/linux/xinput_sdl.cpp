// Linux build: XInput's four slots over SDL2's game controllers (port/linux/compat/xinput.h).
// The GameCube adapter is left out: the game reads it directly over libusb (gc_adapter.cpp), and
// SDL's own GameCube driver would otherwise report the same controllers a second time.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "xinput.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {
struct Slot { SDL_GameController* pad = nullptr; SDL_JoystickID id = -1; DWORD packet = 0; std::string name; uint16_t vendor = 0, product = 0; };
std::mutex g_mutex;
Slot g_slots[XUSER_MAX_COUNT];
std::chrono::steady_clock::time_point g_next_scan{};
bool g_ready = false;

bool skipped(uint16_t vendor, uint16_t product) {
  return vendor == 0x057E && product == 0x0337;   // Nintendo's Wii U / Switch GameCube adapter
}

void ensure_init() {
  if (g_ready) return;
  // Joystick events are read from any thread; the window thread also pumps them.
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "0");
  if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER)) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
  g_ready = true;
}

// Closes slots whose pad went away and fills free slots with newly attached pads, in SDL's order.
void rescan_locked() {
  const auto now = std::chrono::steady_clock::now();
  if (now < g_next_scan) return;
  g_next_scan = now + std::chrono::milliseconds(500);
  SDL_GameControllerUpdate();
  for (auto& s : g_slots)
    if (s.pad && !SDL_GameControllerGetAttached(s.pad)) { SDL_GameControllerClose(s.pad); s = Slot{}; }
  const int count = SDL_NumJoysticks();
  for (int i = 0; i < count; ++i) {
    if (!SDL_IsGameController(i)) continue;
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
    if (std::any_of(std::begin(g_slots), std::end(g_slots), [&](const Slot& s) { return s.pad && s.id == id; })) continue;
    const uint16_t vendor = SDL_JoystickGetDeviceVendor(i), product = SDL_JoystickGetDeviceProduct(i);
    if (skipped(vendor, product)) continue;
    auto free_slot = std::find_if(std::begin(g_slots), std::end(g_slots), [](const Slot& s) { return !s.pad; });
    if (free_slot == std::end(g_slots)) break;
    SDL_GameController* pad = SDL_GameControllerOpen(i);
    if (!pad) continue;
    free_slot->pad = pad; free_slot->id = id; free_slot->vendor = vendor; free_slot->product = product;
    const char* name = SDL_GameControllerName(pad);
    free_slot->name = name ? name : "Controller";
  }
}

short axis(SDL_GameController* pad, SDL_GameControllerAxis a, bool invert) {
  int v = SDL_GameControllerGetAxis(pad, a);
  if (invert) v = -v - 1;
  return (short)std::clamp(v, -32768, 32767);
}
BYTE trigger(SDL_GameController* pad, SDL_GameControllerAxis a) {
  return (BYTE)std::clamp(SDL_GameControllerGetAxis(pad, a) * 255 / 32767, 0, 255);
}
}  // namespace

DWORD XInputGetState(DWORD index, XINPUT_STATE* state) {
  if (index >= XUSER_MAX_COUNT || !state) return ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_init();
  rescan_locked();
  Slot& s = g_slots[index];
  if (!s.pad) return ERROR_DEVICE_NOT_CONNECTED;
  SDL_GameController* p = s.pad;
  XINPUT_GAMEPAD g{};
  auto bit = [&](SDL_GameControllerButton b, WORD mask) { if (SDL_GameControllerGetButton(p, b)) g.wButtons |= mask; };
  bit(SDL_CONTROLLER_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP);
  bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN);
  bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT);
  bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT);
  bit(SDL_CONTROLLER_BUTTON_START, XINPUT_GAMEPAD_START);
  bit(SDL_CONTROLLER_BUTTON_BACK, XINPUT_GAMEPAD_BACK);
  bit(SDL_CONTROLLER_BUTTON_LEFTSTICK, XINPUT_GAMEPAD_LEFT_THUMB);
  bit(SDL_CONTROLLER_BUTTON_RIGHTSTICK, XINPUT_GAMEPAD_RIGHT_THUMB);
  bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER);
  bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER);
  bit(SDL_CONTROLLER_BUTTON_A, XINPUT_GAMEPAD_A);
  bit(SDL_CONTROLLER_BUTTON_B, XINPUT_GAMEPAD_B);
  bit(SDL_CONTROLLER_BUTTON_X, XINPUT_GAMEPAD_X);
  bit(SDL_CONTROLLER_BUTTON_Y, XINPUT_GAMEPAD_Y);
  g.bLeftTrigger = trigger(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
  g.bRightTrigger = trigger(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  // SDL's Y axes grow downward; XInput's grow upward.
  g.sThumbLX = axis(p, SDL_CONTROLLER_AXIS_LEFTX, false);
  g.sThumbLY = axis(p, SDL_CONTROLLER_AXIS_LEFTY, true);
  g.sThumbRX = axis(p, SDL_CONTROLLER_AXIS_RIGHTX, false);
  g.sThumbRY = axis(p, SDL_CONTROLLER_AXIS_RIGHTY, true);
  if (std::memcmp(&g, &state->Gamepad, sizeof g) != 0) ++s.packet;
  state->dwPacketNumber = s.packet;
  state->Gamepad = g;
  return ERROR_SUCCESS;
}

DWORD XInputSetState(DWORD index, XINPUT_VIBRATION* vibration) {
  if (index >= XUSER_MAX_COUNT || !vibration) return ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_init();
  Slot& s = g_slots[index];
  if (!s.pad) return ERROR_DEVICE_NOT_CONNECTED;
  // XInput holds a level until told otherwise; SDL needs a duration, so a long one is renewed by
  // each call and a zero level stops it.
  SDL_GameControllerRumble(s.pad, vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed,
                           (vibration->wLeftMotorSpeed || vibration->wRightMotorSpeed) ? 10000 : 0);
  return ERROR_SUCCESS;
}

DWORD XInputGetCapabilities(DWORD index, DWORD, XINPUT_CAPABILITIES* caps) {
  if (index >= XUSER_MAX_COUNT || !caps) return ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_init();
  rescan_locked();
  if (!g_slots[index].pad) return ERROR_DEVICE_NOT_CONNECTED;
  std::memset(caps, 0, sizeof *caps);
  caps->Type = 1; caps->SubType = 1;
  caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = SDL_GameControllerHasRumble(g_slots[index].pad) ? 0xFFFF : 0;
  return ERROR_SUCCESS;
}

const char* linux_compat::xinput_name(DWORD index) {
  if (index >= XUSER_MAX_COUNT) return "";
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_slots[index].pad ? g_slots[index].name.c_str() : "";
}

bool linux_compat::xinput_is_gamecube_family(DWORD index) {
  if (index >= XUSER_MAX_COUNT) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  const Slot& s = g_slots[index];
  return s.pad && s.vendor == 0x057E;
}
