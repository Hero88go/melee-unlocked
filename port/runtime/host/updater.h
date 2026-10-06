// In-client updater: asks GitHub for the latest release of hero88go/melee-unlocked, and on request
// downloads its zip and hands over to a small batch script that swaps the files in after the
// game exits and relaunches. Everything network-side runs on a background thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>

namespace host::updater {
enum class State { Idle, Checking, UpToDate, UpdateAvailable, Downloading, ReadyToInstall, Failed };
struct Release { std::string version, published; bool legacy = false; bool experimental = false; };   // published: YYYY-MM-DD
enum class RollbackState { Idle, Downloading, Ready, Failed };
void check(const std::string& current_version, bool install_experimental = false); // true also offers the current version's complete experimental zip
State state();
std::string latest_version();                     // tag of the newest release when known (without the leading v)
std::string message();                            // short status text for the settings panel
std::vector<Release> releases();                  // published releases with downloadable Windows builds
RollbackState rollback_state();
std::string rollback_message();
std::string rollback_folder();                    // ready, validated side-by-side install
bool install_release(const std::string& version, bool experimental, const std::string& install_root);
void download_and_install();                      // background download, then writes update.bat and exits the game to run it
void shutdown();                                  // joins the background thread; call before process exit
// The checks a download passes before it is installed (exposed for the unit test): the SHA-256 the
// release lists ("" = none listed, size check only), and every archive entry inside `root`.
bool download_matches(const std::string& body, const std::string& sha256_hex);
bool archive_inside(const std::string& zip_path_utf8, const std::string& root);
// Game processes from this installation that must close before replacement.
std::vector<unsigned long> install_processes(const std::string& install_root_utf8);
}  // namespace host::updater
