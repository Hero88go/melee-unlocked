// Pictures for the skin list: a costume file drawn as the fighter it holds.
//
// A skin is a fighter's model file. Unless its pack shipped a portrait, the Mods list had nothing
// to show for it but a grey figure. This draws the model in its rest pose, textured, from the
// front, into a small picture kept beside the catalog. Nothing here runs in a match.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace host {

// Draws the fighter in `dat` (a Pl??.dat costume file) into width x height RGBA pixels with a
// clear background. False when the file holds no model that can be read; a damaged or hostile
// file is refused, never followed out of its own bytes.
bool render_costume_picture(const uint8_t* dat, size_t size, int width, int height, std::vector<uint8_t>* rgba);

// The picture for one catalog skin: the PNG's path once it exists, "" until then. The first ask
// has it drawn on a background thread (one at a time); a skin that cannot be drawn is not asked
// for again this session.
std::string skin_thumbnail(const std::string& asset_id);
// The same for a standard costume, drawn from the disc's own file ("PlMsRe.dat").
std::string standard_costume_thumbnail(const std::string& costume_file_name);
void skin_thumbnails_shutdown();

}  // namespace host
