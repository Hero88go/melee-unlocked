// Linux build: XInput over SDL2's game controller API. Every controller SDL recognises (Xbox,
// PlayStation, Switch Pro, 8BitDo, generic pads with a mapping) fills one of the four XInput slots in
// connection order, with the Xbox layout SDL normalises to. port/linux/xinput_sdl.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"
#define XUSER_MAX_COUNT 4
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD 30
#define ERROR_DEVICE_NOT_CONNECTED 1167L
#define XINPUT_FLAG_GAMEPAD 1u
typedef struct _XINPUT_GAMEPAD { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; short sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct _XINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct _XINPUT_VIBRATION { WORD wLeftMotorSpeed, wRightMotorSpeed; } XINPUT_VIBRATION;
typedef struct _XINPUT_CAPABILITIES { BYTE Type, SubType; WORD Flags; XINPUT_GAMEPAD Gamepad; XINPUT_VIBRATION Vibration; } XINPUT_CAPABILITIES;
DWORD XInputGetState(DWORD index, XINPUT_STATE* state);
DWORD XInputSetState(DWORD index, XINPUT_VIBRATION* vibration);
DWORD XInputGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES* caps);
// The name SDL gives the pad in an XInput slot ("" when empty), for the controller list in settings.
namespace linux_compat { const char* xinput_name(DWORD index); bool xinput_is_gamecube_family(DWORD index); }
