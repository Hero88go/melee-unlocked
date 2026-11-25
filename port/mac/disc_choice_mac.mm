// The open panel for a start from Finder (disc_choice.h).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// AppKit starts through SDL here, as it does for the game window a moment later (SDL makes the
// application object, its menu and its Dock icon, and the second start only counts up). The panel
// then belongs to Melee Unlocked itself and comes to the front of a start from Finder.
#import <AppKit/AppKit.h>
#include <SDL.h>
#include "disc_choice.h"

namespace {
NSString* const need_disc =
    @"Melee Unlocked needs your own copy of Super Smash Bros. Melee (NTSC 1.02, an .iso file) to start.";

// Cancel in the panel: say what the game needs, then Choose Disc (the panel again) or Quit.
bool choose_again() {
  NSAlert* alert = [[[NSAlert alloc] init] autorelease];
  alert.messageText = @"Melee Unlocked";
  alert.informativeText = need_disc;
  [alert addButtonWithTitle:@"Choose Disc"];
  [alert addButtonWithTitle:@"Quit"];
  return [alert runModal] == NSAlertFirstButtonReturn;
}
}  // namespace

std::filesystem::path mac_host::choose_disc_panel() {
  @autoreleasepool {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
      std::fprintf(stderr, "cannot start SDL video for the disc panel: %s\n", SDL_GetError());
      return {};
    }
    [NSApp activateIgnoringOtherApps:YES];
    for (;;) {
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      panel.message = @"Choose your Super Smash Bros. Melee NTSC 1.02 disc (ISO)";
      panel.prompt = @"Choose";
      panel.canChooseFiles = YES;
      panel.canChooseDirectories = NO;
      panel.allowsMultipleSelection = NO;
      if ([panel runModal] != NSModalResponseOK || panel.URL == nil) {
        if (choose_again()) continue;
        return {};
      }
      const std::filesystem::path chosen = panel.URL.path.fileSystemRepresentation;
      if (mac_host::is_melee_102(chosen)) return chosen;
      NSAlert* alert = [[[NSAlert alloc] init] autorelease];
      alert.alertStyle = NSAlertStyleWarning;
      alert.messageText = @"That file is not a Super Smash Bros. Melee NTSC 1.02 disc image.";
      alert.informativeText = @"Melee Unlocked needs your own copy of Melee, version 1.02 (game ID GALE01), as an .iso "
                              @"file. Choose it again.";
      [alert runModal];
    }
  }
}
