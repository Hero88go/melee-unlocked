// Manual smoke only, on a spare Mac with explicit authorization. NOT an automated window test.
// Hidden parent until "front". Starts one owned child with a fresh, explicit environment.
#import <AppKit/AppKit.h>
#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "Usage: embed-test-parent /absolute/host /absolute/fresh-scratch [host args...]\n"
        "Manual only. Starts hidden. Commands: front, hidewin, mini, unmini, closewin,\n"
        "move X Y (Cocoa points), size W H (content points), send rect/show/hide/focus/blur/quit, exit\n");
    return 2;
  }
  @autoreleasepool {
    NSString* exe = [NSString stringWithUTF8String:argv[1]];
    NSString* scratch = [NSString stringWithUTF8String:argv[2]];
    if (!exe.isAbsolutePath || !scratch.isAbsolutePath) return 2;
    NSError* error = nil;
    // Require a new scratch directory: do not reuse any real player profile.
    if ([NSFileManager.defaultManager fileExistsAtPath:scratch] ||
        ![NSFileManager.defaultManager createDirectoryAtPath:scratch withIntermediateDirectories:YES
            attributes:nil error:&error]) {
      std::fprintf(stderr, "scratch must be a new writable absolute directory\n");
      return 2;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    static NSWindow* parent = [[NSWindow alloc] initWithContentRect:NSMakeRect(180,180,1000,700)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    parent.releasedWhenClosed = NO;
    parent.title = @"YouGame embed stand-in (manual test)";
    parent.backgroundColor = NSColor.darkGrayColor;
    // Mark the default rect: points from frame top-left, including the title bar.
    const CGFloat top = parent.frame.size.height - parent.contentView.frame.size.height;
    NSBox* box = [[NSBox alloc] initWithFrame:NSMakeRect(40,160,800,500)];
    box.title = @"Player box";
    [parent.contentView addSubview:box];
    [box release];

    NSTask* child = [[NSTask alloc] init];
    child.executableURL = [NSURL fileURLWithPath:exe];
    child.currentDirectoryURL = [NSURL fileURLWithPath:scratch];
    NSMutableDictionary* environment = [@{@"PATH":@"/usr/bin:/bin:/usr/sbin:/sbin", @"LANG":@"C",
        @"LC_ALL":@"C", @"YOUGAME_STDIO":@"1", @"YOUGAME_EMBED":@"1",
        @"YOUGAME_EMBED_PARENT":[NSString stringWithFormat:@"%ld", (long)parent.windowNumber],
        @"SDL_AUDIODRIVER":@"dummy", @"SDL_MAC_BACKGROUND_APP":@"1", @"ROSETTA_ADVERTISE_AVX":@"1"}
        mutableCopy];
    for (NSString* name in @[@"HOME", @"XDG_DATA_HOME", @"XDG_CONFIG_HOME", @"XDG_CACHE_HOME",
                             @"XDG_STATE_HOME", @"XDG_RUNTIME_DIR", @"APPDATA", @"LOCALAPPDATA", @"TMPDIR"]) {
      NSString* path = [scratch stringByAppendingPathComponent:name];
      [NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES
          attributes:nil error:&error];
      environment[name] = path;
    }
    child.environment = environment;
    [environment release];
    NSMutableArray* arguments = [NSMutableArray array];
    for (int i = 3; i < argc; ++i) [arguments addObject:[NSString stringWithUTF8String:argv[i]]];
    child.arguments = arguments;
    NSPipe* input = [NSPipe pipe];
    child.standardInput = input;
    // Child stdout goes directly to this terminal: observe the yg key relay without a reader.
    child.standardOutput = NSFileHandle.fileHandleWithStandardOutput;
    child.standardError = NSFileHandle.fileHandleWithStandardError;
    if (![child launchAndReturnError:&error]) {
      std::fprintf(stderr, "could not start owned test child: %s\n", error.localizedDescription.UTF8String);
      [parent close]; [parent release]; [child release];
      return 1;
    }
    std::fprintf(stderr, "parent CGWindowID=%ld (hidden); default: send rect 40 %.0f 800 500\n",
                 (long)parent.windowNumber, top + 40);
    std::fflush(stderr);
    // Read fd 0 directly so no stdio lock is held while the AppKit thread logs or exits.
    std::thread([input] {
      std::string pending;
      char buffer[1024];
      ssize_t count;
      while ((count = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < count; ++i) {
          if (buffer[i] != '\n') { if (pending.size() < 4096) pending += buffer[i]; continue; }
          @autoreleasepool {
          NSString* line = [NSString stringWithUTF8String:pending.c_str()];
          pending.clear();
          dispatch_async(dispatch_get_main_queue(), ^{
            if ([line hasPrefix:@"send "]) {
              NSString* cmd = [@"embed " stringByAppendingString:[line substringFromIndex:5]];
              @try { [input.fileHandleForWriting writeData:[[cmd stringByAppendingString:@"\n"]
                  dataUsingEncoding:NSUTF8StringEncoding]]; } @catch (NSException*) { }
            } else if ([line isEqualToString:@"front"]) {
              [parent makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
            } else if ([line isEqualToString:@"hidewin"]) [parent orderOut:nil];
            else if ([line isEqualToString:@"mini"]) [parent miniaturize:nil];
            else if ([line isEqualToString:@"unmini"]) [parent deminiaturize:nil];
            else if ([line isEqualToString:@"closewin"]) {
              [parent close]; [parent release]; parent = nil;
            }
            else if ([line isEqualToString:@"exit"]) { [NSApp stop:nil]; }
            else {
              double x, y;
              if (std::sscanf(line.UTF8String, "move %lf %lf", &x, &y) == 2)
                [parent setFrameOrigin:NSMakePoint(x,y)];
              else if (std::sscanf(line.UTF8String, "size %lf %lf", &x, &y) == 2 && x > 0 && y > 0)
                [parent setContentSize:NSMakeSize(x,y)];
            }
          });
          }
        }
      }
      dispatch_async(dispatch_get_main_queue(), ^{ [NSApp stop:nil]; });
    }).detach();
    [NSApp run];
    @try { [input.fileHandleForWriting writeData:[@"embed quit\n" dataUsingEncoding:NSUTF8StringEncoding]]; }
    @catch (NSException*) { }
    // The helper only signals the child it created. It never searches or kills other processes.
    if (child.running) [child terminate];
    [parent close]; [parent release]; parent = nil;
    // Detached input thread exits with the process; retained objects stay valid until then.
    return 0;
  }
}
