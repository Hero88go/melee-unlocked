// Slippi spectator server: port of Dolphin's SlippiSpectateServer (Core/Slippi/SlippiSpectate.cpp).
// The game's recording events go out over ENet on a local port (51441 by default) exactly as Slippi
// Dolphin sends them, so the Slippi Launcher's Broadcast tab and stream overlays that read Dolphin
// (slippi-js DolphinConnection) read this game the same way. Both engines feed it at the points
// Dolphin's EXI device does: the Static Recomp from the Slippi EXI device, the Source Port from its
// native recording stream.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace slippi::spectate {

// Starts the server thread when the setting is on (Slippi "EnableSpectator", on by default, and
// "SpectatorLocalPort", 51441). Called once at boot; a later call does nothing, as Dolphin creates
// its server once per run.
void start();
// Sends {"type":"end_game","dolphin_closed":true} and stops the thread (Dolphin's EXI device
// destructor, then the server's own).
void stop();
bool running();
uint16_t port();   // the port in use, 0 when not running

// Dolphin's write(), startGame() and endGame(). write() takes one recording event (command byte
// and payload) or a whole menu frame; thread-safe.
void write(const uint8_t* payload, uint32_t length);
void start_game();
void end_game(bool dolphin_closed = false);

}  // namespace slippi::spectate
