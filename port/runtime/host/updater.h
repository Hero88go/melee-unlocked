// In-client updater: asks GitHub for the latest release of hero88go/melee-unlocked, and on request
// downloads its zip and hands over to a small batch script that swaps the files in after the
// game exits and relaunches. Everything network-side runs on a background thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>

namespace host::updater {
enum class State { Idle, Checking, UpToDate, UpdateAvailable, Downloading, ReadyToInstall, Failed };
struct Release { std::string version, published, notes; bool legacy = false; bool experimental = false; };   // published: YYYY-MM-DD; notes: the GitHub release text
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
// Unpacks a release zip in process (no tar.exe, which Wine and Proton lack). Exposed for the unit test.
bool unpack_archive(const std::string& zip_path_utf8, const std::string& dest_utf8);
// Unpacks a downloaded .zip or .tar.gz in process, refusing any entry that would land outside `dest`.
bool unpack_download(const std::string& archive_utf8, const std::string& dest_utf8);
// The batch script an update hands over to (exposed for the unit test): `self` and `others` are
// the processes it waits for, `relaunch` the command line it starts when the files are in place.
// `show_failure` opens a console window with the reason when the update cannot be put in place.
std::string install_script(const std::string& batch_dir, unsigned long self, const std::vector<unsigned long>& others,
                           const std::string& relaunch, bool show_failure);
// Game processes from this installation that must close before replacement.
std::vector<unsigned long> install_processes(const std::string& install_root_utf8);

// Downloads one file (redirects followed, never HTTPS to HTTP), refuses it unless its SHA-256 is
// `sha256` (lowercase hex), and only then puts it at `dest` (written beside it first). Blocking: call
// it from a worker thread. Fight a pro fetches Phillip's matchup policies with it.
bool fetch_verified(const std::string& url, const std::string& sha256, const std::string& dest_utf8, std::string* why);

// DLSS 5: NVIDIA's model is its own download (it would more than double every update). One click
// fetches it from the release, checks NVIDIA's file against its known SHA-256 and puts it beside the
// game as nvngx_dlssnr.dll, where updates leave it alone. Background thread; the game picks it up
// at its next start.
enum class ModelState { Missing, Present, Downloading, Failed };
bool dlss5_model_present(const std::string& game_dir_utf8);
void dlss5_model_download(const std::string& game_dir_utf8);
ModelState dlss5_model_state(const std::string& game_dir_utf8);
std::string dlss5_model_message();
constexpr const char* kDlss5ModelSize = "about 180 MB";

// The build for processors without AVX2 (the _compat programs), its own download since 0.9.98, so
// nobody else downloads it: the main package names its file and SHA-256 in compat.sha256, and the
// launcher fetches it on a PC that needs it, again after each update (compat.installed records which
// one is beside the game). Background thread.
bool compat_build_needed(const std::string& game_dir_utf8);
void compat_build_download(const std::string& game_dir_utf8);
ModelState compat_build_state(const std::string& game_dir_utf8);
std::string compat_build_message();

// Intel XeSS (libxess.dll), its own download since 0.9.98: PC settings > Video has the button. The
// file is checked against Intel's build before it goes beside the game; the game loads it at its
// next start. Background thread.
bool xess_present(const std::string& game_dir_utf8);
void xess_download(const std::string& game_dir_utf8);
ModelState xess_state(const std::string& game_dir_utf8);
std::string xess_message();
constexpr const char* kXessSize = "about 60 MB";
}  // namespace host::updater
