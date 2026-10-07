// Slippi Jukebox: the netplay codes silence the game's own music and ask the EXI device to play
// the stage/menu HPS track instead (offset + size on the disc). This decodes the HPS (DSP-ADPCM,
// stereo, looping) and hands samples to the host audio mixer.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace slippi::jukebox {
void start_song(uint32_t disc_offset, uint32_t size);   // CMD_PLAY_MUSIC
void stop();                                            // CMD_STOP_MUSIC
// Where songs are read from: the disc image by default. The Source Port's reader also serves its
// mod overlay, so a replaced song plays through the jukebox as it does in the game.
using DiscReader = bool (*)(uint32_t offset, void* dst, uint32_t size);
void set_disc_reader(DiscReader reader);
// Optional user audio replacement, chosen by the original disc offset. Return false for vanilla.
using MusicPackReader = bool (*)(uint32_t offset, std::filesystem::path* file);
void set_music_pack_reader(MusicPackReader reader);
bool resolve_music_pack_path(const std::string& disc_path, std::filesystem::path* file);
void open_music_packs_folder();
void set_music_packs_enabled(bool enabled);
bool music_packs_enabled();
void set_melee_volume(uint8_t volume);                  // CMD_CHANGE_MUSIC_VOLUME (0..254)
void set_user_volume(int percent);                      // PC settings "Music" (0..100)
// A gain for the song about to start (1 = unchanged); start_song() takes it for that song only.
void set_next_song_gain(float gain);
int user_volume();
// A replay viewer that is paused or seeking: the music stops where it is and goes on from there.
void set_paused(bool paused);
// Mixes `frames` stereo 32 kHz samples into `out` (adds to what is there). Audio-thread safe.
// `master` is the Volume setting as a fraction, applied on top of Melee's own music volume and the
// Music slider. Without it the master volume only ever gated the music on or off, because the
// caller scaled its own PCM and then had the music added underneath at full level: turning Volume
// down quietened the game and left the music where it was.
// output_rate: the device's sample rate divided by the clock-tracking ratio, so music follows the
// same clock correction as the game's sound.
void mix(int16_t* out, size_t frames, double master, double output_rate = 32000.0);
}  // namespace slippi::jukebox
