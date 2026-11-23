/*
 * yougame_start.h - the YouGame start hook. One call when your game starts.
 *
 *   #define YOUGAME_START_IMPLEMENTATION      (in exactly one .c or .cpp file)
 *   #include "yougame_start.h"
 *
 *   yg_start_options o = {0};
 *   o.game = "your-game-slug";               the game's address on YouGame: yougame.co/g/<slug>
 *   o.tray_from = "0.13.1407";               first shipped quiet-start version for the target OS
 *   yg_start(&o);                            then carry on starting the game, whatever it returns
 *
 * When to ask is the game's choice (o.ask). At start (the default, YG_START_ASK_AT_START): the
 * call above, at launch; the small window shows then if YouGame isn't ready. On first use: at
 * launch call with o.ask = YG_START_ASK_LATER (an installed YouGame starts quietly in the tray, and
 * no window ever shows), then, when the player presses one of the game's YouGame features (Online,
 * Friends, Ranked) and YouGame isn't connected, call with o.ask = YG_START_ASK_ON_USE: the same
 * checks and small window, whose Play offline goes back to the game. It is never remembered: the
 * next press asks again. Pick at start for a game that needs YouGame to run at all (controller
 * support, sign-in to play); on first use when online is an extra, so nothing covers the game until
 * the player reaches for it.
 *
 * It never brings up YouGame's client window or a game page, and never brings a running YouGame
 * forward. What it does, in this order:
 *   1. YouGame is running for this user: nothing is shown. YG_START_RUNNING.
 *   U. Windows: YouGame is not running and has just quit to install an update (its marker file
 *      %APPDATA%\<profile>\updating.json, written by the app, is valid: see "The update marker"
 *      below), and the marker file is under 20 seconds old or its version is not installed: nothing is started
 *      and nothing is shown. YG_START_UPDATING, with the version it is installing in `update_to`
 *      (when a buffer is given; with `update_to_magic` it is also cleared at entry). The game shows its own
 *      "YouGame is updating" line and calls yg_start again about every 10 seconds, for at most
 *      5 minutes. Also while it waits for a
 *      YouGame it started (`open_wait_ms`): an old YouGame that finds an update quits to install it,
 *      and the hook returns YG_START_UPDATING the moment the marker appears, not "didn't open".
 *   2. YouGame is installed but not running.
 *      Windows, when `no_open` is 0, `tray_from` is set, the registered program is the signed
 *      YouGame program at its install location (%LOCALAPPDATA%\Programs\<folder>\<exe>, its version
 *      texts naming the YouGame app, signed by Fluo Inc. through Microsoft's identity verified CA),
 *      its file version is at least `tray_from`, and the game does not run as administrator: it
 *      starts that same file (checked again once started, before it runs) with the tray link
 *      yougame[-staging]://tray, so YouGame starts with no window, only its tray icon.
 *      macOS checks its bundle identity, version and Developer ID signature, then uses
 *      /usr/bin/open -g -j -a <bundle> --args --yougame-tray: hidden, in the background, menu bar
 *      icon only (Dock icon kept). The update-marker branch remains Windows-only.
 *      YG_START_OPENED. With `open_wait_ms` set and YouGame not up in that time, the small window
 *      below says "YouGame didn't open".
 *      Otherwise nothing is started and the small window says why: "YouGame needs an update" when
 *      the installed version is older than `tray_from` (or its file version cannot be read), else
 *      "YouGame isn't running" (also when its identity cannot be read). Linux always takes this
 *      path: a Linux desktop may have no tray.
 *   3. YouGame is not on this computer: the small window says "Play online with YouGame"
 *      ("Play <game> online" with a name), with "Get YouGame" and the brand "YouGame".
 * The small window is YouGame's own, with two buttons. On Windows, for a first install, pressing
 * "Get YouGame" closes it at once and returns YG_START_GETTING. The game keeps running offline.
 * A worker downloads the site's Windows start installer over HTTPS, checks it is whole and signed
 * by Fluo Inc. (the same check as the installed program), runs it silently (per-user, never elevated,
 * no wizard), then starts YouGame with the checked tray start above, at any version. No worker step
 * uses the closed window.
 * The game polls yg_start_get_progress, even every frame, and draws its own progress line:
 * YG_START_GET_NONE (0), DOWNLOADING (1, percent 0-100), INSTALLING (2), READY (3, installed and
 * started in the tray, or found running), or FAILED (4, the download page was opened). The worker
 * opens the same game's download page on failure. While the worker runs, another yg_start call
 * returns YG_START_GETTING without a window or another installer. The worker lives in the game
 * process: the game must keep running for it to finish. macOS, Linux and other systems always
 * report YG_START_GET_NONE. With background installation disabled, an installed copy, or a game
 * running as administrator, and on macOS and Linux, Get opens the download page (YG_START_OFFERED).
 * "Play offline" or closing the small window returns YG_START_DECLINED. It waits only for the
 * player's answer, so the game starts as usual.
 *
 * It only looks and starts; it does not talk to YouGame. Online calls are yougame.h's job (or the
 * game's own client): they find YouGame once it is running, with no restart of the game.
 *
 * Link flags for this header alone:
 *   Windows/MinGW: -ladvapi32 -lshell32 -luser32 -lgdi32 (MSVC: pragmas below name the same .lib).
 *     wintrust.dll, crypt32.dll, version.dll, ole32.dll, userenv.dll, winhttp.dll (and shell32.dll's
 *     SHGetKnownFolderPath) are loaded at run time from the system directory
 *     (LOAD_LIBRARY_SEARCH_SYSTEM32), never from the game's folder, unless the game itself already
 *     loaded a copy of that name.
 *   macOS: -framework CoreServices -framework CoreFoundation.
 *   Linux: -pthread. Its small window uses /usr/bin/zenity, else /usr/bin/kdialog; with neither
 *     there is no window and the result is YG_START_DECLINED.
 * It downloads only after the player presses Get YouGame on Windows: the installer, from
 * https://yougame.co /api/desktop/download?file=YouGame-Start-windows-x64.exe (staging:
 * https://staging.yougame.co, same path), which each site answers with the full YouGame app's
 * installer, into
 * %LOCALAPPDATA%\YouGame Start, removed after it ran. The only programs it starts, after the
 * player's answer or the checks above: on Windows the signed YouGame program at its install location,
 * with the tray link (not when the game runs as administrator), and that installer once its
 * Fluo Inc. signature checks out, with /S; on Mac codesign and open for the verified bundle; everywhere the system's opener for one fixed https address (the
 * download page); on Linux also zenity or kdialog (the small window). All by absolute path. On macOS and Linux a game
 * that ignores SIGCHLD or reaps every child itself makes those answers read as failures.
 *
 * Runs that must never show a window or start anything (tests, headless runs): set `quiet`, or the
 * environment variable YOUGAME_START_QUIET=1. Then it only looks (a pipe or socket, the registry or
 * the system's link handler, the update marker and the installed version) and reports what it
 * found; it starts no program on any system.
 *
 * The update marker (Windows): right before YouGame quits to install an update it writes one line of
 * UTF-8 JSON, {"v":1,"from":"0.13.1412","to":"0.13.1415","at":1763106830000} (`at` = epoch ms when it
 * quit), to updating.json in its profile folder (Electron's userData) under FOLDERID_RoamingAppData.
 * Production: "yougame-desktop" (the packaged app's folder) first, then "YouGame" (should a later
 * build add a productName); the first valid one wins (see yg__start_profiles). Staging: "YouGame
 * Staging" first, then "YouGame Staging (agent build)" (its name before 11-15). The hook only reads it, never writes or deletes it, and only on a
 * fixed local drive: a Roaming folder redirected to a network share or a removable drive is never
 * opened, so it cannot stall the game. It takes the marker only when it is a regular
 * file under 1 KiB, `v` is 1, `at` is at most 5 minutes old and at most 60 s ahead of this
 * computer's clock, and `to` is digits and dots (at most 32 characters). When the installed YouGame
 * is already at `to` or newer (compared number by number), the marker is a leftover of a finished
 * update and is ignored once the marker file is at least 20 seconds old (installer post-steps may
 * still run). Anything else, malformed included, is no marker: today's behaviour. macOS and Linux have no marker yet, so YG_START_UPDATING never comes back there.
 *
 * MIT licence (see LICENSE beside this file).
 */
/* Strict C (-std=c99) hides glibc's POSIX declarations, which the implementation needs: include this
   header before libc headers in that one file. Only then, and only in strict mode, is the feature
   macro set, so a gnu/C++ build and every other file keep glibc's defaults untouched. */
#if defined(__linux__) && defined(YOUGAME_START_IMPLEMENTATION) && defined(__STRICT_ANSI__) && \
    !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef YOUGAME_START_H
#define YOUGAME_START_H

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum yg_start_result {
  YG_START_RUNNING = 0,     /* YouGame is running: nothing was shown */
  YG_START_OPENED = 1,      /* Windows/macOS: installed YouGame was started quietly */
  YG_START_OFFERED = 2,     /* the small window was shown and the player pressed Get YouGame;
                               the download page is open */
  YG_START_DECLINED = 3,    /* the small window was shown and the player pressed Play offline (or closed
                               it), or no window could be shown */
  YG_START_UNAVAILABLE = 4, /* nothing was done: unsupported system, bad options, or an error */
  YG_START_INSTALLED = 5,   /* quiet runs only: installed but not running; nothing was started */
  YG_START_MISSING = 6,     /* quiet runs only: not installed, and nothing was shown */
  YG_START_UPDATING = 7,    /* Windows: YouGame quit to install an update (its marker is valid):
                               nothing was started or shown. Show the game's own "YouGame is
                               updating" line and call yg_start again in about 10 s, for at most
                               5 minutes. The version it installs is in update_to, when given. */
  YG_START_GETTING = 8      /* Get YouGame was pressed: YouGame is downloading and installing in the
                               background; the game keeps running. Poll yg_start_get_progress. */
} yg_start_result;

/* yg_start_options.ask: when the game asks for YouGame. The game decides; the hook never does. */
#define YG_START_ASK_AT_START 0 /* at game start; the small window shows if YouGame isn't ready.
                                   The default: a game that never sets ask gets this */
#define YG_START_ASK_LATER 1    /* at game start, for a game that asks on first use: an installed
                                   YouGame is started quietly (the same checks as at start) and no
                                   window ever shows. YG_START_RUNNING, OPENED or UPDATING, else
                                   YG_START_INSTALLED (installed, not running) or YG_START_MISSING */
#define YG_START_ASK_ON_USE 2   /* the player just pressed one of the game's YouGame features: the
                                   same checks and window as at start */

/* "YGU1": with it, update_to is also cleared at entry (empty on every result but YG_START_UPDATING). */
#define YG_START_UPDATE_TO_MAGIC 0x59475531u

typedef struct yg_start_options {
  const char *game;      /* required: the game's slug, a-z 0-9 and '-', at most 80 characters */
  int staging;           /* 1 = YouGame's staging app and site (test builds); 0 = YouGame */
  int quiet;             /* 1 = never show a window and never start anything; only report */
  unsigned open_wait_ms; /* Windows, after starting YouGame in the tray: wait up to this long for it
                            to be running (0 = return at once, the usual choice; at most 60000). If it
                            is not running by then, the small window says "YouGame didn't open". */
  int no_open;           /* 1 = never start an installed YouGame on its own; show the small window
                            instead (Windows: its Open YouGame button still starts it, the player's choice) */
  const char *tray_from; /* Windows: the first YouGame version that starts hidden in the tray, as
                            "0.13.1407". An installed YouGame older than this is never started (it
                            would show its whole window); the small window says it needs an update.
                            NULL (or not three dot-separated numbers) = never start it on its own
                            (Windows: Open YouGame in the small window starts it at any version). */
  /* Last, so a caller that zeroes the struct (= {0} or memset) keeps working.
     Give a valid buffer and size, or leave both zero. On YG_START_UPDATING the version is written,
     magic or not; with update_to_magic = YG_START_UPDATE_TO_MAGIC it is also cleared at entry. */
  char *update_to;          /* optional: cleared at entry (with the magic); on YG_START_UPDATING, the version YouGame
                               is installing (at most 32 characters), cut to fit, always NUL-terminated */
  size_t update_to_cap;     /* buffer size including terminator (0 = not written) */
  unsigned update_to_magic; /* YG_START_UPDATE_TO_MAGIC: also clear update_to at entry */
  int ask;                  /* when the game asks: YG_START_ASK_AT_START (0, the default),
                               YG_START_ASK_LATER or YG_START_ASK_ON_USE; any other value: at start */
  const char *name;         /* optional: the game's name as players know it ("Melee Unlocked"), shown
                               in the window ("Play Melee Unlocked online"). 1 to 40 characters:
                               letters, digits, spaces and . , ' - : ! & + ( ). Else "this game". */
  int needs;                /* 1 = the game needs YouGame to run at all (no offline play): the window's
                               other button says Quit, it asks at every start, and on
                               YG_START_DECLINED the game quits. 0 = online is an extra (the usual) */
} yg_start_options;

/* Call on the thread that starts the game, before or after the game's own window exists; again
   with YG_START_ASK_ON_USE when the player presses something that needs YouGame. */
yg_start_result yg_start(const yg_start_options *options);

/* Background first install on Windows, for this game process. Safe and cheap to poll every frame.
   percent may be NULL; written only while DOWNLOADING. Other systems always return NONE. */
#define YG_START_GET_NONE 0
#define YG_START_GET_DOWNLOADING 1
#define YG_START_GET_INSTALLING 2
#define YG_START_GET_READY 3
#define YG_START_GET_FAILED 4
int yg_start_get_progress(int *percent);

#ifdef __cplusplus
}
#endif

#ifdef YOUGAME_START_IMPLEMENTATION

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Raised, for the start hook on Windows: the game holds rights the player's own programs don't have.
   - A full token from UAC (TokenElevationTypeFull, "Run as administrator"): raised.
   - Any other elevated token: raised, unless the player's shell (the desktop's Explorer) is the
     same user and elevated too. Then every program the player starts, YouGame included, already
     runs with those rights, so starting YouGame from the game gives it nothing new: UAC turned off
     (Windows Sandbox runs this way; on a test machine the old rule refused every start) or the built-in
     Administrator with admin approval off. Another user's token (runas), a service or a SYSTEM
     task (no shell on their desktop) stays raised.
   Pure, so the seam tests check it on every system. */
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
static int yg__start_raised_rule(int full, int elevated, int shell_peer) {
  return full || (elevated && !shell_peer);
}
#endif

/* Every text the small window shows, on every system. Plain ASCII only. The game's name (`name`)
   goes where the mock says "Melee Unlocked"; without one (or one that isn't plain text) the window
   says "this game". Never "the free app": YouGame is "a free app" at most. */
#define YG__START_TEXT_MISSING 0     /* not installed */
#define YG__START_TEXT_DIDNT_OPEN 1  /* started in the tray, but not running within open_wait_ms */
#define YG__START_TEXT_UPDATE 2      /* installed, older than tray_from (or its version unreadable) */
#define YG__START_TEXT_NOT_RUNNING 3 /* installed, not started by the hook */
#define YG__START_TEXT_RAISED 4      /* Windows: the game runs as administrator, so nothing is started */
#define YG__START_TEXTS 5
#define YG__START_GET "Get YouGame"
#define YG__START_UPDATE "Update YouGame"
#define YG__START_LATER "Play offline"
#define YG__START_OPEN "Open YouGame"
#define YG__START_QUIT "Quit"
/* Get YouGame installs in the background (Windows); a game can turn that off with
   #define YG__START_GET_INSTALL 0, and then Get opens the download page. */
#ifndef YG__START_GET_INSTALL
#define YG__START_GET_INSTALL 1
#endif
/* yg__start_offer's answers. Open is offered (Windows) when YouGame is installed and the game can
   start it: "isn't running" and "didn't open". */
#define YG__START_ANSWER_LATER 0
#define YG__START_ANSWER_GET 1
#define YG__START_ANSWER_OPEN 2
#define YG__START_ANSWER_NONE (-1) /* nothing was shown, or the game closed meanwhile: no choice made */
#define YG__START_ANSWER_GETTING 4 /* Windows: the background installer was started */
#define YG__START_OFFERS_OPEN(text) ((text) == YG__START_TEXT_NOT_RUNNING || (text) == YG__START_TEXT_DIDNT_OPEN)
#define YG__START_NAME_MAX 40
#define YG__START_TITLE_CAP 96
#define YG__START_BODY_CAP 240

/* One window, every word of it: what yg__start_offer draws (Windows) or hands to the system's box. */
typedef struct yg__start_win {
  int which;   /* YG__START_TEXT_* */
  int on_use;  /* asked on use (the player pressed a YouGame feature) */
  int facts;   /* show the Matchmaking / Friends / Ranked row */
  int answer;  /* what the main button answers: YG__START_ANSWER_GET or _OPEN; _LATER = one button */
  int staging;            /* the site's flavour, copied to the worker when Get is pressed */
  const char *game;        /* checked slug, copied to the worker before the window returns */
  char title[YG__START_TITLE_CAP];
  char body[YG__START_BODY_CAP];
  const char *main_label;  /* the main (white) button */
  const char *later_label; /* the other button: Play offline, or Quit for a game that needs YouGame */
} yg__start_win;

/* The game's name as the window may print it: 1 to 40 characters of letters, digits, spaces and
   . , ' - : ! & + ( ), not starting or ending with a space. Anything else: NULL ("this game"). */
static const char *yg__start_name_ok(const char *name) {
  size_t n = 0;
  if (!name || !name[0] || name[0] == ' ') return NULL;
  for (; name[n]; ++n) {
    unsigned char c = (unsigned char)name[n];
    if (n >= YG__START_NAME_MAX) return NULL;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
          c == '.' || c == ',' || c == '\'' || c == '-' || c == ':' || c == '!' || c == '&' || c == '+' ||
          c == '(' || c == ')'))
      return NULL;
  }
  return name[n - 1] == ' ' ? NULL : name;
}

/* Fills the window for a state. `needs`: the game needs YouGame to run (no offline play), so the
   other button is Quit and the words drop "online". `open_ok`: this system can start an installed
   YouGame from the window (Windows). 0 when a text does not fit (never, with a name of at most 40). */
static int yg__start_compose(int which, int on_use, const char *name, int needs, int open_ok, yg__start_win *w) {
  const char *g = yg__start_name_ok(name), *lower = g ? g : "this game", *upper = g ? g : "This game";
  const char *app = "YouGame";
  int t, b;
  if (which < 0 || which >= YG__START_TEXTS) return 0;
  memset(w, 0, sizeof *w);
  w->which = which;
  w->on_use = on_use ? 1 : 0;
  w->facts = !needs && which != YG__START_TEXT_RAISED;
  w->answer = YG__START_ANSWER_GET;
  w->main_label = YG__START_GET;
  w->later_label = needs ? YG__START_QUIT : YG__START_LATER;
  switch (which) {
    case YG__START_TEXT_MISSING:
      if (needs) {
        t = snprintf(w->title, sizeof w->title, "%s needs YouGame", upper);
        b = snprintf(w->body, sizeof w->body, "YouGame is a free app. Get it, sign in or make an account, then start "
                     "%s again.", lower);
      } else {
        t = g ? snprintf(w->title, sizeof w->title, "Play %s online", g)
              : snprintf(w->title, sizeof w->title, "Play online with YouGame");
        b = snprintf(w->body, sizeof w->body,
                     "YouGame is a free app that runs online matches, friends and ranked play for %s. You sign in "
                     "or make an account after it installs.", lower);
      }
      break;
    case YG__START_TEXT_UPDATE:
      w->main_label = YG__START_UPDATE;
      t = snprintf(w->title, sizeof w->title, needs ? "Update %s to play" : "Update %s to play online", app);
      b = snprintf(w->body, sizeof w->body, needs ? "%s needs a newer %s." :
                   "%s needs a newer %s for online play.", upper, app);
      break;
    case YG__START_TEXT_NOT_RUNNING:
      t = snprintf(w->title, sizeof w->title, needs ? "Open %s to play" : "Open %s to play online", app);
      b = snprintf(w->body, sizeof w->body, open_ok ? "%s is on this computer but isn't running." :
                   "%s is on this computer but isn't running. Open it yourself, or get the latest version.", app);
      break;
    case YG__START_TEXT_DIDNT_OPEN:
      t = snprintf(w->title, sizeof w->title, "%s didn't open", app);
      if (on_use) b = snprintf(w->body, sizeof w->body, "Open %s yourself, then try again.", app);
      else if (needs) b = snprintf(w->body, sizeof w->body, "Open %s yourself, then start %s again.", app, lower);
      else b = snprintf(w->body, sizeof w->body, "Open %s yourself to play online.", app);
      break;
    default: /* YG__START_TEXT_RAISED: YouGame is installed, so Get YouGame would not help. One button. */
      w->answer = YG__START_ANSWER_LATER;
      w->main_label = w->later_label;
      w->later_label = NULL;
      t = needs ? snprintf(w->title, sizeof w->title, "%s can't start from here", app)
                : snprintf(w->title, sizeof w->title, "Online can't start from here");
      b = snprintf(w->body, sizeof w->body, needs ?
                   "%s was started as administrator. Close it and start it normally." :
                   "%s was started as administrator. Close it and start it normally to play online.", upper);
      break;
  }
  /* Installed but not running, or didn't open: Open YouGame where this system can start it. If
     it then won't start, yg_start sends the player to the download page instead. */
  if (open_ok && YG__START_OFFERS_OPEN(which)) {
    w->answer = YG__START_ANSWER_OPEN;
    w->main_label = YG__START_OPEN;
  }
  return t > 0 && (size_t)t < sizeof w->title && b > 0 && (size_t)b < sizeof w->body;
}
#define YG__START_WIDE_(s) L##s
#define YG__START_WIDE(s) YG__START_WIDE_(s)

/* What a tray start attempt came to (Windows and macOS; the test seam drives it everywhere). */
#define YG__START_TRAY_STARTED 0 /* the verified app was started with the tray link or Mac flag */
#define YG__START_TRAY_OLD 1     /* its version is older than tray_from, or could not be read */
#define YG__START_TRAY_REFUSED 2 /* no usable program, not the YouGame app, or not signed */
#define YG__START_TRAY_FAILED 3  /* verified, but starting it failed */

/* The update marker's `to`: at most 32 characters, plus the terminator. */
#define YG__START_TO_CAP 33

static int yg__start_slug_ok(const char *s) {
  size_t n = 0;
  if (!s || !*s) return 0;
  for (; s[n]; n++) {
    char c = s[n];
    if (n >= 80) return 0;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
  }
  return 1;
}

/* "0.13.1407" -> {0, 13, 1407}: exactly three dot-separated numbers, each 0..65535 (a Windows file
   version's first three parts). Anything else is refused. */
static int yg__start_version_parse(const char *s, unsigned out[3]) {
  int part = 0, digits = 0;
  if (!s) return 0;
  out[0] = out[1] = out[2] = 0;
  for (; *s; s++) {
    if (*s >= '0' && *s <= '9') {
      out[part] = out[part] * 10 + (unsigned)(*s - '0');
      if (++digits > 5 || out[part] > 65535) return 0;
    } else if (*s == '.' && digits && part < 2) {
      part++;
      digits = 0;
    } else {
      return 0;
    }
  }
  return part == 2 && digits;
}

static int yg__start_version_at_least(const unsigned have[3], const unsigned want[3]) {
  if (have[0] != want[0]) return have[0] > want[0];
  if (have[1] != want[1]) return have[1] > want[1];
  return have[2] >= want[2];
}

#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
/* The Windows tray start's pure text checks. They sit outside the _WIN32 block so the Mac test runs
   them; the checks that need a file or a process stay in the Windows block (and its test seam). */
#define YG__START_PATH_CAP 1024

/* The one program the Windows tray start may start, one entry per flavour: [0] production (yougame://),
   [1] staging (yougame-staging://). It must be <per-user install root>\<folder>\<file> (the installer
   is per-user only), and its signed version resource must say: ProductName = product, FileDescription
   = product, InternalName present and = product, CompanyName = company. OriginalFilename is not
   required (the real app's is empty); a non-empty one must be the file name. This is the only place
   these names live. The same certificate also signs the installer ("YouGame-<version>-win-x64.exe"),
   the uninstaller ("Uninstall <product>.exe": same ProductName, a long FileDescription, no
   InternalName) and resources\elevate.exe: each fails these checks.
   Sources:
   - STAGING: read from signed staging 0.13.1408 by the release lead (file "YouGame Staging.exe",
     ProductName = FileDescription = InternalName "YouGame Staging", OriginalFilename present and
     empty, CompanyName "YouGame", installed in %LOCALAPPDATA%\Programs\yougame-staging). It matches the
     config: desktop/build/staging.cjs:19 productName "YouGame Staging"; staging-installer.cjs:23
     Windows package name "yougame-staging" (a one-click per-user install folder is the package name:
     app-builder-lib targetUtil.js getWindowsInstallationDirName); rcedit writes InternalName = exe
     name without ".exe" and OriginalFilename = "" (app-builder-lib winPackager.js
     signAndEditResources).
   - PRODUCTION: from config, not yet read from a production binary: desktop/electron-builder.yml:6
     productName "YouGame", :83-84 oneClick true / perMachine false, desktop/package.json:2 name
     "yougame-desktop"; FileDescription, InternalName and CompanyName assumed as on staging. */
typedef struct yg__start_allowed {
  const wchar_t *file;    /* the exe's name, the path's last part (any letter case) */
  const wchar_t *product; /* ProductName = FileDescription = InternalName (exact) */
  const wchar_t *company; /* CompanyName (exact) */
  const wchar_t *folder;  /* its folder under the per-user install root (any letter case) */
} yg__start_allowed;
static const yg__start_allowed yg__start_allow[2] = {
  /* production: from config, not yet read from a production binary */
  {L"YouGame.exe", L"YouGame", L"YouGame", L"yougame-desktop"},
  /* staging: read from signed staging 0.13.1408 */
  {L"YouGame Staging.exe", L"YouGame Staging", L"YouGame", L"yougame-staging"}};

/* SIGNER: the leaf's subject CN and O are both "Fluo Inc.", and its issuer is Microsoft's identity
   verified code signing CA: issuer O "Microsoft Corporation", issuer CN starting with
   "Microsoft ID Verified CS " (the CA number changes). Read from signed staging 0.13.1408 (subject
   "CN=Fluo Inc., O=Fluo Inc., L=Orlando, S=Florida, C=US", issuer "CN=Microsoft ID Verified CS EOC CA
   03, O=Microsoft Corporation, C=US"). The leaf is re-issued every few days, so no thumbprint or
   serial pin. L, S and C are not pinned: desktop/WINDOWS_SIGNING.md says the certificate "still lists
   Orlando until it is renewed", so the locality is expected to change. */
#define YG__START_SIGNER_CN L"Fluo Inc."
#define YG__START_SIGNER_O L"Fluo Inc."
#define YG__START_ISSUER_O L"Microsoft Corporation"
#define YG__START_ISSUER_CN_PREFIX L"Microsoft ID Verified CS "

static wchar_t yg__start_wfold(wchar_t c) { return c >= L'A' && c <= L'Z' ? (wchar_t)(c + 32) : c; }

/* Whole strings equal, ASCII letters in any case (Windows paths). Anything else must match exactly,
   so a name that differs only in a non-ASCII letter's case is refused, never accepted. */
static int yg__start_wsame(const wchar_t *a, const wchar_t *b) {
  if (!a || !b) return 0;
  for (; yg__start_wfold(*a) == yg__start_wfold(*b); ++a, ++b)
    if (!*a) return 1;
  return 0;
}

/* The length of `prefix` when `s` starts with it (ASCII letters in any case), else 0. */
static size_t yg__start_wprefix(const wchar_t *s, const wchar_t *prefix) {
  size_t i;
  for (i = 0; prefix[i]; ++i)
    if (yg__start_wfold(s[i]) != yg__start_wfold(prefix[i])) return 0;
  return i;
}

/* Whole strings equal, exactly (version resource and certificate texts). NULL never equals. */
static int yg__start_wequal(const wchar_t *a, const wchar_t *b) {
  if (!a || !b) return 0;
  for (; *a == *b; ++a, ++b)
    if (!*a) return 1;
  return 0;
}

static const wchar_t *yg__start_basename(const wchar_t *path) {
  const wchar_t *last = path;
  for (; *path; ++path)
    if (*path == L'\\') last = path + 1;
  return last;
}

/* The program in a registered open command: the quoted first part, or the text up to the first space.
   Only a local drive path ("C:\...\X.exe") is taken: no relative path (it would be searched for), no
   UNC, \\?\ or \\.\ path, no quote, wildcard, forward slash, control character or second colon (an
   alternate data stream). Writes it to `out` (cap characters with the terminator); 1 when taken. */
static int yg__start_exe_from_command(const wchar_t *command, wchar_t *out, size_t cap) {
  const wchar_t *first = command, *end;
  wchar_t c;
  size_t n, i;
  while (*first == L' ' || *first == L'\t') ++first;
  if (*first == L'"') {
    ++first;
    for (end = first; *end && *end != L'"'; ++end) {}
    if (!*end) return 0;
  } else {
    for (end = first; *end && *end != L' ' && *end != L'\t'; ++end) {}
  }
  n = (size_t)(end - first);
  if (n < 8 || n >= cap) return 0;
  c = yg__start_wfold(first[0]);
  if (!(c >= L'a' && c <= L'z') || first[1] != L':' || first[2] != L'\\') return 0;
  for (i = 0; i < n; ++i)
    if (first[i] < 32 || first[i] == L'"' || first[i] == L'/' || first[i] == L'*' ||
        first[i] == L'?' || first[i] == L'<' || first[i] == L'>' || first[i] == L'|' ||
        (i > 1 && first[i] == L':')) return 0;
  if (first[n - 4] != L'.' || yg__start_wfold(first[n - 3]) != L'e' ||
      yg__start_wfold(first[n - 2]) != L'x' || yg__start_wfold(first[n - 1]) != L'e') return 0;
  for (i = 0; i < n; ++i) out[i] = first[i];
  out[n] = 0;
  return 1;
}

/* The held file's final path (GetFinalPathNameByHandleW, normalized, DOS volume name) is the path that
   was opened: so no folder on the way is a junction or link, and the drive letter is the volume's own
   (not a subst or a redefined letter). A network file (\\?\UNC\...) or a volume with no letter
   (\\?\Volume{...}) never matches a "C:\" path. */
static int yg__start_final_same(const wchar_t *final, const wchar_t *path) {
  size_t skip = yg__start_wprefix(final, L"\\\\?\\");
  if (skip && yg__start_wprefix(final + skip, L"UNC\\")) return 0;
  return yg__start_wsame(final + skip, path);
}

/* `path` is exactly <root>\<folder>\<file>: the program sits directly in its own install folder. */
static int yg__start_in_folder(const wchar_t *path, const wchar_t *root, const wchar_t *folder,
                               const wchar_t *file) {
  size_t r, k;
  if (!root || !folder || !*folder) return 0;
  for (r = 0; root[r]; ++r) {}
  while (r > 3 && root[r - 1] == L'\\') --r; /* "C:\Programs\" as "C:\Programs"; "C:\" stays */
  if (r < 3 || root[1] != L':' || root[2] != L'\\') return 0;
  for (k = 0; k < r; ++k)
    if (yg__start_wfold(path[k]) != yg__start_wfold(root[k])) return 0;
  path += r;
  if (root[r - 1] != L'\\' && *path++ != L'\\') return 0;
  k = yg__start_wprefix(path, folder);
  if (!k || path[k] != L'\\') return 0;
  return yg__start_wsame(path + k + 1, file);
}

/* The program is the YouGame app of this flavour, by its file name and its version resource texts
   (NULL = the text is missing). */
static int yg__start_identity_ok(int staging, const wchar_t *file, const wchar_t *product,
                                 const wchar_t *description, const wchar_t *internal,
                                 const wchar_t *company, const wchar_t *original) {
  const yg__start_allowed *a = &yg__start_allow[staging ? 1 : 0];
  return yg__start_wsame(file, a->file) && yg__start_wequal(product, a->product) &&
         yg__start_wequal(description, a->product) && yg__start_wequal(internal, a->product) &&
         yg__start_wequal(company, a->company) &&
         (!original || !*original || yg__start_wsame(original, a->file));
}

/* The signing certificate is Fluo Inc.'s, issued by Microsoft's identity verified CA (see SIGNER
   above). Each text is one attribute read on its own; NULL (unreadable) fails. */
static int yg__start_signer_ok(const wchar_t *cn, const wchar_t *org, const wchar_t *issuer_cn,
                               const wchar_t *issuer_org) {
  size_t k;
  if (!issuer_cn) return 0;
  k = 0;
  while (YG__START_ISSUER_CN_PREFIX[k] && issuer_cn[k] == YG__START_ISSUER_CN_PREFIX[k]) ++k;
  return !YG__START_ISSUER_CN_PREFIX[k] && issuer_cn[k] && yg__start_wequal(cn, YG__START_SIGNER_CN) &&
         yg__start_wequal(org, YG__START_SIGNER_O) && yg__start_wequal(issuer_org, YG__START_ISSUER_O);
}

/* Environment entries ("NAME=value") dropped from the started YouGame's environment (which is the
   user's own, never the game's: yg__start_env_block), should the user's or system's settings carry
   them: Electron's own switches (ELECTRON_RUN_AS_NODE would run the signed app as plain Node) and
   Node's preload and extra-CA settings. Names in any letter case, as
   Windows treats them; the hidden "=C:=..." entries are kept. */
static int yg__start_env_drop(const wchar_t *entry) {
  size_t n = 0;
  if (!entry || entry[0] == L'=') return 0;
  while (entry[n] && entry[n] != L'=') ++n;
  return yg__start_wprefix(entry, L"ELECTRON_") ||
         (n == 12 && yg__start_wprefix(entry, L"NODE_OPTIONS") == 12) ||
         (n == 19 && yg__start_wprefix(entry, L"NODE_EXTRA_CA_CERTS") == 19);
}

/* THE UPDATE MARKER. Right before YouGame quits to install an update it writes updating.json in its
   profile folder, app.getPath("userData"), under FOLDERID_RoamingAppData (the known folder Electron's
   app.getPath("appData") is). The app side is desktop PR 1777 (desktop/src/updating-marker.ts).
   [0] production: Electron names userData after app.name, which is package.json's productName, else
   its name. desktop/package.json has name "yougame-desktop" and no productName ("YouGame" is set only
   in electron-builder.yml, for the installer and program names), main.ts never calls setName or sets
   userData for production, and desktop/WINDOWS_HANDOFF.md and the agent-test proof look under
   %APPDATA%\yougame-desktop. So "yougame-desktop" is the folder, read first; "YouGame" is read too,
   since a later build that sets productName would move it there and games already shipped must keep
   working. The first valid marker wins. [1] staging: main.ts sets userData to "YouGame Staging"
   (STAGING.userDataName, desktop/src/staging-profile.ts), read first; "YouGame Staging (agent build)"
   is the folder it had until 11-15 (the app renames it once, and keeps it when the rename fails). */
#define YG__START_PROFILES 2
static const wchar_t *const yg__start_profiles[2][YG__START_PROFILES] = {
  {L"yougame-desktop", L"YouGame"},
  {L"YouGame Staging", L"YouGame Staging (agent build)"}};
#define YG__START_MARKER_MAX 1024        /* a marker is under 1 KiB */
#define YG__START_MARKER_AGE_MS 300000.0 /* `at` at most 5 minutes before this computer's clock */
#define YG__START_MARKER_SKEW_MS 60000.0 /* and at most 60 s after it */

/* <root>\<profile>\updating.json into out (cap characters with the terminator); 0 when root or
   profile is missing or empty, or the path does not fit. Trailing backslashes of root are dropped
   ("C:\" gives "C:\YouGame\..."). */
static int yg__start_marker_path(const wchar_t *root, const wchar_t *profile, wchar_t *out, size_t cap) {
  const wchar_t *tail = L"\\updating.json";
  size_t r, at, k;
  if (!root || !profile || !profile[0] || !cap) return 0;
  for (r = 0; root[r]; ++r) {}
  while (r && root[r - 1] == L'\\') --r;
  if (!r || r >= cap) return 0;
  for (at = 0; at < r; ++at) out[at] = root[at];
  if (at + 1 >= cap) return 0;
  out[at++] = L'\\';
  for (k = 0; profile[k]; ++k) {
    if (at + 1 >= cap) return 0;
    out[at++] = profile[k];
  }
  for (k = 0; tail[k]; ++k) {
    if (at + 1 >= cap) return 0;
    out[at++] = tail[k];
  }
  out[at] = 0;
  return 1;
}

/* `to` looks like a version: 1 to 32 characters, digits and single dots, a digit first and last. */
static int yg__start_marker_version_ok(const char *s, size_t n) {
  size_t i;
  if (n < 1 || n > YG__START_TO_CAP - 1) return 0;
  for (i = 0; i < n; ++i) {
    if (s[i] >= '0' && s[i] <= '9') continue;
    if (s[i] != '.' || i == 0 || i + 1 == n || s[i - 1] == '.') return 0;
  }
  return 1;
}

static size_t yg__start_json_space(const char *s, size_t n, size_t i) {
  while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
  return i;
}

/* A JSON string at s[*i] (its opening quote), moved past. 1: plain text (no escape), at most cap - 1
   bytes, copied into out (NUL-terminated) with its length in *len. 2: well formed but with an escape
   or longer than that: skipped, out empty and *len 0 (usable only as a field the hook ignores). 0: a
   control character, or unclosed within n. */
static int yg__start_json_string(const char *s, size_t n, size_t *i, char *out, size_t cap, size_t *len) {
  size_t k = 0;
  int plain = 1;
  if (*i >= n || s[*i] != '"') return 0;
  for (++*i; *i < n; ++*i) {
    unsigned char c = (unsigned char)s[*i];
    if (c == '"') {
      ++*i;
      if (!plain) k = 0;
      out[k] = 0;
      *len = k;
      return plain ? 1 : 2;
    }
    if (c < 0x20) return 0;
    if (c == '\\') {
      plain = 0;
      if (++*i >= n || (unsigned char)s[*i] < 0x20) return 0; /* the escaped byte, never a closing quote */
    } else if (plain && k + 1 < cap) {
      out[k++] = (char)c;
    } else {
      plain = 0;
    }
  }
  return 0;
}

/* A JSON whole number at s[*i]: 1 to 15 digits (so it is exact in a double; no `long long`, which
   C89 builds lack), no sign, no leading zero, no fraction or exponent. */
static int yg__start_json_whole(const char *s, size_t n, size_t *i, double *out) {
  size_t start = *i;
  double v = 0;
  while (*i < n && s[*i] >= '0' && s[*i] <= '9') {
    if (*i - start >= 15) return 0;
    v = v * 10 + (double)(s[*i] - '0');
    ++*i;
  }
  if (*i == start || (s[start] == '0' && *i - start > 1)) return 0;
  if (*i < n && (s[*i] == '.' || s[*i] == 'e' || s[*i] == 'E')) return 0;
  *out = v;
  return 1;
}

/* true, false or null at s[*i]. */
static int yg__start_json_word(const char *s, size_t n, size_t *i) {
  static const char *const words[3] = {"true", "false", "null"};
  size_t w, k;
  for (w = 0; w < 3; ++w) {
    for (k = 0; words[w][k] && *i + k < n && s[*i + k] == words[w][k]; ++k) {}
    if (!words[w][k]) {
      *i += k;
      return 1;
    }
  }
  return 0;
}

/* The marker's body (n bytes, not NUL-terminated) is a valid marker at now_ms (epoch milliseconds):
   one JSON object with "v": 1, "to": a version (yg__start_marker_version_ok), "at": whole epoch
   milliseconds at most 5 minutes before now_ms and at most 60 s after it, and optionally "from": a
   string. Other keys (any string, escapes included) with a string, whole number, true, false or null
   value are skipped, so the app can add fields later. Strict and bounded: a UTF-8 byte order mark and
   whitespace between tokens are allowed; anything else (an escape in a known key or in "to", a nested
   object or array, a negative or fractional number, a known key twice, text after the object, n of
   1024 or more) is no marker. On 1, to holds the version. */
static int yg__start_marker_parse(const char *s, size_t n, double now_ms, char to[YG__START_TO_CAP]) {
  char key[32], value[64], version[YG__START_TO_CAP];
  size_t i = 0, len = 0;
  int have_v = 0, have_to = 0, have_at = 0, have_from = 0;
  double at = 0, num = 0;
  if (!s || n >= YG__START_MARKER_MAX) return 0;
  if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
    i = 3;
  i = yg__start_json_space(s, n, i);
  if (i >= n || s[i] != '{') return 0;
  i = yg__start_json_space(s, n, i + 1);
  for (;;) {
    int kind; /* 0 true/false/null, 1 plain string, 2 whole number, 3 other string */
    int r = yg__start_json_string(s, n, &i, key, sizeof key, &len);
    if (!r) return 0; /* r == 2: a key no field of the hook's has, so key is "" and matches none */
    i = yg__start_json_space(s, n, i);
    if (i >= n || s[i] != ':') return 0;
    i = yg__start_json_space(s, n, i + 1);
    if (i >= n) return 0;
    if (s[i] == '"') {
      r = yg__start_json_string(s, n, &i, value, sizeof value, &len);
      if (!r) return 0;
      kind = r == 1 ? 1 : 3;
    } else if (s[i] >= '0' && s[i] <= '9') {
      if (!yg__start_json_whole(s, n, &i, &num)) return 0;
      kind = 2;
    } else if (yg__start_json_word(s, n, &i)) {
      kind = 0;
    } else {
      return 0;
    }
    if (!strcmp(key, "v")) {
      if (have_v++ || kind != 2 || num != 1) return 0;
    } else if (!strcmp(key, "at")) {
      if (have_at++ || kind != 2) return 0;
      at = num;
    } else if (!strcmp(key, "to")) {
      if (have_to++ || kind != 1 || !yg__start_marker_version_ok(value, len)) return 0;
      memcpy(version, value, len + 1);
    } else if (!strcmp(key, "from")) {
      if (have_from++ || (kind != 1 && kind != 3)) return 0;
    }
    i = yg__start_json_space(s, n, i);
    if (i >= n) return 0;
    if (s[i] == '}') break;
    if (s[i] != ',') return 0;
    i = yg__start_json_space(s, n, i + 1);
  }
  if (yg__start_json_space(s, n, i + 1) != n) return 0;
  if (!have_v || !have_to || !have_at) return 0;
  if (at > now_ms + YG__START_MARKER_SKEW_MS || now_ms - at > YG__START_MARKER_AGE_MS) return 0;
  memcpy(to, version, strlen(version) + 1);
  return 1;
}

/* GetDriveTypeW's DRIVE_FIXED, and the file attributes the marker must not have (FILE_ATTRIBUTE_
   DIRECTORY, _DEVICE, _REPARSE_POINT), written out so the Mac test runs the same checks. */
#define YG__START_DRIVE_FIXED 3u
#define YG__START_ATTR_REFUSED (0x10UL | 0x40UL | 0x400UL)

/* "X:\..." with a drive letter: not a UNC path (\\server\share), not a \\?\ or \\.\ device path, not
   relative. */
static int yg__start_drive_path(const wchar_t *path) {
  return path && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
         path[1] == L':' && path[2] == L'\\';
}

/* The marker may be opened at all: its path is on a drive letter (yg__start_drive_path) whose type,
   GetDriveTypeW of "X:\", is a fixed local drive. A network drive (a Roaming folder redirected to a
   share, mapped or UNC), a removable, CD or RAM drive, or an unknown type is no marker, decided before
   anything is opened, so a slow or gone server cannot hold up the game (a folder on the way that is
   itself a link to a share is not detected). */
static int yg__start_marker_local(const wchar_t *path, unsigned drive_type) {
  return yg__start_drive_path(path) && drive_type == YG__START_DRIVE_FIXED;
}

/* The opened marker is a plain file the hook takes: on disk (GetFileType is FILE_TYPE_DISK), not a
   folder, device or reparse point (it is opened as itself, so a link is seen, never followed), and
   under 1 KiB (the size's high half 0, its low half below YG__START_MARKER_MAX). */
static int yg__start_marker_file_ok(int on_disk, unsigned long attrs, unsigned long size_high,
                                    unsigned long size_low) {
  return on_disk && !(attrs & YG__START_ATTR_REFUSED) && !size_high && size_low < YG__START_MARKER_MAX;
}

/* A FILETIME (100 ns steps since 1601-01-01 UTC, in its 32-bit halves) as epoch milliseconds in *ms;
   0 for a time before 1970. The Unix epoch is FILETIME 116444736000000000 (0x019DB1DE D53E8000);
   the halves are subtracted exactly (no 64-bit integer type, which C89 lacks), and only the
   difference goes into a double: within a microsecond for the next few hundred years. */
#define YG__START_EPOCH_HIGH 0x019DB1DEUL
#define YG__START_EPOCH_LOW 0xD53E8000UL
static int yg__start_filetime_ms(unsigned long high, unsigned long low, double *ms) {
  unsigned long dh, dl;
  high &= 0xFFFFFFFFUL;
  low &= 0xFFFFFFFFUL;
  if (high < YG__START_EPOCH_HIGH || (high == YG__START_EPOCH_HIGH && low < YG__START_EPOCH_LOW))
    return 0;
  dh = high - YG__START_EPOCH_HIGH;
  dl = (low - YG__START_EPOCH_LOW) & 0xFFFFFFFFUL;
  if (low < YG__START_EPOCH_LOW) --dh;
  *ms = ((double)dh * 4294967296.0 + (double)dl) / 10000.0;
  return 1;
}

/* The installed program's version (its file version's first three parts) is already the marker's
   `to` or newer, so the marker is a leftover of an update that has finished. Number by number from
   the left; parts `to` lacks count as 0, and so do have's parts past the third (a file version's fourth part,
   which the hook does not read). Leading zeros do not count; a part of more than 9 digits is larger
   than any have. `to` is digits and dots (yg__start_marker_version_ok). */
static int yg__start_version_reached(const unsigned have[3], const char *to) {
  size_t i = 0;
  unsigned part;
  for (part = 0;; ++part) {
    unsigned long v = 0, h = part < 3 ? have[part] : 0;
    int digits = 0;
    for (; to[i] >= '0' && to[i] <= '9'; ++i) {
      if (!v && to[i] == '0') continue;
      if (++digits > 9) return 0;
      v = v * 10 + (unsigned long)(to[i] - '0');
    }
    if (v != h) return h > v;
    if (to[i] != '.') return 1;
    ++i;
  }
}
#endif

#if defined(__APPLE__) || defined(YOUGAME_START_TESTING)
/* Pure Mac adapters are shared with the seam tests; no real helpers run in those tests. */
#include <limits.h>
#ifndef PATH_MAX /* MSVC builds of the seam tests */
#define PATH_MAX 1024
#endif
/* codesign --strict on a large bundle on a cold disk may approach this 8 s deadline.
   A timeout reads as FAILED; the game keeps running and can retry. */
#define YG__START_CODESIGN_MS 8000u
#define YG__START_OPEN_MS 5000u
static void yg__start_sleep(unsigned ms);
static unsigned long yg__start_ms(void);
static int yg__start_running(int staging);

/* LaunchServices may finish launching even after the open child was killed at its deadline; the
   app's socket can take a moment more, so look up to three times, 250 ms apart. */
static int yg__start_mac_opened(int waited, int staging) {
  int tries;
  if (waited == 1) return 1;
  if (waited != -1) return 0;
  for (tries = 0; tries < 3; ++tries) {
    if (tries) yg__start_sleep(250);
    if (yg__start_running(staging)) return 1;
  }
  return 0;
}

/* poll_child: 0 pending (including EINTR), 1 clean exit, -1 failed/reaped elsewhere.
   stop_child kills and reaps. Return -1 only for a deadline, 0 for another failure. */
static int yg__start_mac_wait(unsigned deadline, int (*poll_child)(void *),
                              void (*stop_child)(void *), void *child) {
  unsigned slept = 0;
  unsigned long start = yg__start_ms();
  for (;;) {
    unsigned long elapsed = (yg__start_ms() - start) & 0xFFFFFFFFUL;
    unsigned step;
    int result;
    if (elapsed >= deadline || slept >= deadline) {
      stop_child(child);
      return -1;
    }
    result = poll_child(child);
    if (result) return result > 0;
    elapsed = (yg__start_ms() - start) & 0xFFFFFFFFUL;
    if (elapsed >= deadline) continue;
    step = deadline - (unsigned)elapsed;
    if (step > deadline - slept) step = deadline - slept;
    if (step > 50) step = 50;
    yg__start_sleep(step);
    slept += step; /* also bounds the wait if the clock fails */
  }
}

static int yg__start_mac_requirement(int staging, char *out, size_t cap) {
  /* codesign -R reads its argument as requirement text only after a leading "=" (else a file path:
     "invalid requirement specification", so every installed app read as unsigned). */
  const char *base = "=anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists "
    "and certificate leaf[field.1.2.840.113635.100.6.1.13] exists "
    "and certificate leaf[subject.OU] = \"4894SAXKGG\" and identifier ";
  int n = snprintf(out, cap, "%s%s", base,
    staging ? "co.yougame.desktop.staging" : "co.yougame.desktop");
  return n >= 0 && (size_t)n < cap;
}

static void yg__start_mac_verify_argv(char *path, char *requirement, char *argv[8]) {
  argv[0] = (char *)"/usr/bin/codesign";
  argv[1] = (char *)"--verify";
  argv[2] = (char *)"--strict";
  argv[3] = (char *)"--all-architectures";
  argv[4] = (char *)"-R";
  argv[5] = requirement;
  argv[6] = path;
  argv[7] = NULL;
}

static void yg__start_mac_open_argv(char *path, char *argv[8]) {
  argv[0] = (char *)"/usr/bin/open";
  argv[1] = (char *)"-g";
  argv[2] = (char *)"-j";
  argv[3] = (char *)"-a";
  argv[4] = path;
  argv[5] = (char *)"--args";
  argv[6] = (char *)"--yougame-tray";
  argv[7] = NULL;
}

/* Keep ordinary launcher variables, but never app/test switches or Electron/Node overrides.
   LaunchServices does not pass open's caller environment to the app anyway; filter both helpers
   as well so neither helper inherits these controls. The returned pointer array is caller-owned. */
static char **yg__start_mac_environment(char *const source[]) {
  size_t n = 0, kept = 0, i;
  char **out;
  while (source[n]) ++n;
  if (n > (size_t)-1 / sizeof *out - 1) return NULL;
  out = (char **)malloc((n + 1) * sizeof *out);
  if (!out) return NULL;
  for (i = 0; i < n; ++i) {
    if (!strncmp(source[i], "YOUGAME_", 8) || !strncmp(source[i], "ELECTRON_", 9) ||
        !strncmp(source[i], "NODE_OPTIONS=", 13) || !strcmp(source[i], "NODE_OPTIONS")) continue;
    out[kept++] = source[i];
  }
  out[kept] = NULL;
  return out;
}

static int yg__start_mac_owner_ok(unsigned long owner, unsigned long user) {
  return owner == 0 || owner == user;
}

/* Audit every link, including links inside another link's target, reusing the same buffers.
   link_target checks ownership and refuses any lstat/readlink failure. At most 40 links. */
static int yg__start_mac_links_ok(const char *path,
                                 int (*link_target)(const char *, char *, size_t)) {
  char prefix[PATH_MAX], target[PATH_MAX], next[PATH_MAX];
  unsigned links = 0;
  size_t n = strlen(path);
  if (path[0] != '/' || n >= sizeof next) return 0;
  memcpy(next, path, n + 1);
  for (;;) {
    size_t i;
    n = strlen(next);
    memcpy(prefix, next, n + 1);
    for (i = 1; i <= n; ++i) {
      int link, written;
      char saved;
      size_t parent = i;
      if (next[i] && next[i] != '/') continue;
      saved = prefix[i];
      prefix[i] = 0;
      link = link_target(prefix, target, sizeof target);
      prefix[i] = saved;
      if (link < 0) return 0;
      if (!link) continue;
      if (++links > 40) return 0;
      while (parent && prefix[parent - 1] != '/') --parent;
      written = target[0] == '/' ? snprintf(next, sizeof next, "%s%s", target, prefix + i) :
        snprintf(next, sizeof next, "%.*s%s%s", (int)parent, prefix, target, prefix + i);
      if (written < 0 || (size_t)written >= sizeof next) return 0;
      break;
    }
    if (i > n) return 1;
  }
}

static int yg__start_mac_bundle_ok(unsigned long owner, unsigned long user,
                                  unsigned mode, int directory) {
  return directory && yg__start_mac_owner_ok(owner, user) && !(mode & 0022u);
}

/* Match desktop/src/os-handlers.ts's development exclusions. Resolved paths have no dot parts.
   Additionally restrict quiet starts to Applications folders; mounted/build copies must not run. */
static int yg__start_mac_candidate(const char *path, const char *home) {
  const char *p;
  char lower[4096];
  size_t n, i, h;
  int rank = 0;
  if (!path || path[0] != '/') return 0;
  n = strlen(path);
  if (n < 5 || n >= sizeof lower) return 0;
  for (i = 0; i <= n; ++i)
    lower[i] = path[i] >= 'A' && path[i] <= 'Z' ? (char)(path[i] + ('a' - 'A')) : path[i];
  if (strcmp(lower + n - 4, ".app")) return 0;
  if (!strncmp(path, "/Applications/", 14)) rank = 2;
  h = home ? strlen(home) : 0;
  while (h > 1 && home[h - 1] == '/') --h;
  if (!rank && h > 1 && home[0] == '/' && !strncmp(path, home, h) &&
      !strncmp(path + h, "/Applications/", 14)) rank = 1;
  if (!rank) return 0;
  if (strstr(lower, "/apptranslocation/") || strstr(lower, "/volumes/") ||
      strstr(lower, "worktree") || strstr(lower, "/.claude/") ||
      strstr(lower, "/release-staging/") || strstr(path, "/desktop/release/")) return 0;
  for (p = lower; (p = strstr(p, "/release/mac")) != NULL; ++p)
    if (p[12] == '/' || p[12] == '-') return 0;
  for (p = lower; *p; ++p) {
    const char *end;
    if (*p == '/' && (p[1] == '/' || (p[1] == '.' &&
        (p[2] == '/' || (p[2] == '.' && p[3] == '/'))))) return 0;
    /* os-handlers also excludes win[-arch]-unpacked and linux[-arch]-unpacked folders. */
    end = *p == '/' ? strchr(p + 1, '/') : NULL;
    if (end && end - p >= 13 && !strncmp(end - 9, "-unpacked", 9) &&
        (!strncmp(p + 1, "win-", 4) || !strncmp(p + 1, "linux-", 6))) return 0;
    /* The executable must be inside this bundle, not an outer .app's nested helper. */
    if (!strncmp(p, ".app/", 5)) return 0;
  }
  return rank;
}

static int yg__start_mac_prefer(const char *path, const char *home, int best) {
  int rank = yg__start_mac_candidate(path, home);
  return rank > best ? rank : 0;
}
#endif

#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
#define YG__START_GET_DOWNLOAD 1
#define YG__START_GET_INSTALLING 2
#define YG__START_GET_DONE 3
#define YG__START_GET_FAILED 4
/* Only the worker writes progress after begin; it never holds a window or the caller's strings.
   Windows LONG is long on both 32-bit and 64-bit builds. The seam has no concurrent OS calls. */
typedef struct yg__start_get {
  volatile long stage, pct, running;
  int staging;
  char game[81];
} yg__start_get;
static yg__start_get yg__start_g;
#ifdef YOUGAME_START_TESTING
#define YG__START_GET_READ(p) (*(p))
#define YG__START_GET_SET(p, v) (*(p) = (v))
#elif defined(_MSC_VER)
/* The compiler's intrinsics: Unreal's Windows headers hide the InterlockedExchange family. */
#include <intrin.h>
#define YG__START_GET_READ(p) _InterlockedCompareExchange((p), 0, 0)
#define YG__START_GET_SET(p, v) _InterlockedExchange((p), (v))
#else
#define YG__START_GET_READ(p) InterlockedCompareExchange((p), 0, 0)
#define YG__START_GET_SET(p, v) InterlockedExchange((p), (v))
#endif
static void yg__start_get_note(int stage);
static void yg__start_get_finish(int installed);
static int yg__start_get_begin(int staging, const char *game);
static int yg__start_download_page(int staging, const char *game);
#endif

#ifdef YOUGAME_START_TESTING
/* The test translation unit defines these; no OS calls, apps or windows in seam builds. */
typedef struct yg__start_app { int id; } yg__start_app;
static int yg__start_test_windows; /* Windows tray and update-marker path */
static int yg__start_test_macos; /* macOS tray path, without update markers; both 0 = Linux */
#define YG__START_TRAY_SYSTEM (yg__start_test_windows || yg__start_test_macos)
#define YG__START_UPDATE_SYSTEM yg__start_test_windows
static int yg__start_running(int staging);
static int yg__start_installed(int staging);
static int yg__start_app_find(int staging, yg__start_app *app);
static int yg__start_app_named(yg__start_app *app, int staging);
static int yg__start_app_version(yg__start_app *app, unsigned have[3]);
static int yg__start_app_signed(yg__start_app *app);
static int yg__start_app_launch(yg__start_app *app, int staging);
static void yg__start_app_close(yg__start_app *app);
static int yg__start_shell_open(const char *url);
/* Existing seam users need no new fake unless they exercise background installation. */
static int (*yg__start_test_get_launch)(void);
static int yg__start_get_launch(void) {
  return yg__start_test_get_launch ? yg__start_test_get_launch() : 0;
}
static int yg__start_offer(const yg__start_win *w);
static int yg__start_offline(int staging, const char *game, int note);
static int yg__start_raised(void);
static int yg__start_quiet_env(void);
static void yg__start_sleep(unsigned ms);
static unsigned long yg__start_ms(void);
static int yg__start_installed_version(int staging, unsigned have[3]);
typedef struct yg__start_mark { int looked; double age_ms; } yg__start_mark;
static int yg__start_marker(int staging, yg__start_mark *mark, char to[YG__START_TO_CAP]);
#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>
#include <sddl.h>
#include <shellapi.h>
#include <wchar.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

#define YG__START_TRAY_SYSTEM 1
#define YG__START_UPDATE_SYSTEM 1
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
/* GetFinalPathNameByHandleW flags (FILE_NAME_NORMALIZED with VOLUME_NAME_DOS / VOLUME_NAME_NT),
   FileRemoteProtocolInfo, PROCESS_NAME_NATIVE and PROCESS_QUERY_LIMITED_INFORMATION, written out so
   a game that sets an old _WIN32_WINNT still builds; the functions themselves are looked up in
   kernel32 at run time. */
#define YG__START_FINAL_DOS 0x0
#define YG__START_FINAL_NT 0x2
#define YG__START_REMOTE_INFO 13
#define YG__START_IMAGE_NATIVE 0x1
#define YG__START_VER_NEUTRAL 0x2 /* FILE_VER_GET_NEUTRAL */
#define YG__START_QUERY_LIMITED 0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */

/* The installed program: its path, a handle that stops it being changed, replaced or deleted from
   the moment it is checked until it has been started, and what its version resource says. */
typedef struct yg__start_app {
  HANDLE file;
  wchar_t path[YG__START_PATH_CAP];
  int named;       /* its version resource and file name are the YouGame app's (yg__start_allow) */
  int have_ok;     /* its file version was read */
  unsigned have[3];
} yg__start_app;

static void yg__start_sleep(unsigned ms) { Sleep(ms); }

/* A monotonic millisecond clock for the tray wait: GetTickCount (kernel32, every Windows; time since
   boot, not the wall clock the user can set). It wraps every 49.7 days; the wait takes differences
   modulo 2^32, so a wrap inside a wait of at most 60 s is harmless. */
static unsigned long yg__start_ms(void) { return (unsigned long)GetTickCount(); }

static int yg__start_quiet_env(void) {
  wchar_t v[8];
  DWORD n = GetEnvironmentVariableW(L"YOUGAME_START_QUIET", v, 8);
  return n == 1 && v[0] == L'1';
}

/* The app's local channel for this Windows user: \\.\pipe\yougame[-staging]-sdk-v1-<user SID>.
   Only its presence is checked; nothing connects to it here. */
static int yg__start_running(int staging) {
  HANDLE tok = NULL;
  union { TOKEN_USER user; BYTE bytes[256]; } buf; /* aligned for TOKEN_USER */
  DWORD len = 0;
  LPWSTR sid = NULL;
  wchar_t name[400];
  int up = 0;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
  if (GetTokenInformation(tok, TokenUser, &buf, sizeof buf, &len) &&
      ConvertSidToStringSidW(buf.user.User.Sid, &sid) && sid) {
    if (wcslen(sid) < 200) {
      wcscpy(name, staging ? L"\\\\.\\pipe\\yougame-staging-sdk-v1-" : L"\\\\.\\pipe\\yougame-sdk-v1-");
      wcscat(name, sid);
      /* TRUE: a pipe instance is free. ERROR_SEM_TIMEOUT: the pipe exists and is busy. */
      if (WaitNamedPipeW(name, 1) || GetLastError() == ERROR_SEM_TIMEOUT) up = 1;
    }
    LocalFree(sid);
  }
  CloseHandle(tok);
  return up;
}

/* The link type's open command, as the app registers it ("<exe>" "%1"). HKCU wins over the merged
   HKCR view; HKCR is read only when HKCU has no key at all. */
static int yg__start_command(int staging, wchar_t *command, DWORD cap) {
  HKEY key;
  LONG rc;
  DWORD type = 0, size;
  wchar_t raw[2048];
  rc = RegOpenKeyExW(HKEY_CURRENT_USER, staging
    ? L"Software\\Classes\\yougame-staging\\shell\\open\\command"
    : L"Software\\Classes\\yougame\\shell\\open\\command", 0, KEY_QUERY_VALUE, &key);
  if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND)
    rc = RegOpenKeyExW(HKEY_CLASSES_ROOT, staging
      ? L"yougame-staging\\shell\\open\\command" : L"yougame\\shell\\open\\command",
      0, KEY_QUERY_VALUE, &key);
  if (rc != ERROR_SUCCESS) return 0;
  size = sizeof raw;
  rc = RegQueryValueExW(key, NULL, NULL, &type, (BYTE *)raw, &size);
  RegCloseKey(key);
  /* A value that does not end in its terminator is refused, never patched up. */
  if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
      size < 2 * sizeof(wchar_t) || size % sizeof(wchar_t) || raw[size / sizeof(wchar_t) - 1])
    return 0;
  if (type == REG_EXPAND_SZ) {
    DWORD n = ExpandEnvironmentStringsW(raw, command, cap);
    return n && n <= cap;
  }
  if (size / sizeof(wchar_t) > cap) return 0;
  memcpy(command, raw, size);
  return 1;
}

/* Installed = the link type has a non-empty open command for this user (the app writes it). */
static int yg__start_installed(int staging) {
  wchar_t command[2048];
  return yg__start_command(staging, command, 2048) && command[0];
}

/* The player's shell: the desktop's shell window (Explorer) belongs to the game's own user, and is
   elevated too? No shell window (a service, session 0, a SYSTEM task) or a question that cannot be
   answered: no. */
static int yg__start_shell_peer(HANDLE game) {
  /* 68: SECURITY_MAX_SID_SIZE; the union keeps the buffer aligned for TOKEN_USER. */
  union { TOKEN_USER user; BYTE raw[sizeof(TOKEN_USER) + 68]; } mine, theirs;
  TOKEN_ELEVATION elevation;
  HWND shell = GetShellWindow();
  DWORD pid = 0, len = 0;
  HANDLE proc, tok = NULL;
  int peer = 0;
  /* The game's own window never counts as the player's shell. */
  if (!shell || !GetWindowThreadProcessId(shell, &pid) || !pid || pid == GetCurrentProcessId())
    return 0;
  proc = OpenProcess(YG__START_QUERY_LIMITED, FALSE, pid);
  if (!proc) return 0;
  if (OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
    elevation.TokenIsElevated = 0;
    if (GetTokenInformation(tok, TokenElevation, &elevation, sizeof elevation, &len) &&
        elevation.TokenIsElevated &&
        GetTokenInformation(tok, TokenUser, &theirs, sizeof theirs, &len) &&
        GetTokenInformation(game, TokenUser, &mine, sizeof mine, &len) &&
        EqualSid(mine.user.User.Sid, theirs.user.User.Sid))
      peer = 1;
    CloseHandle(tok);
  }
  CloseHandle(proc);
  return peer;
}

/* A game run as administrator would start YouGame with the same rights: never do that. The rule is
   yg__start_raised_rule; a question that cannot be answered counts as raised. */
static int yg__start_raised(void) {
  HANDLE tok = NULL;
  TOKEN_ELEVATION_TYPE type = TokenElevationTypeFull;
  TOKEN_ELEVATION elevation;
  DWORD len = 0;
  int raised = 1;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 1;
  elevation.TokenIsElevated = 1;
  if (GetTokenInformation(tok, TokenElevationType, &type, sizeof type, &len) &&
      GetTokenInformation(tok, TokenElevation, &elevation, sizeof elevation, &len)) {
    int full = type == TokenElevationTypeFull, elevated = elevation.TokenIsElevated != 0;
    /* The shell is looked at only when it decides the answer. */
    raised = yg__start_raised_rule(full, elevated,
                                   full || !elevated ? 0 : yg__start_shell_peer(tok));
  }
  CloseHandle(tok);
  return raised;
}

/* Only the system directory's copy, and its own imports from there too (LOAD_LIBRARY_SEARCH_SYSTEM32):
   never a DLL of that name beside the game. Windows without that flag (Windows 7 before KB2533623)
   refuses the load, so nothing is started there. */
static HMODULE yg__start_system_dll(const wchar_t *name) {
  return LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

/* memcpy avoids ISO C's object/function pointer casts; FARPROC is a function pointer. */
#define YG__START_PROC(dst, dll, symbol) do { \
  FARPROC yg__proc = GetProcAddress(dll, symbol); memcpy(&(dst), &yg__proc, sizeof(dst)); \
} while (0)

typedef DWORD (WINAPI *yg__start_final_fn)(HANDLE, LPWSTR, DWORD, DWORD);

/* A file's final path (see yg__start_final_same) into out; 0 when it cannot be had or does not fit. */
static int yg__start_final_path(HANDLE file, DWORD flags, wchar_t *out, DWORD cap) {
  HMODULE k = GetModuleHandleW(L"kernel32.dll");
  yg__start_final_fn get_final = NULL;
  DWORD n;
  if (!k) return 0;
  YG__START_PROC(get_final, k, "GetFinalPathNameByHandleW");
  if (!get_final) return 0;
  n = get_final(file, out, cap, flags);
  return n && n < cap;
}

/* The file at `path` right now is the held file: same volume serial number and file index. */
static int yg__start_same_file(HANDLE held, const wchar_t *path) {
  BY_HANDLE_FILE_INFORMATION a, b;
  HANDLE f;
  int same;
  if (!GetFileInformationByHandle(held, &a)) return 0;
  f = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  same = GetFileInformationByHandle(f, &b) && a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
         a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
  CloseHandle(f);
  return same;
}

/* The held file is where its path says, with no junction, link or remapped drive letter on the way
   (its final path is the path itself), and it is on a fixed local drive: not a network file, even one
   reached through a drive letter. */
static int yg__start_held_path_ok(yg__start_app *app) {
  typedef BOOL (WINAPI *info_fn)(HANDLE, int, LPVOID, DWORD);
  wchar_t final[YG__START_PATH_CAP + 16], root[4];
  union { ULONGLONG align; BYTE bytes[512]; } remote;
  HMODULE k = GetModuleHandleW(L"kernel32.dll");
  info_fn info = NULL;
  if (!k) return 0;
  YG__START_PROC(info, k, "GetFileInformationByHandleEx");
  if (!info ||
      !yg__start_final_path(app->file, YG__START_FINAL_DOS, final, YG__START_PATH_CAP + 16) ||
      !yg__start_final_same(final, app->path))
    return 0;
  /* Only a remote (redirector) file answers FileRemoteProtocolInfo. */
  if (info(app->file, YG__START_REMOTE_INFO, &remote, sizeof remote)) return 0;
  root[0] = app->path[0];
  root[1] = L':';
  root[2] = L'\\';
  root[3] = 0;
  return GetDriveTypeW(root) == DRIVE_FIXED;
}

/* The program sits directly in its flavour's install folder under the per-user install root
   (FOLDERID_UserProgramFiles, %LOCALAPPDATA%\Programs): the installer is per-user only. */
static int yg__start_install_folder_ok(const wchar_t *path, int staging) {
  typedef HRESULT (WINAPI *known_fn)(const GUID *, DWORD, HANDLE, PWSTR *);
  typedef void (WINAPI *free_fn)(LPVOID);
  static const GUID user_programs = /* FOLDERID_UserProgramFiles */
    {0x5cd7aee2, 0x2219, 0x4a67, {0xb8, 0x5d, 0x6c, 0x9c, 0xe1, 0x56, 0x60, 0xcb}};
  const yg__start_allowed *a = &yg__start_allow[staging ? 1 : 0];
  HMODULE shell = yg__start_system_dll(L"shell32.dll");
  HMODULE ole = yg__start_system_dll(L"ole32.dll");
  known_fn known = NULL;
  free_fn release = NULL;
  PWSTR root = NULL;
  int ok = 0;
  if (shell && ole) {
    YG__START_PROC(known, shell, "SHGetKnownFolderPath");
    YG__START_PROC(release, ole, "CoTaskMemFree");
  }
  if (known && release) {
    if (known(&user_programs, 0, NULL, &root) == S_OK && root)
      ok = yg__start_in_folder(path, root, a->folder, a->file);
    if (root) release(root);
  }
  if (ole) FreeLibrary(ole);
  if (shell) FreeLibrary(shell);
  return ok;
}

/* The update marker (see THE UPDATE MARKER), only read: never written or deleted. Its folder is
   FOLDERID_RoamingAppData from SHGetKnownFolderPath (loaded from the system directory, as above),
   never %APPDATA% from the game's environment, looked up once per yg_start call (mark), with the
   drive type of its root: only paths on a fixed local drive are kept (yg__start_marker_local), so a
   Roaming folder redirected to a network share is never opened (a folder on the way that is itself
   a link to a share is not detected: FILE_FLAG_OPEN_REPARSE_POINT covers the file alone). Each kept path, in yg__start_profiles' order, is read
   by yg__start_marker_read; the first valid marker wins. Any failure is no marker. */
typedef struct yg__start_mark {
  double age_ms; /* marker file age, for the installer post-step grace period */
  int looked, count; /* the folder was looked up; count paths kept in path */
  wchar_t path[YG__START_PROFILES][YG__START_PATH_CAP];
} yg__start_mark;

/* One marker file: opened as itself, not through a link, shared for reading, writing and deleting
   so the app is never held up; a plain file under 1 KiB (yg__start_marker_file_ok) whose body is
   valid against the system clock now (yg__start_filetime_ms, yg__start_marker_parse). */
static int yg__start_marker_read(const wchar_t *path, char to[YG__START_TO_CAP], double *age_ms) {
  char body[YG__START_MARKER_MAX];
  BY_HANDLE_FILE_INFORMATION info;
  FILETIME ft;
  DWORD got = 0;
  double now, written;
  HANDLE f;
  int ok = 0;
  f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  if (GetFileInformationByHandle(f, &info) &&
      yg__start_marker_file_ok(GetFileType(f) == FILE_TYPE_DISK, info.dwFileAttributes,
                               info.nFileSizeHigh, info.nFileSizeLow) &&
      ReadFile(f, body, (DWORD)sizeof body, &got, NULL) && got < YG__START_MARKER_MAX) {
    GetSystemTimeAsFileTime(&ft);
    if (yg__start_filetime_ms(ft.dwHighDateTime, ft.dwLowDateTime, &now) &&
        yg__start_filetime_ms(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime, &written)) {
      ok = yg__start_marker_parse(body, got, now, to);
      if (ok) *age_ms = now - written;
    }
  }
  CloseHandle(f);
  return ok;
}

static int yg__start_marker(int staging, yg__start_mark *mark, char to[YG__START_TO_CAP]) {
  typedef HRESULT (WINAPI *known_fn)(const GUID *, DWORD, HANDLE, PWSTR *);
  typedef void (WINAPI *free_fn)(LPVOID);
  static const GUID roaming = /* FOLDERID_RoamingAppData */
    {0x3eb685db, 0x65f9, 0x4cf6, {0xa0, 0x3a, 0xe3, 0xef, 0x65, 0x72, 0x9f, 0x3d}};
  int i;
  if (!mark->looked) {
    HMODULE shell = yg__start_system_dll(L"shell32.dll");
    HMODULE ole = yg__start_system_dll(L"ole32.dll");
    known_fn known = NULL;
    free_fn release = NULL;
    PWSTR root = NULL;
    mark->looked = 1;
    mark->count = 0;
    if (shell && ole) {
      YG__START_PROC(known, shell, "SHGetKnownFolderPath");
      YG__START_PROC(release, ole, "CoTaskMemFree");
    }
    if (known && release) {
      if (known(&roaming, 0, NULL, &root) == S_OK && root && yg__start_drive_path(root)) {
        wchar_t drive[4];
        unsigned type;
        drive[0] = root[0];
        drive[1] = L':';
        drive[2] = L'\\';
        drive[3] = 0;
        type = (unsigned)GetDriveTypeW(drive);
        for (i = 0; i < YG__START_PROFILES; ++i) {
          wchar_t *out = mark->path[mark->count];
          if (yg__start_marker_path(root, yg__start_profiles[staging ? 1 : 0][i], out, YG__START_PATH_CAP) &&
              yg__start_marker_local(out, type))
            ++mark->count;
        }
      }
      if (root) release(root);
    }
    if (ole) FreeLibrary(ole);
    if (shell) FreeLibrary(shell);
  }
  for (i = 0; i < mark->count; ++i)
    if (yg__start_marker_read(mark->path[i], to, &mark->age_ms)) return 1;
  return 0;
}

/* One text of the version resource, \StringFileInfo\<lang><codepage>\<name>, copied into out (at most
   cap - 1 characters): 1. Missing: 0 (out empty). Too long to be ours: -1. */
typedef BOOL (WINAPI *yg__start_query_fn)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
static int yg__start_version_text(yg__start_query_fn query, const void *block, const WORD *lang,
                                  const wchar_t *name, wchar_t *out, size_t cap) {
  static const wchar_t hex[] = L"0123456789abcdef";
  wchar_t key[64];
  const wchar_t *p = L"\\StringFileInfo\\", *value = NULL;
  UINT len = 0;
  size_t at = 0, i;
  DWORD code = ((DWORD)lang[0] << 16) | lang[1];
  while (*p) key[at++] = *p++;
  for (i = 0; i < 8; ++i) key[at++] = hex[(code >> (28 - 4 * i)) & 15];
  key[at++] = L'\\';
  for (i = 0; name[i] && at + 1 < sizeof key / sizeof *key; ++i) key[at++] = name[i];
  key[at] = 0;
  out[0] = 0;
  if (!query(block, key, (LPVOID *)&value, &len) || !value) return 0;
  for (i = 0; i < len && value[i]; ++i) {
    if (i + 1 >= cap) return -1;
    out[i] = value[i];
  }
  out[i] = 0;
  return 1;
}

/* Reads the program's version resource once: its file version (first three parts; electron-builder
   writes 0.13.1407 as 0,13,1407,0) and whether it is the YouGame app of this flavour
   (yg__start_identity_ok). Version.dll has no call that reads from a handle, so the read goes by the
   held file's own final path (GetFinalPathNameByHandleW, its "\\?\" prefix removed: a local drive
   path, already checked to be the registered one), not the registered text; yg__start_app_find checks
   right after that the path still leads to the held file, and the start itself is bound to the held
   file (yg__start_image_is_held). FILE_VER_GET_NEUTRAL reads the texts from the program itself, never
   from a .mui file beside it, so they are inside the signed image and a changed text fails the
   signature check. Without the Ex calls (before Vista) nothing is read, so nothing is started. */
static void yg__start_app_read(yg__start_app *app, int staging) {
  typedef DWORD (WINAPI *size_fn)(DWORD, LPCWSTR, LPDWORD);
  typedef BOOL (WINAPI *info_fn)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
  HMODULE dll = yg__start_system_dll(L"version.dll");
  size_fn get_size = NULL;
  info_fn get_info = NULL;
  yg__start_query_fn query = NULL;
  DWORD handle = 0, size = 0;
  void *buf = NULL;
  VS_FIXEDFILEINFO *fixed = NULL;
  WORD *lang = NULL;
  UINT len = 0;
  wchar_t product[128], description[128], internal[128], company[128], original[128];
  wchar_t final[YG__START_PATH_CAP + 16];
  const wchar_t *at;
  if (!dll) return;
  if (!yg__start_final_path(app->file, YG__START_FINAL_DOS, final, YG__START_PATH_CAP + 16) ||
      !yg__start_final_same(final, app->path)) {
    FreeLibrary(dll);
    return;
  }
  at = final + yg__start_wprefix(final, L"\\\\?\\");
  YG__START_PROC(get_size, dll, "GetFileVersionInfoSizeExW");
  YG__START_PROC(get_info, dll, "GetFileVersionInfoExW");
  YG__START_PROC(query, dll, "VerQueryValueW");
  if (get_size && get_info && query) size = get_size(YG__START_VER_NEUTRAL, at, &handle);
  if (size && size <= (1u << 20)) buf = HeapAlloc(GetProcessHeap(), 0, size);
  if (buf && get_info(YG__START_VER_NEUTRAL, at, 0, size, buf)) {
    if (query(buf, L"\\", (LPVOID *)&fixed, &len) && fixed && len >= sizeof *fixed &&
        fixed->dwSignature == 0xFEEF04BDu) {
      app->have[0] = HIWORD(fixed->dwFileVersionMS);
      app->have[1] = LOWORD(fixed->dwFileVersionMS);
      app->have[2] = HIWORD(fixed->dwFileVersionLS);
      app->have_ok = 1;
    }
    /* The texts of the first language the resource lists. A missing text reads as NULL. */
    len = 0;
    if (query(buf, L"\\VarFileInfo\\Translation", (LPVOID *)&lang, &len) && lang && len >= 4) {
      int p = yg__start_version_text(query, buf, lang, L"ProductName", product, 128);
      int d = yg__start_version_text(query, buf, lang, L"FileDescription", description, 128);
      int i = yg__start_version_text(query, buf, lang, L"InternalName", internal, 128);
      int c = yg__start_version_text(query, buf, lang, L"CompanyName", company, 128);
      int o = yg__start_version_text(query, buf, lang, L"OriginalFilename", original, 128);
      app->named = p >= 0 && d >= 0 && i >= 0 && c >= 0 && o >= 0 &&
                   yg__start_identity_ok(staging, yg__start_basename(app->path), p ? product : NULL,
                                         d ? description : NULL, i ? internal : NULL,
                                         c ? company : NULL, o ? original : NULL);
    }
  }
  if (buf) HeapFree(GetProcessHeap(), 0, buf);
  FreeLibrary(dll);
}

/* The program in the open command (yg__start_exe_from_command), opened with read sharing only so
   nothing can write, replace or delete it while it is checked and started; not a link or junction
   itself, and none on the way to it; on a fixed local drive; directly in its flavour's install folder;
   then its version resource is read, and the path must still lead to this same file. Refused outright
   when the game runs raised. */
static int yg__start_app_find(int staging, yg__start_app *app) {
  wchar_t command[2048];
  BY_HANDLE_FILE_INFORMATION info;
  app->file = INVALID_HANDLE_VALUE;
  app->path[0] = 0;
  app->named = app->have_ok = 0;
  if (yg__start_raised() || !yg__start_command(staging, command, 2048) ||
      !yg__start_exe_from_command(command, app->path, YG__START_PATH_CAP))
    return 0;
  app->file = CreateFileW(app->path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (app->file == INVALID_HANDLE_VALUE) return 0;
  if (GetFileType(app->file) != FILE_TYPE_DISK || !GetFileInformationByHandle(app->file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
      !yg__start_held_path_ok(app) || !yg__start_install_folder_ok(app->path, staging)) {
    CloseHandle(app->file);
    app->file = INVALID_HANDLE_VALUE;
    return 0;
  }
  yg__start_app_read(app, staging);
  if (!yg__start_held_path_ok(app) || !yg__start_same_file(app->file, app->path)) {
    CloseHandle(app->file);
    app->file = INVALID_HANDLE_VALUE;
    return 0;
  }
  return 1;
}

static void yg__start_app_close(yg__start_app *app) {
  if (app->file != INVALID_HANDLE_VALUE) CloseHandle(app->file);
  app->file = INVALID_HANDLE_VALUE;
}

/* The installed program's file version (first three parts), for the leftover-marker check only
   (yg__start_update_done). A valid marker usually means an installer is replacing the program right
   now, so the hook holds no handle of its own on it (yg__start_app_find's read-only sharing would
   block the installer's writes and deletes): version.dll reads it by path, for the moment of the
   read. The path is the open command's program (yg__start_exe_from_command: absolute, no UNC or
   device path) and its drive must be a fixed local one, checked before the read. Nothing else is
   checked: this only decides to skip "updating", and the tray start that follows checks it all. */
static int yg__start_installed_version(int staging, unsigned have[3]) {
  typedef DWORD (WINAPI *size_fn)(DWORD, LPCWSTR, LPDWORD);
  typedef BOOL (WINAPI *info_fn)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
  wchar_t command[2048], path[YG__START_PATH_CAP], drive[4];
  HMODULE dll;
  size_fn get_size = NULL;
  info_fn get_info = NULL;
  yg__start_query_fn query = NULL;
  DWORD handle = 0, size = 0;
  void *buf = NULL;
  VS_FIXEDFILEINFO *fixed = NULL;
  UINT len = 0;
  int ok = 0;
  if (!yg__start_command(staging, command, 2048) ||
      !yg__start_exe_from_command(command, path, YG__START_PATH_CAP) || !yg__start_drive_path(path))
    return 0;
  drive[0] = path[0];
  drive[1] = L':';
  drive[2] = L'\\';
  drive[3] = 0;
  if (!yg__start_marker_local(path, (unsigned)GetDriveTypeW(drive))) return 0;
  dll = yg__start_system_dll(L"version.dll");
  if (!dll) return 0;
  YG__START_PROC(get_size, dll, "GetFileVersionInfoSizeExW");
  YG__START_PROC(get_info, dll, "GetFileVersionInfoExW");
  YG__START_PROC(query, dll, "VerQueryValueW");
  if (get_size && get_info && query) size = get_size(YG__START_VER_NEUTRAL, path, &handle);
  if (size && size <= (1u << 20)) buf = HeapAlloc(GetProcessHeap(), 0, size);
  if (buf && get_info(YG__START_VER_NEUTRAL, path, 0, size, buf) &&
      query(buf, L"\\", (LPVOID *)&fixed, &len) && fixed && len >= sizeof *fixed &&
      fixed->dwSignature == 0xFEEF04BDu) {
    have[0] = HIWORD(fixed->dwFileVersionMS);
    have[1] = LOWORD(fixed->dwFileVersionMS);
    have[2] = HIWORD(fixed->dwFileVersionLS);
    ok = 1;
  }
  if (buf) HeapFree(GetProcessHeap(), 0, buf);
  FreeLibrary(dll);
  return ok;
}

static int yg__start_app_named(yg__start_app *app, int staging) {
  (void)staging; /* decided by yg__start_app_read for the flavour find was given */
  return app->named;
}

static int yg__start_app_version(yg__start_app *app, unsigned have[3]) {
  if (!app->have_ok) return 0;
  have[0] = app->have[0];
  have[1] = app->have[1];
  have[2] = app->have[2];
  return 1;
}

/* One name attribute (oid) of the certificate's subject, or of its issuer, into out. 0 when it cannot
   be read, does not fit, or has a NUL inside it. */
typedef DWORD (WINAPI *yg__start_name_fn)(PCCERT_CONTEXT, DWORD, DWORD, void *, LPWSTR, DWORD);
static int yg__start_cert_attr(yg__start_name_fn name, PCCERT_CONTEXT cert, int issuer, const char *oid,
                               wchar_t *out, DWORD cap) {
  DWORD n;
  memset(out, 0, cap * sizeof *out);
  n = name(cert, CERT_NAME_ATTR_TYPE, issuer ? CERT_NAME_ISSUER_FLAG : 0, (void *)oid, out, cap);
  return n > 1 && n < cap && wcslen(out) == n - 1;
}

/* The program's own Authenticode signature (WinVerifyTrust on the held file, no UI, no network, no
   revocation check) is valid, and its signing certificate passes yg__start_signer_ok: subject CN and
   O "Fluo Inc.", issuer O "Microsoft Corporation", issuer CN "Microsoft ID Verified CS ...". Any step
   that fails or cannot run, an issuer that cannot be read included, means not signed. A game start
   reads the certificate cache only, so it never waits on the network; the background Get may fetch
   a missing intermediate (network 1), since a computer that never ran YouGame may not have it. */
static int yg__start_app_signed_from(yg__start_app *app, int network) {
  typedef LONG (WINAPI *verify_fn)(HWND, GUID *, LPVOID);
  typedef CRYPT_PROVIDER_DATA *(WINAPI *data_fn)(HANDLE);
  typedef CRYPT_PROVIDER_SGNR *(WINAPI *signer_fn)(CRYPT_PROVIDER_DATA *, DWORD, BOOL, DWORD);
  /* crypt32 first, so wintrust's import of it is already the system copy. */
  HMODULE crypt = yg__start_system_dll(L"crypt32.dll");
  HMODULE trust = yg__start_system_dll(L"wintrust.dll");
  verify_fn verify = NULL;
  data_fn data = NULL;
  signer_fn signer = NULL;
  yg__start_name_fn name = NULL;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_FILE_INFO file;
  WINTRUST_DATA check;
  int ok = 0;
  if (trust && crypt) {
    YG__START_PROC(verify, trust, "WinVerifyTrust");
    YG__START_PROC(data, trust, "WTHelperProvDataFromStateData");
    YG__START_PROC(signer, trust, "WTHelperGetProvSignerFromChain");
    YG__START_PROC(name, crypt, "CertGetNameStringW");
  }
  if (verify && data && signer && name) {
    memset(&file, 0, sizeof file);
    file.cbStruct = sizeof file;
    file.pcwszFilePath = app->path;
    file.hFile = app->file; /* the held file; the start is bound to it (yg__start_image_is_held) */
    memset(&check, 0, sizeof check);
    check.cbStruct = sizeof check;
    check.dwUIChoice = WTD_UI_NONE;
    check.fdwRevocationChecks = WTD_REVOKE_NONE;
    check.dwUnionChoice = WTD_CHOICE_FILE;
    check.pFile = &file;
    check.dwStateAction = WTD_STATEACTION_VERIFY;
    check.dwProvFlags = WTD_REVOCATION_CHECK_NONE | (network ? 0 : WTD_CACHE_ONLY_URL_RETRIEVAL);
    if (verify((HWND)INVALID_HANDLE_VALUE, &action, &check) == ERROR_SUCCESS) {
      CRYPT_PROVIDER_DATA *provider = data(check.hWVTStateData);
      CRYPT_PROVIDER_SGNR *sig = provider ? signer(provider, 0, FALSE, 0) : NULL;
      /* pasCertChain[0] is the leaf that signed the file (not a countersigner). */
      PCCERT_CONTEXT leaf =
        sig && sig->csCertChain && sig->pasCertChain ? sig->pasCertChain[0].pCert : NULL;
      wchar_t cn[128], org[128], issuer_cn[128], issuer_org[128];
      if (leaf) {
        int c = yg__start_cert_attr(name, leaf, 0, szOID_COMMON_NAME, cn, 128);
        int o = yg__start_cert_attr(name, leaf, 0, szOID_ORGANIZATION_NAME, org, 128);
        int ic = yg__start_cert_attr(name, leaf, 1, szOID_COMMON_NAME, issuer_cn, 128);
        int io = yg__start_cert_attr(name, leaf, 1, szOID_ORGANIZATION_NAME, issuer_org, 128);
        ok = yg__start_signer_ok(c ? cn : NULL, o ? org : NULL, ic ? issuer_cn : NULL,
                                 io ? issuer_org : NULL);
      }
    }
    /* Always release the verification state, whatever the verdict. */
    check.dwStateAction = WTD_STATEACTION_CLOSE;
    verify((HWND)INVALID_HANDLE_VALUE, &action, &check);
  }
  if (trust) FreeLibrary(trust);
  if (crypt) FreeLibrary(crypt);
  return ok;
}
static int yg__start_app_signed(yg__start_app *app) { return yg__start_app_signed_from(app, 0); }

/* The new (still suspended) process runs the held file: the kernel's own name for its image equals
   the held file's final NT path (no drive letter or junction involved), and the image's path opened
   afresh is the held file by volume serial number and file index. */
static int yg__start_image_is_held(yg__start_app *app, HANDLE process) {
  typedef BOOL (WINAPI *image_fn)(HANDLE, DWORD, LPWSTR, PDWORD);
  wchar_t image[YG__START_PATH_CAP + 64], held[YG__START_PATH_CAP + 64];
  HMODULE k = GetModuleHandleW(L"kernel32.dll");
  image_fn query = NULL;
  DWORD n = YG__START_PATH_CAP + 64;
  if (!k) return 0;
  YG__START_PROC(query, k, "QueryFullProcessImageNameW");
  if (!query || !query(process, YG__START_IMAGE_NATIVE, image, &n) || !n ||
      n >= YG__START_PATH_CAP + 64 ||
      !yg__start_final_path(app->file, YG__START_FINAL_NT, held, YG__START_PATH_CAP + 64) ||
      !yg__start_wsame(image, held))
    return 0;
  n = YG__START_PATH_CAP + 64;
  if (!query(process, 0, image, &n) || !n || n >= YG__START_PATH_CAP + 64) return 0;
  return yg__start_same_file(app->file, image);
}

/* The started YouGame's environment: the one Explorer would give this user (CreateEnvironmentBlock
   on this process's token with bInherit FALSE: the system and user variables from the registry,
   nothing of the game's own), without the entries yg__start_env_drop names. A Unicode block, as
   CREATE_UNICODE_ENVIRONMENT expects; HeapFree it. NULL when it cannot be made, and then nothing is
   started: there is no hand-built fallback, because userenv.dll and CreateEnvironmentBlock ship with
   every Windows since 2000, so a normal PC never lands there. */
static wchar_t *yg__start_env_block(void) {
  typedef BOOL (WINAPI *create_fn)(LPVOID *, HANDLE, BOOL);
  typedef BOOL (WINAPI *destroy_fn)(LPVOID);
  HMODULE dll = yg__start_system_dll(L"userenv.dll");
  create_fn create = NULL;
  destroy_fn destroy = NULL;
  HANDLE tok = NULL;
  LPVOID clean = NULL;
  wchar_t *out = NULL;
  const wchar_t *env, *e;
  size_t total = 0, at = 0, len;
  if (!dll) return NULL;
  YG__START_PROC(create, dll, "CreateEnvironmentBlock");
  YG__START_PROC(destroy, dll, "DestroyEnvironmentBlock");
  /* The documented rights for CreateEnvironmentBlock's token. */
  if (create && destroy &&
      OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, &tok)) {
    if (!create(&clean, tok, FALSE)) clean = NULL;
    CloseHandle(tok);
  }
  if (clean) {
    env = (const wchar_t *)clean;
    for (e = env; *e; e += wcslen(e) + 1) total += wcslen(e) + 1;
    out = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (total + 2) * sizeof(wchar_t));
    if (out) {
      for (e = env; *e; e += len + 1) {
        len = wcslen(e);
        if (yg__start_env_drop(e)) continue;
        memcpy(out + at, e, len * sizeof(wchar_t));
        at += len;
        out[at++] = 0;
      }
      out[at++] = 0;
      if (at == 1) out[at++] = 0; /* an empty block is two NULs */
    }
    destroy(clean);
  }
  FreeLibrary(dll);
  return out;
}

/* Starts the checked program by its full path (no search), with one argument: the tray link. That is
   what the link type's own command does; the app then starts with no window. It starts suspended and
   runs only once yg__start_image_is_held confirms it is the held file; otherwise it is ended (and if
   ending it fails, it stays suspended and never runs) and counts as not started. The game's handles
   and environment are not inherited (yg__start_env_block), and it runs in the program's own folder.
   It leaves the game's job when the job allows that, so a launcher that ends the game's job does not
   end YouGame. */

static int yg__start_app_launch(yg__start_app *app, int staging) {
  const wchar_t *link = staging ? L"yougame-staging://tray" : L"yougame://tray";
  wchar_t line[YG__START_PATH_CAP + 40], dir[YG__START_PATH_CAP];
  const DWORD flags =
    DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED |
    CREATE_UNICODE_ENVIRONMENT;
  wchar_t *env;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  size_t n = wcslen(app->path), cut = n;
  int attempt, ok = 0;
  if (n + wcslen(link) + 4 >= sizeof line / sizeof *line) return 0;
  while (cut && app->path[cut - 1] != L'\\') --cut;
  if (cut < 3) return 0;
  if (cut > 3) --cut; /* "C:\Games\YouGame" without its last backslash; a root keeps "C:\" */
  memcpy(dir, app->path, cut * sizeof(wchar_t));
  dir[cut] = 0;
  env = yg__start_env_block();
  if (!env) return 0;
  for (attempt = 0; attempt < 2 && !ok; ++attempt) {
    /* Built afresh each time: CreateProcessW may write to the command line. */
    line[0] = L'"';
    memcpy(line + 1, app->path, n * sizeof(wchar_t));
    line[n + 1] = L'"';
    line[n + 2] = L' ';
    wcscpy(line + n + 3, link);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    ok = CreateProcessW(app->path, line, NULL, NULL, FALSE,
                        attempt == 0 ? flags | CREATE_BREAKAWAY_FROM_JOB : flags, env, dir, &si, &pi) != 0;
    if (!ok && GetLastError() != ERROR_ACCESS_DENIED) break; /* only a job that forbids leaving retries */
  }
  HeapFree(GetProcessHeap(), 0, env);
  if (!ok) return 0;
  if (!yg__start_image_is_held(app, pi.hProcess) || ResumeThread(pi.hThread) == (DWORD)-1) {
    /* End it and wait for the end. If ending it fails, its thread was never resumed (the image
       check failed, or the resume itself did), so it stays suspended and runs nothing. Either way it
       counts as not started and both handles are closed. */
    if (TerminateProcess(pi.hProcess, 1)) (void)WaitForSingleObject(pi.hProcess, 5000);
    ok = 0;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return ok;
}

/* The download page, through the system's handler for https. */
static int yg__start_shell_open(const char *url) {
  wchar_t what[256];
  size_t i;
  for (i = 0; url[i] && i + 1 < sizeof what / sizeof *what; ++i) what[i] = (wchar_t)url[i];
  if (url[i]) return 0;
  what[i] = 0;
  return (INT_PTR)ShellExecuteW(NULL, L"open", what, NULL, NULL, SW_SHOWNORMAL) > 32;
}

/* The small window: YouGame's own, two buttons, or three with Open YouGame (the default then).
   Returns YG__START_ANSWER_*. */
/* The at-start game's player pressed Play offline once (see yg_start): an empty file named for the
   game in %LOCALAPPDATA%\\YouGame Start (FOLDERID_LocalAppData from SHGetKnownFolderPath, loaded from
   the system directory). note = 0 asks whether it is there; note = 1 makes it. Any failure: not
   there (the window shows at start, as before). */
static int yg__start_offline(int staging, const char *game, int note) {
  typedef HRESULT (WINAPI *known_fn)(const GUID *, DWORD, HANDLE, PWSTR *);
  typedef void (WINAPI *free_fn)(LPVOID);
  static const GUID local = /* FOLDERID_LocalAppData */
    {0xf1b32785, 0x6fba, 0x4fcf, {0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91}};
  HMODULE shell = yg__start_system_dll(L"shell32.dll");
  HMODULE ole = yg__start_system_dll(L"ole32.dll");
  known_fn known = NULL;
  free_fn release = NULL;
  PWSTR root = NULL;
  wchar_t path[YG__START_PATH_CAP];
  int ok = 0;
  if (shell && ole) {
    YG__START_PROC(known, shell, "SHGetKnownFolderPath");
    YG__START_PROC(release, ole, "CoTaskMemFree");
  }
  if (known && release && known(&local, 0, NULL, &root) == S_OK && root) {
    size_t at = wcslen(root), i;
    const char *tail = staging ? ".staging.offline" : ".offline";
    static const wchar_t folder[] = L"\\YouGame Start";
    if (at + wcslen(folder) + 1 + strlen(game) + strlen(tail) + 1 <= YG__START_PATH_CAP) {
      memcpy(path, root, at * sizeof(wchar_t));
      memcpy(path + at, folder, sizeof folder);
      at += wcslen(folder);
      if (note) CreateDirectoryW(path, NULL);
      path[at++] = L'\\';
      for (i = 0; game[i]; ++i) path[at++] = (wchar_t)(unsigned char)game[i];
      for (i = 0; tail[i]; ++i) path[at++] = (wchar_t)(unsigned char)tail[i];
      path[at] = 0;
      if (!note) {
        DWORD a = GetFileAttributesW(path);
        ok = a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
      } else {
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ok = f != INVALID_HANDLE_VALUE;
        if (ok) CloseHandle(f);
      }
    }
  }
  if (root) release(root);
  if (ole) FreeLibrary(ole);
  if (shell) FreeLibrary(shell);
  return ok;
}

/* Puts the small window in front. A game that just started often isn't the foreground process yet,
   and then Windows refuses SetForegroundWindow (the window opened behind a browser on a test machine).
   So: try it; else, only while the player isn't typing or clicking somewhere else (no input for a
   second) and the foreground window answers within 200 ms (never hang the game on a busy launcher),
   join the foreground window's input queue for the one call; else flash it in the taskbar so the
   player sees it and clicks it when ready. Never synthetic key presses. */
static void yg__start_front(HWND w) {
  HWND fg;
  DWORD me = GetCurrentThreadId(), them = 0;
  BOOL joined = FALSE;
  if (SetForegroundWindow(w) && GetForegroundWindow() == w) return;
  fg = GetForegroundWindow();
  if (fg) them = GetWindowThreadProcessId(fg, NULL);
  if (them && them != me) {
    LASTINPUTINFO li;
    DWORD_PTR answer = 0;
    memset(&li, 0, sizeof li);
    li.cbSize = sizeof li;
    if (GetLastInputInfo(&li) && GetTickCount() - li.dwTime >= 1000 &&
        SendMessageTimeoutW(fg, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &answer))
      joined = AttachThreadInput(me, them, TRUE);
  }
  if (joined) {
    BringWindowToTop(w);
    SetForegroundWindow(w);
    SetActiveWindow(w);
    AttachThreadInput(me, them, FALSE);
  }
  if (GetForegroundWindow() != w) {
    FLASHWINFO f;
    memset(&f, 0, sizeof f);
    f.cbSize = sizeof f;
    f.hwnd = w;
    f.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    FlashWindowEx(&f);
  }
}

/* YouGame's own window (the approved C3 look): a red logo panel on the left, the words
   and buttons on the right, on YouGame's dark surface. Everything is drawn here, with no image file
   and no new library: shapes are anti-aliased by distance (yg__start_cover), text is the system's
   Segoe UI. Mouse, keyboard (Tab, arrows, Enter, Space, Escape) and an Xbox-style controller (D-pad
   or left stick, A, B) all work; the window can be dragged by any empty spot. */
#define YG__START_ID_MAIN 101
#define YG__START_ID_LATER 102
#define YG__START_ID_CLOSE 103
#define YG__START_TIMER 1

/* Colours as 0xRRGGBB, from the mock. */
#define YG__START_C_SURFACE 0x212121u
#define YG__START_C_BORDER 0x3a3a3au
#define YG__START_C_TITLE 0xffffffu
#define YG__START_C_BODY 0xd0d0d0u
#define YG__START_C_FACT 0xeeeeeeu
#define YG__START_C_RED 0xff0000u
#define YG__START_C_WHITE 0xffffffu
#define YG__START_C_MAIN 0xf1f1f1u
#define YG__START_C_MAIN_HOT 0xffffffu
#define YG__START_C_MAIN_DOWN 0xd9d9d9u
#define YG__START_C_MAIN_TEXT 0x0f0f0fu
#define YG__START_C_LATER 0x383838u
#define YG__START_C_LATER_HOT 0x474747u
#define YG__START_C_LATER_DOWN 0x2e2e2eu
#define YG__START_C_LATER_TEXT 0xf1f1f1u
#define YG__START_C_X 0x8a8a8au
#define YG__START_C_X_HOT 0xe6e6e6u
#define YG__START_C_FOCUS 0x4da3ffu

typedef struct yg__start_xi {
  DWORD packet;
  WORD buttons;
  BYTE left_trigger, right_trigger;
  SHORT lx, ly, rx, ry;
} yg__start_xi;
typedef DWORD (WINAPI *yg__start_xi_fn)(DWORD, yg__start_xi *);

typedef struct yg__start_ui {
  const yg__start_win *w;
  float s;                       /* pixels per mock pixel (the monitor's DPI / 96) */
  int cw, ch;                    /* client size */
  int side;                      /* the logo panel's width */
  RECT title, body, close, main, later;
  int facts_y, logo_y, logo, brand_y, ring;
  int rounded;                   /* Windows 11 rounds the corners and draws the border itself */
  HFONT f_title, f_body, f_fact, f_button, f_brand;
  HDC dc;                        /* the back buffer: the window without its buttons */
  HBITMAP bmp, old_bmp;
  DWORD *bits;
  HWND hwnd;
  int hot, down;                 /* the button under the mouse, and the one held */
  int choice, done;
  HMODULE xinput;
  yg__start_xi_fn xi_get;
  WORD xi_last;
  int xi_armed;                  /* every controller button was seen released since the window opened */
  WNDPROC button_proc;
  HICON icon_big, icon_small;
} yg__start_ui;
static yg__start_ui yg__start_u;

/* Get YouGame runs on a worker after the window returns. The installer is downloaded over
   HTTPS, held and checked, then run silently per-user. The worker owns the checked tray start and
   the download-page fallback; it never posts to a window or reads UI state. */
#define YG__START_GET_WAIT_MS 600000 /* a silent install still running after 10 minutes counts as failed */
#define YG__START_GET_CHUNK 65536

/* winhttp.dll's functions, looked up at run time; HINTERNET is a void pointer. */
typedef void *yg__start_net;
typedef BOOL (WINAPI *yg__start_net_close_fn)(yg__start_net);

typedef yg__start_net (WINAPI *yg__start_net_open_fn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef yg__start_net (WINAPI *yg__start_net_connect_fn)(yg__start_net, LPCWSTR, WORD, DWORD);
typedef yg__start_net (WINAPI *yg__start_net_request_fn)(yg__start_net, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR,
                                                          LPCWSTR *, DWORD);
typedef BOOL (WINAPI *yg__start_net_send_fn)(yg__start_net, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *yg__start_net_receive_fn)(yg__start_net, LPVOID);
typedef BOOL (WINAPI *yg__start_net_query_fn)(yg__start_net, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *yg__start_net_read_fn)(yg__start_net, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *yg__start_net_timeouts_fn)(yg__start_net, int, int, int, int);

/* %LOCALAPPDATA%\YouGame Start\<name>, the folder made when missing. */
static int yg__start_local_path(const wchar_t *name, wchar_t *path) {
  typedef HRESULT (WINAPI *known_fn)(const GUID *, DWORD, HANDLE, PWSTR *);
  typedef void (WINAPI *free_fn)(LPVOID);
  static const GUID local = /* FOLDERID_LocalAppData */
    {0xf1b32785, 0x6fba, 0x4fcf, {0x9d, 0x55, 0x7b, 0x8e, 0x7f, 0x15, 0x70, 0x91}};
  static const wchar_t folder[] = L"\\YouGame Start\\";
  HMODULE shell = yg__start_system_dll(L"shell32.dll");
  HMODULE ole = yg__start_system_dll(L"ole32.dll");
  known_fn known = NULL;
  free_fn release = NULL;
  PWSTR root = NULL;
  int ok = 0;
  if (shell && ole) {
    YG__START_PROC(known, shell, "SHGetKnownFolderPath");
    YG__START_PROC(release, ole, "CoTaskMemFree");
  }
  if (known && release && known(&local, 0, NULL, &root) == S_OK && root) {
    size_t at = wcslen(root);
    if (at + wcslen(folder) + wcslen(name) + 1 <= YG__START_PATH_CAP) {
      memcpy(path, root, at * sizeof(wchar_t));
      memcpy(path + at, folder, sizeof folder);
      at += wcslen(folder);
      path[at - 1] = 0;
      CreateDirectoryW(path, NULL);
      path[at - 1] = L'\\';
      wcscpy(path + at, name);
      ok = 1;
    }
  }
  if (root) release(root);
  if (ole) FreeLibrary(ole);
  if (shell) FreeLibrary(shell);
  return ok;
}

/* The installer, whole: status 200, a Content-Length between 1 MB and 1 GB, and exactly that many
   bytes written. A partial file is removed. */
static int yg__start_get_download(const wchar_t *path, int staging) {
  HMODULE dll = yg__start_system_dll(L"winhttp.dll");
  yg__start_net_open_fn net_open = NULL;
  yg__start_net_connect_fn net_connect = NULL;
  yg__start_net_request_fn net_request = NULL;
  yg__start_net_send_fn net_send = NULL;
  yg__start_net_receive_fn net_receive = NULL;
  yg__start_net_query_fn net_query = NULL;
  yg__start_net_read_fn net_read = NULL;
  yg__start_net_close_fn net_close = NULL;
  yg__start_net_timeouts_fn net_timeouts = NULL;
  static const wchar_t agent[] = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) YouGameStart/1";
  yg__start_net s = NULL, c = NULL, r = NULL;
  HANDLE f = INVALID_HANDLE_VALUE;
  DWORD status = 0, total = 0, got = 0, n = 0, len, wrote;
  char *buf = NULL;
  int ok = 0;
  if (!dll) return 0;
  YG__START_PROC(net_open, dll, "WinHttpOpen");
  YG__START_PROC(net_connect, dll, "WinHttpConnect");
  YG__START_PROC(net_request, dll, "WinHttpOpenRequest");
  YG__START_PROC(net_send, dll, "WinHttpSendRequest");
  YG__START_PROC(net_receive, dll, "WinHttpReceiveResponse");
  YG__START_PROC(net_query, dll, "WinHttpQueryHeaders");
  YG__START_PROC(net_read, dll, "WinHttpReadData");
  YG__START_PROC(net_close, dll, "WinHttpCloseHandle");
  YG__START_PROC(net_timeouts, dll, "WinHttpSetTimeouts");
  if (!net_open || !net_connect || !net_request || !net_send || !net_receive || !net_query || !net_read ||
      !net_close || !net_timeouts)
    goto done;
  s = net_open(agent, 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, Windows 8.1+ */, NULL, NULL, 0);
  if (!s) s = net_open(agent, 0 /* WINHTTP_ACCESS_TYPE_DEFAULT_PROXY */, NULL, NULL, 0);
  if (!s) goto done;
  net_timeouts(s, 15000, 15000, 30000, 30000);
  c = net_connect(s, staging ? L"staging.yougame.co" : L"yougame.co", 443, 0);
  /* Both sites are asked for the start installer, the full app's, checked before it runs. */
  if (c) r = net_request(c, L"GET", L"/api/desktop/download?file=YouGame-Start-windows-x64.exe",
                         NULL, NULL, NULL, 0x00800000 /* WINHTTP_FLAG_SECURE */);
  if (!r || !net_send(r, NULL, 0, NULL, 0, 0, 0) || !net_receive(r, NULL)) goto done;
  len = sizeof status;
  if (!net_query(r, 19 | 0x20000000 /* STATUS_CODE | FLAG_NUMBER */, NULL, &status, &len, NULL) || status != 200)
    goto done;
  len = sizeof total;
  if (!net_query(r, 5 | 0x20000000 /* CONTENT_LENGTH | FLAG_NUMBER */, NULL, &total, &len, NULL) ||
      total < 1000000UL || total > 1000000000UL)
    goto done;
  buf = (char *)malloc(YG__START_GET_CHUNK);
  if (!buf) goto done;
  f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) goto done;
  for (;;) {
    LONG pct;
    if (!net_read(r, buf, YG__START_GET_CHUNK, &n)) goto done;
    if (!n) break;
    if (n > total - got || !WriteFile(f, buf, n, &wrote, NULL) || wrote != n) goto done;
    got += n;
    pct = (LONG)((double)got * 100.0 / (double)total);
    if (pct != YG__START_GET_READ(&yg__start_g.pct))
      YG__START_GET_SET(&yg__start_g.pct, pct);
  }
  ok = got == total;
done:
  free(buf);
  if (f != INVALID_HANDLE_VALUE) {
    CloseHandle(f);
    if (!ok) DeleteFileW(path);
  }
  if (r) net_close(r);
  if (c) net_close(c);
  if (s) net_close(s);
  /* winhttp.dll stays loaded: it may still finish closing in the background. */
  return ok;
}

/* Holds the downloaded installer (others may read it, nobody may change or remove it), checks its
   Fluo Inc. signature, and runs that same file (yg__start_image_is_held, as for the tray start) with
   /S, from its own folder, in the clean user environment, out of the game's job when the job allows
   it. The worker waits until it exits, so another call cannot start a second installer beside a
   slow one. 1 = it exited with 0; 0 = anything else. */
static int yg__start_get_install(const wchar_t *path) {
  const DWORD flags =
    DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED |
    CREATE_UNICODE_ENVIRONMENT;
  yg__start_app app;
  wchar_t line[YG__START_PATH_CAP + 8], dir[YG__START_PATH_CAP];
  wchar_t *env;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  DWORD code = 1;
  size_t n = wcslen(path), cut;
  int attempt, started = 0, ok = 0;
  if (n + 6 >= YG__START_PATH_CAP) return 0;
  memset(&app, 0, sizeof app);
  wcscpy(app.path, path);
  app.file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (app.file == INVALID_HANDLE_VALUE) return 0;
  for (cut = n; cut && path[cut - 1] != L'\\'; --cut) {}
  if (cut > 3) --cut;
  if (cut < 3 || yg__start_app_signed_from(&app, 1) <= 0 || (env = yg__start_env_block()) == NULL) {
    CloseHandle(app.file);
    return 0;
  }
  memcpy(dir, path, cut * sizeof(wchar_t));
  dir[cut] = 0;
  for (attempt = 0; attempt < 2 && !started; ++attempt) {
    line[0] = L'"';
    memcpy(line + 1, path, n * sizeof(wchar_t));
    wcscpy(line + n + 1, L"\" /S");
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    started = CreateProcessW(app.path, line, NULL, NULL, FALSE,
                             attempt == 0 ? flags | CREATE_BREAKAWAY_FROM_JOB : flags, env, dir, &si, &pi) != 0;
    if (!started && GetLastError() != ERROR_ACCESS_DENIED) break;
  }
  HeapFree(GetProcessHeap(), 0, env);
  if (started) {
    if (yg__start_image_is_held(&app, pi.hProcess) && ResumeThread(pi.hThread) != (DWORD)-1) {
      DWORD waited = WaitForSingleObject(pi.hProcess, YG__START_GET_WAIT_MS);
      ok = waited == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code) && code == 0;
    } else if (TerminateProcess(pi.hProcess, 1)) {
      (void)WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  CloseHandle(app.file);
  return ok;
}

/* "YouGame Setup <pid>.exe" into out (at least 40 wide characters). */
static void yg__start_get_file_name(wchar_t *out, DWORD pid) {
  static const wchar_t head[] = L"YouGame Setup ";
  static const wchar_t tail[] = L".exe";
  wchar_t digits[11];
  int n = 0, i = 0, k;
  do { digits[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; } while (pid && n < 10);
  for (k = 0; head[k]; ++k) out[i++] = head[k];
  while (n) out[i++] = digits[--n];
  for (k = 0; tail[k]; ++k) out[i++] = tail[k];
  out[i] = 0;
}

static DWORD WINAPI yg__start_get_run(LPVOID unused) {
  typedef HRESULT (WINAPI *init_fn)(LPVOID, DWORD);
  typedef void (WINAPI *uninit_fn)(void);
  HMODULE ole = yg__start_system_dll(L"ole32.dll");
  init_fn init = NULL;
  uninit_fn uninit = NULL;
  HRESULT com = E_FAIL;
  wchar_t path[YG__START_PATH_CAP];
  wchar_t name[40];
  int got = 0;
  (void)unused;
  /* ShellExecute's https handler may need COM on this worker, even if the game thread has it. */
  if (ole) {
    YG__START_PROC(init, ole, "CoInitializeEx");
    YG__START_PROC(uninit, ole, "CoUninitialize");
    if (init && uninit) com = init(NULL, 2 /* COINIT_APARTMENTTHREADED */);
  }
  /* One file per game process, so two games getting YouGame at once never share it. */
  yg__start_get_file_name(name, GetCurrentProcessId());
  if (yg__start_local_path(name, path) &&
      yg__start_get_download(path, yg__start_g.staging)) {
    yg__start_get_note(YG__START_GET_INSTALLING); /* signature check and silent install */
    got = yg__start_running(yg__start_g.staging) || yg__start_get_install(path);
    DeleteFileW(path);
  }
  yg__start_get_finish(got);
  if (SUCCEEDED(com)) uninit();
  if (ole) FreeLibrary(ole);
  return 0;
}

static int yg__start_get_launch(void) {
  DWORD id;
  HANDLE thread = CreateThread(NULL, 0, yg__start_get_run, NULL, 0, &id);
  if (!thread) return 0;
  CloseHandle(thread); /* the worker runs independently of the window */
  return 1;
}

static float yg__start_clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

/* Coverage of a pixel by a shape whose signed distance at the pixel's centre is d (negative inside). */
static float yg__start_cover(float d) { return yg__start_clamp01(0.5f - d); }

/* Blends colour c over one 0x00RRGGBB pixel by a (0..1). */
static void yg__start_blend(DWORD *px, unsigned c, float a) {
  unsigned d = (unsigned)*px, r, g, b;
  if (a <= 0.f) return;
  if (a >= 1.f) { *px = c & 0xffffffu; return; }
  r = (unsigned)(((c >> 16) & 255) * a + ((d >> 16) & 255) * (1.f - a) + .5f);
  g = (unsigned)(((c >> 8) & 255) * a + ((d >> 8) & 255) * (1.f - a) + .5f);
  b = (unsigned)((c & 255) * a + (d & 255) * (1.f - a) + .5f);
  *px = (r << 16) | (g << 8) | b;
}

/* Signed distance to a rounded rectangle (x, y, w, h, corner r). */
static float yg__start_sd_rrect(float px, float py, float x, float y, float w, float h, float r) {
  float hx = w * .5f, hy = h * .5f, qx = (float)fabs(px - (x + hx)) - hx + r, qy = (float)fabs(py - (y + hy)) - hy + r;
  float ox = qx > 0.f ? qx : 0.f, oy = qy > 0.f ? qy : 0.f, in = qx > qy ? qx : qy;
  return (float)sqrt(ox * ox + oy * oy) + (in < 0.f ? in : 0.f) - r;
}

/* Fills a rounded rectangle into a w-wide pixel buffer, clipped to [0,bw) x [0,bh). */
static void yg__start_fill_rrect(DWORD *bits, int bw, int bh, float x, float y, float w, float h, float r,
                                 unsigned c) {
  int x0 = (int)x - 1, y0 = (int)y - 1, x1 = (int)(x + w) + 2, y1 = (int)(y + h) + 2, i, j;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > bw) x1 = bw;
  if (y1 > bh) y1 = bh;
  for (j = y0; j < y1; ++j)
    for (i = x0; i < x1; ++i)
      yg__start_blend(&bits[j * bw + i], c, yg__start_cover(yg__start_sd_rrect(i + .5f, j + .5f, x, y, w, h, r)));
}

static void yg__start_fill_circle(DWORD *bits, int bw, int bh, float cx, float cy, float r, unsigned c) {
  yg__start_fill_rrect(bits, bw, bh, cx - r, cy - r, 2 * r, 2 * r, r, c);
}

/* A stroked line drawing (round ends and joins): segments in a 24-unit box, drawn at (x, y), size px. */
#define YG__START_SEGS 64
typedef struct yg__start_path { int n; float p[YG__START_SEGS][4]; } yg__start_path;

static void yg__start_seg(yg__start_path *p, float ax, float ay, float bx, float by) {
  if (p->n >= YG__START_SEGS) return;
  p->p[p->n][0] = ax; p->p[p->n][1] = ay; p->p[p->n][2] = bx; p->p[p->n][3] = by;
  ++p->n;
}

/* An arc of a circle from angle a0 to a1 (radians, y down), as 10 segments. */
static void yg__start_arc(yg__start_path *p, float cx, float cy, float r, float a0, float a1) {
  int i;
  for (i = 0; i < 10; ++i) {
    float t0 = a0 + (a1 - a0) * i / 10.f, t1 = a0 + (a1 - a0) * (i + 1) / 10.f;
    yg__start_seg(p, cx + r * (float)cos(t0), cy + r * (float)sin(t0), cx + r * (float)cos(t1), cy + r * (float)sin(t1));
  }
}

static void yg__start_bezier(yg__start_path *p, float x0, float y0, float x1, float y1, float x2, float y2,
                             float x3, float y3) {
  float lx = x0, ly = y0;
  int i;
  for (i = 1; i <= 8; ++i) {
    float t = i / 8.f, u = 1.f - t;
    float x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
    float y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
    yg__start_seg(p, lx, ly, x, y);
    lx = x;
    ly = y;
  }
}

static void yg__start_stroke(DWORD *bits, int bw, int bh, const yg__start_path *p, float x, float y,
                             float size, float width, unsigned c) {
  float k = size / 24.f, half = width * k * .5f;
  int x0 = (int)x - 1, y0 = (int)y - 1, x1 = (int)(x + size) + 2, y1 = (int)(y + size) + 2, i, j, s;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > bw) x1 = bw;
  if (y1 > bh) y1 = bh;
  for (j = y0; j < y1; ++j)
    for (i = x0; i < x1; ++i) {
      float px = (i + .5f - x) / k, py = (j + .5f - y) / k, best = 1e9f;
      for (s = 0; s < p->n; ++s) {
        float ax = p->p[s][0], ay = p->p[s][1], dx = p->p[s][2] - ax, dy = p->p[s][3] - ay;
        float len2 = dx * dx + dy * dy, t = len2 > 0.f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f, ex, ey, d;
        t = yg__start_clamp01(t);
        ex = px - (ax + t * dx);
        ey = py - (ay + t * dy);
        d = ex * ex + ey * ey;
        if (d < best) best = d;
      }
      yg__start_blend(&bits[j * bw + i], c, yg__start_cover((float)sqrt(best) * k - half));
    }
}

/* The three icons of the facts row (the mock's match2, friends and rank drawings). */
static void yg__start_icon(int which, yg__start_path *p) {
  const float pi = 3.14159265f;
  p->n = 0;
  if (which == 0) { /* Matchmaking: crossed swords */
    yg__start_seg(p, 4.5f, 4.5f, 15, 15); yg__start_seg(p, 12.5f, 17.5f, 17.5f, 12.5f);
    yg__start_seg(p, 16, 16, 19.5f, 19.5f); yg__start_seg(p, 19.5f, 4.5f, 9, 15);
    yg__start_seg(p, 6.5f, 12.5f, 11.5f, 17.5f); yg__start_seg(p, 8, 16, 4.5f, 19.5f);
  } else if (which == 1) { /* Friends: two people */
    yg__start_arc(p, 9, 8, 3.5f, 0, 2 * pi);
    yg__start_bezier(p, 2.5f, 20, 3.3f, 16.5f, 5.9f, 14.5f, 9, 14.5f);
    yg__start_bezier(p, 9, 14.5f, 12.1f, 14.5f, 14.7f, 16.5f, 15.5f, 20);
    yg__start_arc(p, 17, 9, 2.5f, 0, 2 * pi);
    yg__start_bezier(p, 16, 14.6f, 18.6f, 14.8f, 20.6f, 16.6f, 21.2f, 19.5f);
  } else { /* Ranked: a cup */
    yg__start_seg(p, 7, 4, 17, 4); yg__start_seg(p, 17, 4, 17, 9);
    yg__start_arc(p, 12, 9, 5, 0, pi);
    yg__start_seg(p, 7, 9, 7, 4); yg__start_seg(p, 12, 14, 12, 18); yg__start_seg(p, 8, 20, 16, 20);
    yg__start_seg(p, 7, 6, 4, 6); yg__start_seg(p, 4, 6, 4, 7); yg__start_arc(p, 7, 7, 3, pi, pi / 2);
    yg__start_seg(p, 17, 6, 20, 6); yg__start_seg(p, 20, 6, 20, 7); yg__start_arc(p, 17, 7, 3, 0, pi / 2);
  }
}

/* YouGame's logo (the site's 192-unit drawing) at (x, y), size px. */
static void yg__start_logo(DWORD *bits, int bw, int bh, float x, float y, float size) {
  float k = size / 192.f;
  yg__start_fill_rrect(bits, bw, bh, x, y, 192 * k, 192 * k, 44 * k, YG__START_C_RED);
  yg__start_fill_rrect(bits, bw, bh, x + 34 * k, y + 66 * k, 124 * k, 60 * k, 30 * k, YG__START_C_WHITE);
  yg__start_fill_rrect(bits, bw, bh, x + 56 * k, y + 90 * k, 24 * k, 8 * k, 2 * k, YG__START_C_RED);
  yg__start_fill_rrect(bits, bw, bh, x + 64 * k, y + 82 * k, 8 * k, 24 * k, 2 * k, YG__START_C_RED);
  yg__start_fill_circle(bits, bw, bh, x + 118 * k, y + 88 * k, 6 * k, YG__START_C_RED);
  yg__start_fill_circle(bits, bw, bh, x + 130 * k, y + 102 * k, 6 * k, YG__START_C_RED);
}

/* ASCII to wide. Returns the length, or 0 if it does not fit. */
static int yg__start_wide(const char *s, wchar_t *out, int cap) {
  int n = 0;
  for (; *s; ++s) {
    if (n + 1 >= cap) return 0;
    out[n++] = (wchar_t)(unsigned char)*s;
  }
  out[n] = 0;
  return n;
}

static HFONT yg__start_font(float px, int weight) {
  return CreateFontW(-(int)(px + .5f), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

/* The height of text in font f wrapped to width (or one line when width is 0), and its width.
   DT_EDITCONTROL also breaks a word wider than the line (a long name with no spaces), here and
   where the text is drawn, so the window grows to fit instead of cutting it off. */
static int yg__start_measure(HDC dc, HFONT f, const char *s, int width, int *out_w) {
  wchar_t t[YG__START_BODY_CAP];
  RECT r;
  HGDIOBJ old = SelectObject(dc, f);
  r.left = r.top = 0;
  r.right = width ? width : 4000;
  r.bottom = 0;
  if (yg__start_wide(s, t, YG__START_BODY_CAP))
    DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_NOPREFIX | (width ? DT_WORDBREAK | DT_EDITCONTROL : DT_SINGLELINE));
  SelectObject(dc, old);
  if (out_w) *out_w = r.right - r.left;
  return r.bottom - r.top;
}

static void yg__start_text(HDC dc, HFONT f, const char *s, RECT *r, unsigned c, UINT how) {
  wchar_t t[YG__START_BODY_CAP];
  HGDIOBJ old = SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB((c >> 16) & 255, (c >> 8) & 255, c & 255));
  if (yg__start_wide(s, t, YG__START_BODY_CAP)) DrawTextW(dc, t, -1, r, how | DT_NOPREFIX);
  SelectObject(dc, old);
}

static void yg__start_ui_fonts(yg__start_ui *u) {
  float s = u->s;
  u->f_title = yg__start_font(19 * s, FW_SEMIBOLD);
  u->f_body = yg__start_font(13.5f * s, FW_NORMAL);
  u->f_fact = yg__start_font(12.5f * s, FW_SEMIBOLD);
  u->f_button = yg__start_font(14 * s, FW_SEMIBOLD);
  u->f_brand = yg__start_font(14 * s, FW_BOLD);
}

static void yg__start_ui_free(yg__start_ui *u) {
  if (u->dc) {
    if (u->old_bmp) SelectObject(u->dc, u->old_bmp);
    DeleteDC(u->dc);
  }
  if (u->bmp) DeleteObject(u->bmp);
  if (u->f_title) DeleteObject(u->f_title);
  if (u->f_body) DeleteObject(u->f_body);
  if (u->f_fact) DeleteObject(u->f_fact);
  if (u->f_button) DeleteObject(u->f_button);
  if (u->f_brand) DeleteObject(u->f_brand);
  u->dc = NULL;
  u->bmp = u->old_bmp = NULL;
  u->bits = NULL;
  u->f_title = u->f_body = u->f_fact = u->f_button = u->f_brand = NULL;
}

/* A 32-bit top-down DIB section selected into a new memory DC. */
static int yg__start_dib(int w, int h, HDC *dc, HBITMAP *bmp, HBITMAP *old, DWORD **bits) {
  BITMAPINFO bi;
  void *p = NULL;
  memset(&bi, 0, sizeof bi);
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  *dc = CreateCompatibleDC(NULL);
  if (!*dc) return 0;
  *bmp = CreateDIBSection(*dc, &bi, DIB_RGB_COLORS, &p, NULL, 0);
  if (!*bmp || !p) {
    if (*bmp) DeleteObject(*bmp);
    *bmp = NULL;
    DeleteDC(*dc);
    *dc = NULL;
    return 0;
  }
  *old = (HBITMAP)SelectObject(*dc, *bmp);
  *bits = (DWORD *)p;
  return 1;
}

/* Lays the window out at scale u->s (fonts made first) and draws everything but the buttons into
   the back buffer. 0 on failure. */
static int yg__start_ui_build(yg__start_ui *u) {
  const yg__start_win *w = u->w;
  float s = u->s;
  HDC probe = CreateCompatibleDC(NULL);
  int x0, x1, y, th, bh, tw, mw = 0, lw = 0, btn_h, gap = (int)(10 * s + .5f), i, j;
  if (!probe) return 0;
  u->cw = (int)(480 * s + .5f);
  u->side = (int)(128 * s + .5f);
  x0 = u->side + (int)(20 * s + .5f);
  x1 = u->cw - (int)(20 * s + .5f);
  y = (int)(20 * s + .5f);
  th = yg__start_measure(probe, u->f_title, w->title, x1 - x0 - (int)(22 * s + .5f), NULL);
  SetRect(&u->title, x0, y, x1 - (int)(22 * s + .5f), y + th);
  y += th + (int)(6 * s + .5f);
  bh = yg__start_measure(probe, u->f_body, w->body, x1 - x0, NULL);
  SetRect(&u->body, x0, y, x1, y + bh);
  y += bh;
  if (w->facts) {
    y += (int)(14 * s + .5f);
    u->facts_y = y;
    y += (int)(18 * s + .5f);
  }
  y += (int)(16 * s + .5f);
  btn_h = yg__start_measure(probe, u->f_button, "Ag", 0, NULL) + (int)(16 * s + .5f);
  yg__start_measure(probe, u->f_button, w->main_label, 0, &mw);
  if (w->later_label) yg__start_measure(probe, u->f_button, w->later_label, 0, &lw);
  SetRect(&u->main, x1 - mw - (int)(36 * s + .5f), y, x1, y + btn_h);
  if (w->later_label) SetRect(&u->later, u->main.left - gap - lw - (int)(32 * s + .5f), y, u->main.left - gap, y + btn_h);
  else SetRectEmpty(&u->later);
  /* The child windows are the pills plus room for the focus ring around them. */
  u->ring = (int)(3 * s + .5f);
  InflateRect(&u->main, u->ring, u->ring);
  if (w->later_label) InflateRect(&u->later, u->ring, u->ring);
  y += btn_h + (int)(18 * s + .5f);
  u->logo = (int)(56 * s + .5f);
  /* The panel's name, "YouGame", on one line. */
  tw = yg__start_measure(probe, u->f_brand, "YouGame", 0, NULL);
  if (y < u->logo + tw + (int)(60 * s + .5f)) y = u->logo + tw + (int)(60 * s + .5f);
  u->ch = y;
  u->logo_y = (u->ch - (u->logo + (int)(10 * s + .5f) + tw)) / 2;
  u->brand_y = u->logo_y + u->logo + (int)(10 * s + .5f);
  {
    int c = (int)(28 * s + .5f), r = u->cw - (int)(8 * s + .5f), t = (int)(8 * s + .5f);
    SetRect(&u->close, r - c, t, r, t + c);
  }
  DeleteDC(probe);
  if (!yg__start_dib(u->cw, u->ch, &u->dc, &u->bmp, &u->old_bmp, &u->bits)) return 0;

  /* Surface, then the panel's gradient (the mock's 165deg: #5e1118, #35151a at 55%, #251a1b). */
  for (i = 0; i < u->cw * u->ch; ++i) u->bits[i] = YG__START_C_SURFACE;
  {
    const float dx = .2588f, dy = .9659f, len = u->side * dx + u->ch * dy;
    for (j = 0; j < u->ch; ++j)
      for (i = 0; i < u->side; ++i) {
        float t = ((i + .5f - u->side * .5f) * dx + (j + .5f - u->ch * .5f) * dy) / len + .5f;
        unsigned a, b;
        float k;
        if (t < .55f) { a = 0x5e1118u; b = 0x35151au; k = yg__start_clamp01(t / .55f); }
        else { a = 0x35151au; b = 0x251a1bu; k = yg__start_clamp01((t - .55f) / .45f); }
        u->bits[j * u->cw + i] = a;
        yg__start_blend(&u->bits[j * u->cw + i], b, k);
      }
  }
  yg__start_logo(u->bits, u->cw, u->ch, (u->side - u->logo) * .5f, (float)u->logo_y, (float)u->logo);
  if (w->facts) {
    static const char *const names[3] = {"Matchmaking", "Friends", "Ranked"};
    int x = x0, k;
    for (k = 0; k < 3; ++k) {
      yg__start_path p;
      int lwid = 0, lh = yg__start_measure(u->dc, u->f_fact, names[k], 0, &lwid);
      RECT r;
      yg__start_icon(k, &p);
      yg__start_stroke(u->bits, u->cw, u->ch, &p, (float)x, (float)u->facts_y, 18 * s, 2, YG__START_C_FACT);
      x += (int)(23 * s + .5f);
      SetRect(&r, x, u->facts_y + ((int)(18 * s + .5f) - lh) / 2, x + lwid + 2, u->facts_y + (int)(18 * s + .5f));
      yg__start_text(u->dc, u->f_fact, names[k], &r, YG__START_C_FACT, DT_SINGLELINE | DT_LEFT | DT_TOP);
      x += lwid + (int)(14 * s + .5f);
    }
  }
  GdiFlush();
  yg__start_text(u->dc, u->f_title, w->title, &u->title, YG__START_C_TITLE, DT_WORDBREAK | DT_EDITCONTROL | DT_LEFT | DT_TOP);
  yg__start_text(u->dc, u->f_body, w->body, &u->body, YG__START_C_BODY, DT_WORDBREAK | DT_EDITCONTROL | DT_LEFT | DT_TOP);
  {
    RECT r;
    SetRect(&r, 0, u->brand_y, u->side, u->ch);
    yg__start_text(u->dc, u->f_brand, "YouGame", &r, YG__START_C_WHITE, DT_SINGLELINE | DT_CENTER | DT_TOP);
  }
  GdiFlush();
  if (!u->rounded) { /* Windows 10: square corners, a hairline border drawn here */
    for (i = 0; i < u->cw; ++i) u->bits[i] = u->bits[(u->ch - 1) * u->cw + i] = YG__START_C_BORDER;
    for (j = 0; j < u->ch; ++j) u->bits[j * u->cw] = u->bits[j * u->cw + u->cw - 1] = YG__START_C_BORDER;
  }
  return 1;
}

/* Draws one button: the back buffer behind its place src (window coordinates), then the pill or the
   close mark, into target at (dx, dy). hot: mouse over; down: pressed; focus: keyboard focus shown. */
static void yg__start_ui_button(yg__start_ui *u, int id, HDC target, const RECT *src, int dx, int dy,
                                int hot, int down, int focus) {
  HDC dc;
  HBITMAP bmp, old;
  DWORD *bits;
  int bw = src->right - src->left, bh = src->bottom - src->top;
  float s = u->s;
  if (bw <= 0 || bh <= 0 || !yg__start_dib(bw, bh, &dc, &bmp, &old, &bits)) return;
  BitBlt(dc, 0, 0, bw, bh, u->dc, src->left, src->top, SRCCOPY);
  GdiFlush();
  if (id == YG__START_ID_CLOSE) {
    yg__start_path p;
    float m = bw * .5f;
    if (hot || focus) yg__start_fill_circle(bits, bw, bh, m, bh * .5f, m - 1, 0x303030u);
    p.n = 0;
    yg__start_seg(&p, 7.5f, 7.5f, 16.5f, 16.5f);
    yg__start_seg(&p, 16.5f, 7.5f, 7.5f, 16.5f);
    yg__start_stroke(bits, bw, bh, &p, 0, 0, (float)bw, 1.6f * s * 24.f / (float)bw,
                     hot ? YG__START_C_X_HOT : YG__START_C_X);
  } else {
    int is_main = id == YG__START_ID_MAIN;
    unsigned fill = is_main ? (down ? YG__START_C_MAIN_DOWN : hot ? YG__START_C_MAIN_HOT : YG__START_C_MAIN)
                            : (down ? YG__START_C_LATER_DOWN : hot ? YG__START_C_LATER_HOT : YG__START_C_LATER);
    float g = (float)u->ring, pw = bw - 2 * g, ph = bh - 2 * g;
    if (focus) {
      float t = 2 * s; /* the ring: 2 px, 1 px clear of the pill */
      yg__start_fill_rrect(bits, bw, bh, g - t - s, g - t - s, pw + 2 * (t + s), ph + 2 * (t + s), ph * .5f + t + s, YG__START_C_FOCUS);
      yg__start_fill_rrect(bits, bw, bh, g - s, g - s, pw + 2 * s, ph + 2 * s, ph * .5f + s, YG__START_C_SURFACE);
    }
    yg__start_fill_rrect(bits, bw, bh, g, g, pw, ph, ph * .5f, fill);
    GdiFlush();
    {
      RECT t;
      SetRect(&t, 0, 0, bw, bh);
      yg__start_text(dc, u->f_button, is_main ? u->w->main_label : u->w->later_label, &t,
                     is_main ? YG__START_C_MAIN_TEXT : YG__START_C_LATER_TEXT, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
  }
  GdiFlush();
  BitBlt(target, dx, dy, bw, bh, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
}

/* The logo as an icon (taskbar, Alt+Tab), size px, with real alpha. */
static HICON yg__start_logo_icon(int size) {
  HDC dc;
  HBITMAP bmp, old, mask;
  DWORD *bits;
  ICONINFO ii;
  HICON icon = NULL;
  int i;
  if (!yg__start_dib(size, size, &dc, &bmp, &old, &bits)) return NULL;
  /* Drawn twice: once over black and once over white; alpha from the difference. */
  {
    DWORD *white = (DWORD *)malloc((size_t)size * size * sizeof(DWORD));
    if (!white) { SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc); return NULL; }
    for (i = 0; i < size * size; ++i) { bits[i] = 0; white[i] = 0xffffffu; }
    yg__start_logo(bits, size, size, 0, 0, (float)size);
    yg__start_logo(white, size, size, 0, 0, (float)size);
    for (i = 0; i < size * size; ++i) {
      unsigned a = 255 - (((white[i] >> 8) & 255) - ((bits[i] >> 8) & 255));
      unsigned r = (bits[i] >> 16) & 255, g = (bits[i] >> 8) & 255, b = bits[i] & 255;
      if (a) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
      bits[i] = (a << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
    }
    free(white);
  }
  GdiFlush();
  SelectObject(dc, old);
  {
    void *zero = calloc((size_t)((size + 15) / 16) * 2, (size_t)size); /* rows are WORD-aligned */
    mask = zero ? CreateBitmap(size, size, 1, 1, zero) : NULL;
    free(zero);
  }
  if (mask) {
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask = mask;
    icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
  }
  DeleteObject(bmp);
  DeleteDC(dc);
  return icon;
}

/* The player's choice. Closing the window (Esc, the X, Alt+F4) is Play offline: the player saw it and
   chose not to go online. */
static void yg__start_pick(HWND w, int id) {
  if (id == YG__START_ID_MAIN && YG__START_GET_INSTALL && yg__start_u.w->answer == YG__START_ANSWER_GET &&
      yg__start_u.w->which == YG__START_TEXT_MISSING &&
      yg__start_get_begin(yg__start_u.w->staging, yg__start_u.w->game)) {
    yg__start_u.choice = YG__START_ANSWER_GETTING;
    DestroyWindow(w);
    return;
  }
  if (id == YG__START_ID_MAIN) yg__start_u.choice = yg__start_u.w->answer;
  else yg__start_u.choice = YG__START_ANSWER_LATER;
  DestroyWindow(w);
}

/* Moves focus between the two buttons and shows the focus ring (keyboard or controller). */
static void yg__start_focus(HWND w, int id) {
  HWND b = GetDlgItem(w, id);
  if (!b) return;
  SendMessageW(w, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
  SetFocus(b);
  InvalidateRect(GetDlgItem(w, YG__START_ID_MAIN), NULL, FALSE);
  if (GetDlgItem(w, YG__START_ID_LATER)) InvalidateRect(GetDlgItem(w, YG__START_ID_LATER), NULL, FALSE);
}

static int yg__start_focused_id(HWND w) {
  HWND f = GetFocus();
  if (f && GetParent(f) == w) return GetDlgCtrlID(f);
  return 0;
}

/* The controller: D-pad or left stick left/right moves between the buttons, A presses, B is the
   other button (Play offline, or Quit). Nothing counts until every button was seen released, so the
   press that opened the window (A on the game's Online) never answers it. Only while it is in front. */
static void yg__start_pad(HWND w) {
  yg__start_ui *u = &yg__start_u;
  yg__start_xi st;
  WORD now = 0, fresh;
  DWORD i;
  if (!u->xi_get) return;
  for (i = 0; i < 4; ++i) {
    memset(&st, 0, sizeof st);
    if (u->xi_get(i, &st) == ERROR_SUCCESS) {
      now |= st.buttons;
      if (st.lx < -16000) now |= 0x0004;
      if (st.lx > 16000) now |= 0x0008;
    }
  }
  if (!u->xi_armed) {
    if ((now & 0x300c) == 0) u->xi_armed = 1;
    u->xi_last = now;
    return;
  }
  fresh = (WORD)(now & ~u->xi_last);
  u->xi_last = now;
  if (GetForegroundWindow() != w || !fresh) return;
  if (fresh & 0x2000) { yg__start_pick(w, YG__START_ID_LATER); return; }               /* B */
  if (fresh & 0x1000) { yg__start_pick(w, yg__start_focused_id(w) == YG__START_ID_LATER ? YG__START_ID_LATER : YG__START_ID_MAIN); return; } /* A */
  if ((fresh & 0x0004) && GetDlgItem(w, YG__START_ID_LATER)) yg__start_focus(w, YG__START_ID_LATER); /* left */
  if (fresh & 0x0008) yg__start_focus(w, YG__START_ID_MAIN);                           /* right */
}

static LRESULT CALLBACK yg__start_button_proc(HWND b, UINT m, WPARAM wp, LPARAM lp) {
  yg__start_ui *u = &yg__start_u;
  int id = GetDlgCtrlID(b);
  switch (m) {
    case WM_MOUSEMOVE:
      if (u->hot != id) {
        TRACKMOUSEEVENT t;
        HWND prev = u->hot ? GetDlgItem(GetParent(b), u->hot) : NULL;
        u->hot = id;
        memset(&t, 0, sizeof t);
        t.cbSize = sizeof t;
        t.dwFlags = TME_LEAVE;
        t.hwndTrack = b;
        TrackMouseEvent(&t);
        if (prev) InvalidateRect(prev, NULL, FALSE);
        InvalidateRect(b, NULL, FALSE);
      }
      break;
    case WM_MOUSELEAVE:
      if (u->hot == id) { u->hot = 0; InvalidateRect(b, NULL, FALSE); }
      break;
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND));
      return TRUE;
    default: break;
  }
  return CallWindowProcW(u->button_proc, b, m, wp, lp);
}

/* Puts the child buttons at their laid-out places. */
static void yg__start_ui_place(yg__start_ui *u) {
  HWND b;
  if ((b = GetDlgItem(u->hwnd, YG__START_ID_MAIN)) != NULL)
    MoveWindow(b, u->main.left, u->main.top, u->main.right - u->main.left, u->main.bottom - u->main.top, FALSE);
  if ((b = GetDlgItem(u->hwnd, YG__START_ID_LATER)) != NULL)
    MoveWindow(b, u->later.left, u->later.top, u->later.right - u->later.left, u->later.bottom - u->later.top, FALSE);
  if ((b = GetDlgItem(u->hwnd, YG__START_ID_CLOSE)) != NULL)
    MoveWindow(b, u->close.left, u->close.top, u->close.right - u->close.left, u->close.bottom - u->close.top, FALSE);
}

/* How many buttons are not exactly at their laid-out places (0: all placed). Read in the window's own
   pixels, so it must run while the thread is per-monitor aware, like the placing itself. A button left
   at its 1x1 start, or moved off the window (coordinates read at another scale), counts. */
static int yg__start_ui_misplaced(const yg__start_ui *u) {
  static const int ids[3] = {YG__START_ID_MAIN, YG__START_ID_LATER, YG__START_ID_CLOSE};
  RECT client;
  int k, bad = 0;
  if (!GetClientRect(u->hwnd, &client)) return 3;
  for (k = 0; k < 3; ++k) {
    const RECT *want = ids[k] == YG__START_ID_MAIN ? &u->main : ids[k] == YG__START_ID_LATER ? &u->later : &u->close;
    HWND b = GetDlgItem(u->hwnd, ids[k]);
    RECT r, in;
    if (!b) {
      if (ids[k] != YG__START_ID_LATER || !IsRectEmpty(want)) ++bad; /* only Play offline may be absent */
      continue;
    }
    if (!GetWindowRect(b, &r)) { ++bad; continue; }
    MapWindowPoints(NULL, u->hwnd, (POINT *)&r, 2);
    if (!EqualRect(&r, want) || !IntersectRect(&in, &r, &client) || !EqualRect(&in, &r) || !(GetWindowLongW(b, GWL_STYLE) & WS_VISIBLE)) ++bad;
  }
  return bad;
}

/* A build that wants to look at the shown window (the Windows screenshot check) defines this. */
#ifndef YG__START_ON_SHOWN
#define YG__START_ON_SHOWN(u) ((void)0)
#endif

static LRESULT CALLBACK yg__start_wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  yg__start_ui *u = &yg__start_u;
  switch (m) {
    case WM_COMMAND:
      if (LOWORD(wp) == YG__START_ID_MAIN) { yg__start_pick(w, YG__START_ID_MAIN); return 0; }
      if (LOWORD(wp) == YG__START_ID_LATER || LOWORD(wp) == YG__START_ID_CLOSE || LOWORD(wp) == IDCANCEL) {
        /* The administrator window's one button is its main button: same answer either way. */
        yg__start_pick(w, YG__START_ID_LATER);
        return 0;
      }
      break;
    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)lp;
      int id = (int)d->CtlID;
      const RECT *src = id == YG__START_ID_MAIN ? &u->main : id == YG__START_ID_LATER ? &u->later : &u->close;
      yg__start_ui_button(u, id, d->hDC, src, 0, 0, u->hot == id, (d->itemState & ODS_SELECTED) != 0,
                          (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT));
      return TRUE;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(w, &ps);
      if (u->dc) BitBlt(dc, 0, 0, u->cw, u->ch, u->dc, 0, 0, SRCCOPY);
      EndPaint(w, &ps);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: { /* drag the window by any empty spot */
      LRESULT h = DefWindowProcW(w, m, wp, lp);
      return h == HTCLIENT ? HTCAPTION : h;
    }
    case WM_DPICHANGED: { /* moved to a monitor with another scale: everything again at the new size */
      const RECT *to = (const RECT *)lp; /* where Windows suggests, so it stays on the new monitor */
      yg__start_ui_free(u);
      u->s = LOWORD(wp) / 96.f;
      yg__start_ui_fonts(u);
      if (!yg__start_ui_build(u)) { /* nothing left to show: no choice was made */
        u->choice = YG__START_ANSWER_NONE;
        DestroyWindow(w);
        return 0;
      }
      yg__start_ui_place(u);
      SetWindowPos(w, NULL, to->left, to->top, u->cw, u->ch, SWP_NOZORDER | SWP_NOACTIVATE);
      RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
      return 0;
    }
    case WM_TIMER:
      if (wp == YG__START_TIMER) yg__start_pad(w);
      return 0;
    case WM_UPDATEUISTATE: {
      LRESULT r = DefWindowProcW(w, m, wp, lp);
      InvalidateRect(w, NULL, FALSE);
      return r;
    }
    case WM_ACTIVATE: /* clicked in later (after a flash): focus the main button, so Enter works */
      if (LOWORD(wp) != WA_INACTIVE) {
        int f = yg__start_focused_id(w);
        if (!f) SetFocus(GetDlgItem(w, YG__START_ID_MAIN));
        return 0;
      }
      break;
    case WM_CLOSE: yg__start_pick(w, YG__START_ID_LATER); return 0;
    case WM_DESTROY:
      KillTimer(w, YG__START_TIMER);
      u->done = 1;
      return 0; /* never a WM_QUIT: that would end the game */
    default: break;
  }
  return DefWindowProcW(w, m, wp, lp);
}

typedef HRESULT (WINAPI *yg__start_dwm_fn)(HWND, DWORD, LPCVOID, DWORD);
typedef HANDLE (WINAPI *yg__start_dpictx_fn)(HANDLE);
typedef HRESULT (WINAPI *yg__start_mondpi_fn)(HMONITOR, int, UINT *, UINT *);

static HWND yg__start_child(HWND parent, int id, const char *label, DWORD style, HINSTANCE inst) {
  wchar_t t[64];
  HWND b;
  if (!yg__start_wide(label, t, 64)) return NULL;
  b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | style, 0, 0, 1, 1, parent,
                      (HMENU)(INT_PTR)id, inst, NULL);
  if (b) {
    WNDPROC was = (WNDPROC)SetWindowLongPtrW(b, GWLP_WNDPROC, (LONG_PTR)yg__start_button_proc);
    if (!yg__start_u.button_proc) yg__start_u.button_proc = was;
  }
  return b;
}

static int yg__start_offer_window(const yg__start_win *win) {
  static const wchar_t *cls = L"YouGameStartWindow";
  yg__start_ui *u = &yg__start_u;
  HINSTANCE inst = GetModuleHandleW(NULL);
  HMODULE user = GetModuleHandleW(L"user32.dll"), dll;
  yg__start_dpictx_fn set_ctx = NULL;
  HANDLE old_ctx = NULL;
  WNDCLASSW wc;
  HWND w, mainb;
  MSG msg;
  UINT dpi = 96;
  HMONITOR mon;
  MONITORINFO mi;
  wchar_t title[YG__START_TITLE_CAP];
  int x, y;
  memset(u, 0, sizeof *u);
  u->w = win;
  u->choice = YG__START_ANSWER_NONE;
  if (!yg__start_wide(win->title, title, YG__START_TITLE_CAP)) return YG__START_ANSWER_NONE;
  /* Drawn at the monitor's real scale (per-monitor DPI for this window only, the game's own
     setting untouched), on the monitor the player is looking at. */
  if (user) YG__START_PROC(set_ctx, user, "SetThreadDpiAwarenessContext");
  if (set_ctx) old_ctx = set_ctx((HANDLE)(INT_PTR)-4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
  mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
  if (old_ctx && (dll = yg__start_system_dll(L"shcore.dll")) != NULL) {
    yg__start_mondpi_fn get = NULL;
    UINT dx = 0, dy = 0;
    YG__START_PROC(get, dll, "GetDpiForMonitor");
    if (get && get(mon, 0, &dx, &dy) == S_OK && dx) dpi = dx;
    FreeLibrary(dll);
  } else {
    HDC screen = GetDC(NULL);
    if (screen) {
      dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
      ReleaseDC(NULL, screen);
    }
  }
  u->s = (dpi ? dpi : 96) / 96.f;
  memset(&wc, 0, sizeof wc);
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = yg__start_wndproc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
  wc.lpszClassName = cls;
  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    if (old_ctx) set_ctx(old_ctx);
    return YG__START_ANSWER_NONE;
  }
  w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, cls, title, WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1,
                      NULL, NULL, inst, NULL);
  if (!w) {
    if (old_ctx) set_ctx(old_ctx);
    UnregisterClassW(cls, inst);
    return YG__START_ANSWER_NONE;
  }
  u->hwnd = w;
  /* Windows 11 rounds the corners and draws a hairline in the window's own colour; Windows 10
     keeps square corners and the border drawn in the back buffer. */
  if ((dll = yg__start_system_dll(L"dwmapi.dll")) != NULL) {
    yg__start_dwm_fn set = NULL;
    YG__START_PROC(set, dll, "DwmSetWindowAttribute");
    if (set) {
      DWORD round = 2;                       /* DWMWCP_ROUND */
      COLORREF border = RGB(0x3a, 0x3a, 0x3a);
      u->rounded = set(w, 33, &round, sizeof round) == S_OK; /* DWMWA_WINDOW_CORNER_PREFERENCE */
      if (u->rounded) set(w, 34, &border, sizeof border);    /* DWMWA_BORDER_COLOR */
    }
    FreeLibrary(dll);
  }
  yg__start_ui_fonts(u);
  if (!u->f_title || !u->f_body || !u->f_fact || !u->f_button || !u->f_brand || !yg__start_ui_build(u)) {
    DestroyWindow(w);
    yg__start_ui_free(u);
    if (old_ctx) set_ctx(old_ctx);
    UnregisterClassW(cls, inst);
    return YG__START_ANSWER_NONE;
  }
  memset(&mi, 0, sizeof mi);
  mi.cbSize = sizeof mi;
  if (GetMonitorInfoW(mon, &mi)) {
    x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - u->cw) / 2;
    y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - u->ch) / 2;
  } else {
    x = (GetSystemMetrics(SM_CXSCREEN) - u->cw) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - u->ch) / 2;
  }
  SetWindowPos(w, NULL, x, y, u->cw, u->ch, SWP_NOZORDER | SWP_NOACTIVATE);
  if (win->later_label) yg__start_child(w, YG__START_ID_LATER, win->later_label, WS_TABSTOP, inst);
  mainb = yg__start_child(w, YG__START_ID_MAIN, win->main_label, WS_TABSTOP, inst);
  yg__start_child(w, YG__START_ID_CLOSE, "Close", 0, inst);
  if (!mainb) {
    DestroyWindow(w);
    yg__start_ui_free(u);
    if (old_ctx) set_ctx(old_ctx);
    UnregisterClassW(cls, inst);
    return YG__START_ANSWER_NONE;
  }
  /* The thread stays per-monitor aware until the window is placed and shown. Windows reads the coordinates of
     MoveWindow and SetWindowPos at the calling thread's scale: back in a DPI-unaware game's setting,
     a 150% screen would put the buttons at 1.5 times their places, past the window's edges (on a test
     machine the window drew with no buttons). */
  yg__start_ui_place(u);
  if (yg__start_ui_misplaced(u)) yg__start_ui_place(u);
  u->icon_big = yg__start_logo_icon(GetSystemMetrics(SM_CXICON));
  u->icon_small = yg__start_logo_icon(GetSystemMetrics(SM_CXSMICON));
  if (u->icon_big) SendMessageW(w, WM_SETICON, ICON_BIG, (LPARAM)u->icon_big);
  if (u->icon_small) SendMessageW(w, WM_SETICON, ICON_SMALL, (LPARAM)u->icon_small);
  /* The controller (Xbox-style, through the system's XInput), polled 20 times a second. */
  u->xinput = yg__start_system_dll(L"xinput1_4.dll");
  if (!u->xinput) u->xinput = yg__start_system_dll(L"xinput9_1_0.dll");
  if (u->xinput) YG__START_PROC(u->xi_get, u->xinput, "XInputGetState");
  if (u->xi_get) SetTimer(w, YG__START_TIMER, 50, NULL);
  SendMessageW(w, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0); /* ring after a key only */
  ShowWindow(w, SW_SHOWNORMAL);
  yg__start_front(w);
  SetFocus(mainb);
  YG__START_ON_SHOWN(u);
  /* Placed and shown: the game's setting again for the loop. Our own window procedure (placing on a
     scale change, the controller) runs in the window's setting anyway, while game code the loop
     reaches outside a window procedure (a timer callback with no window, hooks) keeps its own. */
  if (old_ctx) set_ctx(old_ctx);
  /* Its own short message loop, until the window is gone. It also pumps the game's windows on this
     thread. A WM_QUIT it receives is the game's (the player closed the game meanwhile): the small
     window goes, the quit is posted again for the game's own loop, and no choice was made.
     Enter and Escape count only when pressed here: a key still held from the game (its repeat) never
     answers the window. */
  while (!u->done) {
    BOOL got = GetMessageW(&msg, NULL, 0, 0);
    if (got == 0 || got == -1) {
      u->choice = YG__START_ANSWER_NONE;
      if (IsWindow(w)) DestroyWindow(w);
      if (got == 0) PostQuitMessage((int)msg.wParam);
      break;
    }
    if (IsWindow(w) && (msg.hwnd == w || IsChild(w, msg.hwnd)) && msg.message == WM_KEYDOWN) {
      int f = yg__start_focused_id(w);
      if ((msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE || msg.wParam == VK_SPACE) && (msg.lParam & (1L << 30)))
        continue;
      if (msg.wParam == VK_RETURN) {
        yg__start_pick(w, f == YG__START_ID_LATER || f == YG__START_ID_CLOSE ? YG__START_ID_LATER : YG__START_ID_MAIN);
        continue;
      }
      if (msg.wParam == VK_LEFT && win->later_label) { yg__start_focus(w, YG__START_ID_LATER); continue; }
      if (msg.wParam == VK_RIGHT) { yg__start_focus(w, YG__START_ID_MAIN); continue; }
    }
    if (IsWindow(w) && IsDialogMessageW(w, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (u->xinput) FreeLibrary(u->xinput);
  if (u->icon_big) DestroyIcon(u->icon_big);
  if (u->icon_small) DestroyIcon(u->icon_small);
  yg__start_ui_free(u);
  UnregisterClassW(cls, inst);
  return u->choice;
}

/* One window at a time: the window's loop also runs the game's own window messages, so a game that
   calls yg_start again from one of them (the player clicked its Online button meanwhile) gets no
   second window over the first; it hears that nothing was shown. */
static int yg__start_offer(const yg__start_win *win) {
  static int busy;
  int answer;
  if (busy) return YG__START_ANSWER_NONE;
  busy = 1;
  answer = yg__start_offer_window(win);
  busy = 0;
  return answer;
}

#elif defined(__APPLE__) || defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <crt_externs.h>
#include <pwd.h>
#include <sys/mount.h>
#include <time.h>
#define YG__START_ENVIRON (*_NSGetEnviron())
#else
#include <dirent.h>
extern char **environ;
#define YG__START_ENVIRON environ
#endif

/* A Mac has a menu bar; a Linux desktop need not have a tray. */
#ifdef __APPLE__
#define YG__START_TRAY_SYSTEM 1
#else
#define YG__START_TRAY_SYSTEM 0
#endif

static void yg__start_sleep(unsigned ms) { poll(NULL, 0, (int)ms); }

static int yg__start_quiet_env(void) {
  const char *v = getenv("YOUGAME_START_QUIET");
  return v && strcmp(v, "1") == 0;
}

/* A stale socket (ECONNREFUSED) is not running. EAGAIN means its listen queue is busy.
   Trust only a socket and a real 0700 parent folder owned by this user; never send any data.
   The connect is non-blocking and waits at most 100 ms. */
static int yg__start_socket(const char *path) {
  struct stat st;
  struct sockaddr_un addr;
  char folder[256];
  const char *slash = strrchr(path, '/');
  uid_t uid = getuid();
  int fd, rc, err, flags;
  size_t n;
  if (!slash || strlen(path) >= sizeof addr.sun_path) return 0;
  n = (size_t)(slash - path);
  if (!n || n >= sizeof folder) return 0;
  memcpy(folder, path, n);
  folder[n] = 0;
  if (lstat(folder, &st) || !S_ISDIR(st.st_mode) || st.st_uid != uid ||
      (st.st_mode & 0777) != 0700) return 0;
  if (lstat(path, &st) || !S_ISSOCK(st.st_mode) || st.st_uid != uid) return 0;
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return 0; }
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);
  rc = connect(fd, (const struct sockaddr *)&addr, sizeof addr);
  err = rc == 0 ? 0 : errno;
  if (err == EINPROGRESS) {
    struct pollfd pf;
    socklen_t len = sizeof err;
    pf.fd = fd;
    pf.events = POLLOUT;
    pf.revents = 0;
    if (poll(&pf, 1, 100) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len))
      err = EINPROGRESS;
  }
  close(fd);
  return err == 0 || err == EAGAIN || err == EWOULDBLOCK;
}

static int yg__start_running(int staging) {
  char path[256];
  const char *name = staging ? "yougame-staging" : "yougame";
  unsigned long uid = (unsigned long)getuid();
#ifdef __linux__
  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime && runtime[0] == '/') {
    size_t n = strlen(runtime);
    while (n > 1 && runtime[n - 1] == '/') --n;
    if (n < 200) {
      snprintf(path, sizeof path, "%.*s/%s/sdk-v1.sock", (int)n, runtime, name);
      if (yg__start_socket(path)) return 1;
    }
  }
  snprintf(path, sizeof path, "/run/user/%lu/%s/sdk-v1.sock", uid, name);
  if (yg__start_socket(path)) return 1;
#endif
  snprintf(path, sizeof path, "/tmp/%s-%lu/sdk-v1.sock", name, uid);
  return yg__start_socket(path);
}

#ifdef __linux__
/* Linux has no "close everything else" spawn flag everywhere (closefrom_np needs glibc 2.34), so
   each descriptor the game has open, as /proc lists it, is closed in the child. glibc and musl
   ignore a close of one that is already gone. Without /proc the spawn is refused. */
static int yg__start_close_others(posix_spawn_file_actions_t *fa) {
  DIR *d = opendir("/proc/self/fd");
  struct dirent *e;
  int *fds = NULL, ok = 1, own;
  size_t n = 0, cap = 0, i;
  if (!d) return 0;
  own = dirfd(d);
  while (ok && (e = readdir(d)) != NULL) {
    char *end;
    long v = strtol(e->d_name, &end, 10);
    if (end == e->d_name || *end || v <= 2 || v > INT_MAX || v == own) continue;
    if (n == cap) {
      size_t more = cap ? cap * 2 : 64;
      int *grown = (int *)realloc(fds, more * sizeof *fds);
      if (!grown) { ok = 0; break; }
      fds = grown;
      cap = more;
    }
    fds[n++] = (int)v;
  }
  closedir(d);
  for (i = 0; ok && i < n; ++i)
    if (posix_spawn_file_actions_addclose(fa, fds[i])) ok = 0;
  free(fds);
  return ok;
}
#endif

/* Absolute executables and argv arrays only (posix_spawn, never a shell or a PATH search), with
   stdin, stdout and stderr on /dev/null. The child inherits none of the game's other descriptors and
   starts with no blocked signals and default handling for the common ones. */
static int yg__start_spawn(const char *prog, char *const argv[], pid_t *pid) {
  posix_spawn_file_actions_t fa;
  posix_spawnattr_t attr;
  sigset_t none, dflt;
  int rc, attr_ok = 0;
  short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef __APPLE__
  char **filtered = yg__start_mac_environment(YG__START_ENVIRON);
  if (!filtered) return 0;
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT; /* only the descriptors the actions name reach the child */
#endif
  sigemptyset(&none);
  sigemptyset(&dflt);
  sigaddset(&dflt, SIGPIPE);
  sigaddset(&dflt, SIGCHLD);
  sigaddset(&dflt, SIGHUP);
  sigaddset(&dflt, SIGINT);
  sigaddset(&dflt, SIGQUIT);
  sigaddset(&dflt, SIGTERM);
  rc = posix_spawnattr_init(&attr);
  if (!rc) {
    attr_ok = 1;
    rc = posix_spawnattr_setflags(&attr, flags);
    if (!rc) rc = posix_spawnattr_setsigmask(&attr, &none);
    if (!rc) rc = posix_spawnattr_setsigdefault(&attr, &dflt);
  }
  if (!rc) rc = posix_spawn_file_actions_init(&fa);
  if (!rc) {
    rc = posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (!rc) rc = posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    if (!rc) rc = posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
#ifdef __linux__
    if (!rc && !yg__start_close_others(&fa)) rc = EMFILE;
#endif
    if (!rc) rc = posix_spawn(pid, prog, &fa, &attr, argv,
#ifdef __APPLE__
                             filtered
#else
                             YG__START_ENVIRON
#endif
                             );
    posix_spawn_file_actions_destroy(&fa);
  }
  if (attr_ok) posix_spawnattr_destroy(&attr);
#ifdef __APPLE__
  free(filtered);
#endif
  return rc == 0;
}

#ifdef __APPLE__
static int yg__start_mac_poll_child(void *child) {
  pid_t pid = *(pid_t *)child, rc;
  int status;
  rc = waitpid(pid, &status, WNOHANG);
  if (!rc || (rc < 0 && errno == EINTR)) return 0;
  return rc == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 1 : -1;
}

static void yg__start_mac_stop_child(void *child) {
  pid_t pid = *(pid_t *)child;
  int status;
  (void)kill(pid, SIGKILL);
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}

static int yg__start_wait(pid_t pid, unsigned deadline) {
  return yg__start_mac_wait(deadline, yg__start_mac_poll_child, yg__start_mac_stop_child, &pid);
}
#else
/* The child's exit code, or -1 when it did not exit normally. */
static int yg__start_wait_code(pid_t pid) {
  int status;
  pid_t rc;
  do { rc = waitpid(pid, &status, 0); } while (rc < 0 && errno == EINTR);
  return rc == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int yg__start_wait(pid_t pid) { return yg__start_wait_code(pid) == 0; }
#endif

#ifdef __linux__
/* xdg-open may live until a browser closes; reap it without holding up the game. */
static void *yg__start_reap(void *arg) {
  pid_t pid = *(pid_t *)arg;
  free(arg);
  (void)yg__start_wait(pid);
  return NULL;
}
#endif

/* The download page, through the system's opener for https (never YouGame's own link type). */
static int yg__start_shell_open(const char *url) {
#ifdef __APPLE__
  const char *prog = "/usr/bin/open";
#else
  const char *prog = access("/usr/bin/xdg-open", X_OK) == 0 ? "/usr/bin/xdg-open" : "/bin/xdg-open";
#endif
  char *argv[3];
  pid_t pid;
  argv[0] = (char *)prog;
  argv[1] = (char *)url;
  argv[2] = NULL;
  if (!yg__start_spawn(prog, argv, &pid)) return 0;
#ifdef __APPLE__
  return yg__start_wait(pid, YG__START_OPEN_MS) == 1;
#else
  {
    pthread_t thread;
    pid_t *job = (pid_t *)malloc(sizeof *job);
    if (job) {
      *job = pid;
      if (pthread_create(&thread, NULL, yg__start_reap, job) == 0) {
        pthread_detach(thread);
        return 1; /* dispatched, as with ShellExecute */
      }
      free(job);
    }
    return yg__start_wait(pid);
  }
#endif
}

#ifdef __linux__
/* Whether one of the desktop's MIME association files names a .desktop handler for `key`
   ("x-scheme-handler/yougame"), in a section that adds one (never "[Removed Associations]"). */
static int yg__start_list_names(const char *file, const char *key) {
  char line[1024];
  size_t k = strlen(key);
  int fd = open(file, O_RDONLY | O_CLOEXEC), take = 0, found = 0;
  FILE *f;
  if (fd < 0) return 0;
  f = fdopen(fd, "r");
  if (!f) { close(fd); return 0; }
  while (!found && fgets(line, sizeof line, f)) {
    size_t n = strlen(line);
    if (n && line[n - 1] != '\n' && !feof(f)) { /* too long to be ours: skip the rest of it */
      int c;
      while ((c = fgetc(f)) != EOF && c != '\n') {}
      continue;
    }
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    if (line[0] == '[') {
      take = !strcmp(line, "[Default Applications]") || !strcmp(line, "[Added Associations]") ||
             !strcmp(line, "[MIME Cache]");
    } else if (take && !strncmp(line, key, k) && line[k] == '=') {
      const char *v = line + k + 1, *semi = strchr(v, ';');
      size_t len = semi ? (size_t)(semi - v) : strlen(v);
      found = len > 8 && !memcmp(v + len - 8, ".desktop", 8) && !memchr(v, '/', len);
    }
  }
  fclose(f);
  return found;
}

/* An absolute XDG folder from the environment, else $HOME + fallback. Empty when neither works. */
static void yg__start_xdg(const char *var, const char *fallback, char *out, size_t cap) {
  const char *v = getenv(var), *home = getenv("HOME");
  out[0] = 0;
  if (v && v[0] == '/' && strlen(v) < cap) strcpy(out, v);
  else if (home && home[0] == '/' && (size_t)snprintf(out, cap, "%s/%s", home, fallback) >= cap) out[0] = 0;
}
#endif

#ifdef __APPLE__
/* LaunchServices finds a bundle by flavour, never by a game-supplied path or URL. */
typedef struct yg__start_app {
  CFURLRef url;
  CFBundleRef bundle;
  char path[PATH_MAX];
  int staging;
} yg__start_app;

static CFStringRef yg__start_bundle_id(int staging) {
  return staging ? CFSTR("co.yougame.desktop.staging") : CFSTR("co.yougame.desktop");
}

/* 0 for a non-link, 1 for an allowed link's target, -1 for any refusal or read failure. */
static int yg__start_mac_link_target(const char *path, char *target, size_t cap) {
  struct stat st;
  ssize_t len;
  if (lstat(path, &st)) return -1;
  if (!S_ISLNK(st.st_mode)) return 0;
  if (!yg__start_mac_owner_ok((unsigned long)st.st_uid, (unsigned long)getuid())) return -1;
  len = readlink(path, target, cap - 1);
  if (len < 0 || (size_t)len >= cap - 1) return -1;
  target[len] = 0;
  return 1;
}

static int yg__start_mac_resolve(const char *path, const char *home, char resolved[PATH_MAX]) {
  struct stat st;
  struct statfs volume;
  char parent[PATH_MAX], *slash;
  /* Resolve once. All metadata reads, codesign and open then use this exact resolved string.
     Full running-image binding cannot happen before execution with open/LaunchServices. An
     attacker who can write the bundle can already replace the app the user starts by hand;
     Windows has the same model (its hook additionally checks a suspended image). */
  if (!yg__start_mac_candidate(path, home) || !yg__start_mac_links_ok(path, yg__start_mac_link_target) ||
      !realpath(path, resolved) || !yg__start_mac_candidate(resolved, home) ||
      lstat(resolved, &st) ||
      !yg__start_mac_bundle_ok((unsigned long)st.st_uid, (unsigned long)getuid(),
                               (unsigned)st.st_mode, S_ISDIR(st.st_mode))) return 0;
  /* desktop's installation.installed rejects read-only volumes and unwritable parents too. */
  if (statfs(resolved, &volume) || (volume.f_flags & MNT_RDONLY)) return 0;
  strcpy(parent, resolved);
  slash = strrchr(parent, '/');
  if (!slash) return 0;
  *slash = 0;
  return access(parent, W_OK) == 0;
}

static int yg__start_app_find(int staging, yg__start_app *app) {
  CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier(yg__start_bundle_id(staging), NULL);
  struct passwd *user = getpwuid(getuid());
  const char *home = user ? user->pw_dir : NULL;
  int best = 0;
  memset(app, 0, sizeof *app);
  app->staging = staging;
  if (urls) {
    CFIndex i;
    for (i = 0; i < CFArrayGetCount(urls); ++i) {
      char path[PATH_MAX], resolved[PATH_MAX];
      CFURLRef url = (CFURLRef)CFArrayGetValueAtIndex(urls, i), resolved_url;
      CFBundleRef bundle;
      int rank;
      if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof path) ||
          !yg__start_mac_resolve(path, home, resolved)) continue;
      rank = yg__start_mac_prefer(resolved, home, best);
      if (!rank) continue;
      resolved_url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)resolved,
                                                            (CFIndex)strlen(resolved), true);
      if (!resolved_url) continue;
      bundle = CFBundleCreate(NULL, resolved_url);
      if (!bundle) { CFRelease(resolved_url); continue; }
      if (app->bundle) CFRelease(app->bundle);
      if (app->url) CFRelease(app->url);
      app->url = resolved_url;
      app->bundle = bundle;
      strcpy(app->path, resolved);
      best = rank;
    }
    CFRelease(urls);
  }
  if (app->bundle) return 1;
  if (app->url) CFRelease(app->url);
  app->url = NULL;
  return 0;
}

static void yg__start_app_close(yg__start_app *app) {
  CFRelease(app->bundle);
  CFRelease(app->url);
}

static int yg__start_app_named(yg__start_app *app, int staging) {
  CFStringRef ident = CFBundleGetIdentifier(app->bundle);
  CFTypeRef kind = CFBundleGetValueForInfoDictionaryKey(app->bundle, CFSTR("CFBundlePackageType"));
  return ident && CFEqual(ident, yg__start_bundle_id(staging)) && kind && CFEqual(kind, CFSTR("APPL"));
}

static int yg__start_app_version(yg__start_app *app, unsigned have[3]) {
  char text[32];
  CFTypeRef v = CFBundleGetValueForInfoDictionaryKey(app->bundle, CFSTR("CFBundleShortVersionString"));
  return v && CFGetTypeID(v) == CFStringGetTypeID() &&
    CFStringGetCString((CFStringRef)v, text, sizeof text, kCFStringEncodingASCII) &&
    yg__start_version_parse(text, have);
}

/* Offline code-signature verification, including sealed resources (nested code is sealed by them; no
   --deep, which re-verifies every helper and doubles the time a game waits). Pin the same
   Developer ID team as desktop/src/orca-policy.ts, and this flavour's signed bundle identifier.
   No Gatekeeper assessment, shell, output, or certificate download. */
static int yg__start_app_signed(yg__start_app *app) {
  char requirement[512];
  char *argv[8];
  pid_t pid;
  if (!yg__start_mac_requirement(app->staging, requirement, sizeof requirement)) return 0;
  yg__start_mac_verify_argv(app->path, requirement, argv);
  if (!yg__start_spawn(argv[0], argv, &pid)) return 0;
  return yg__start_wait(pid, YG__START_CODESIGN_MS);
}

/* -g leaves the game in front, -j hides the app, --args reaches Electron's argv. Use the
   verified bundle path (-a), rather than resolving its identifier again after verification. */
static int yg__start_app_launch(yg__start_app *app, int staging) {
  char *argv[8];
  pid_t pid;
  yg__start_mac_open_argv(app->path, argv);
  if (!yg__start_spawn(argv[0], argv, &pid)) return 0;
  return yg__start_mac_opened(yg__start_wait(pid, YG__START_OPEN_MS), staging);
}

static unsigned long yg__start_ms(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
  return ((unsigned long)now.tv_sec * 1000UL + (unsigned long)now.tv_nsec / 1000000UL) & 0xFFFFFFFFUL;
}
#endif

/* Installed = a Mac bundle of this flavour, or an app registered for YouGame's link type. Only looked up: nothing is opened or
   started (Linux reads the association files itself rather than running xdg-mime). */
static int yg__start_installed(int staging) {
#ifdef __APPLE__
  yg__start_app app;
  if (!yg__start_app_find(staging, &app)) return 0;
  yg__start_app_close(&app);
  return 1;
#else
  /* The files xdg-mime and the desktops read for a user's handler (the app's own xdg-settings call
     writes the first), then the caches a packaged install's desktop entry lands in. */
  const char *key = staging ? "x-scheme-handler/yougame-staging" : "x-scheme-handler/yougame";
  char config[256], data[256], path[320];
  yg__start_xdg("XDG_CONFIG_HOME", ".config", config, sizeof config);
  yg__start_xdg("XDG_DATA_HOME", ".local/share", data, sizeof data);
  if (config[0]) {
    snprintf(path, sizeof path, "%s/mimeapps.list", config);
    if (yg__start_list_names(path, key)) return 1;
  }
  if (data[0]) {
    snprintf(path, sizeof path, "%s/applications/mimeapps.list", data);
    if (yg__start_list_names(path, key)) return 1;
    snprintf(path, sizeof path, "%s/applications/mimeinfo.cache", data);
    if (yg__start_list_names(path, key)) return 1;
  }
  return yg__start_list_names("/usr/local/share/applications/mimeinfo.cache", key) ||
         yg__start_list_names("/usr/share/applications/mimeinfo.cache", key);
#endif
}

/* The at-start game's player pressed Play offline once (see yg_start): a file named for the game in
   the user's own folder (macOS: YouGame Start under Application Support in the home Library folder;
   Linux: yougame-start under $XDG_STATE_HOME, by default the home folder's local state folder). note = 0 asks whether it is there;
   note = 1 makes it. Any failure: not there (the window shows at start, as before). */
static int yg__start_offline(int staging, const char *game, int note) {
  char dir[PATH_MAX], path[PATH_MAX];
  struct stat st;
  int fd;
#ifdef __APPLE__
  const char *home = getenv("HOME");
  if (!home || home[0] != '/' ||
      (size_t)snprintf(dir, sizeof dir, "%s/Library/Application Support/YouGame Start", home) >= sizeof dir)
    return 0;
#else
  char state[PATH_MAX];
  yg__start_xdg("XDG_STATE_HOME", ".local/state", state, sizeof state);
  if (!state[0] || (size_t)snprintf(dir, sizeof dir, "%s/yougame-start", state) >= sizeof dir) return 0;
#endif
  if ((size_t)snprintf(path, sizeof path, "%s/%s%s.offline", dir, game, staging ? ".staging" : "") >= sizeof path)
    return 0;
  if (!note) return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
#ifndef __APPLE__
  mkdir(state, 0700); /* the XDG state folder may not exist yet; its parent almost always does */
#endif
  if (mkdir(dir, 0700) != 0 && errno != EEXIST) return 0;
  fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) return 0;
  close(fd);
  return 1;
}

/* The system's own box (macOS alert; Linux zenity or kdialog) with the window's words. YouGame's
   own drawn window is Windows only for now (Windows first). 1: the main button, 0: the
   other, -1: nothing was shown or no answer came. */
static int yg__start_show(const yg__start_win *w) {
#ifdef __APPLE__
  CFStringRef title = CFStringCreateWithCString(NULL, w->title, kCFStringEncodingASCII);
  CFStringRef body = CFStringCreateWithCString(NULL, w->body, kCFStringEncodingASCII);
  CFStringRef main_label = CFStringCreateWithCString(NULL, w->main_label, kCFStringEncodingASCII);
  CFStringRef later_label = w->later_label ? CFStringCreateWithCString(NULL, w->later_label, kCFStringEncodingASCII) : NULL;
  CFOptionFlags answer = kCFUserNotificationCancelResponse;
  SInt32 rc = -1;
  if (title && body && main_label && (later_label || !w->later_label))
    rc = CFUserNotificationDisplayAlert(0, kCFUserNotificationNoteAlertLevel, NULL, NULL, NULL,
      title, body, main_label, later_label, NULL, &answer);
  if (title) CFRelease(title);
  if (body) CFRelease(body);
  if (main_label) CFRelease(main_label);
  if (later_label) CFRelease(later_label);
  if (rc != 0) return -1;
  if ((answer & 3) == kCFUserNotificationDefaultResponse) return 1;
  return (answer & 3) == kCFUserNotificationAlternateResponse ? 0 : -1;
#else
  char *argv[10];
  char message[YG__START_TITLE_CAP + YG__START_BODY_CAP + 4];
  char ok[64], cancel[64];
  pid_t pid;
  const char *prog;
  int code;
  if ((size_t)snprintf(message, sizeof message, "%s\n\n%s", w->title, w->body) >= sizeof message) return -1;
  if (access("/usr/bin/zenity", X_OK) == 0) {
    prog = "/usr/bin/zenity";
    snprintf(ok, sizeof ok, "--ok-label=%s", w->main_label);
    snprintf(cancel, sizeof cancel, "--cancel-label=%s", w->later_label ? w->later_label : w->main_label);
    argv[0] = (char *)prog;
    argv[1] = (char *)"--question";
    argv[2] = (char *)"--title=YouGame";
    argv[3] = ok;
    argv[4] = cancel;
    argv[5] = (char *)"--no-markup"; /* a name may hold & */
    argv[6] = (char *)"--text";
    argv[7] = message;
    argv[8] = NULL;
  } else if (access("/usr/bin/kdialog", X_OK) == 0) {
    prog = "/usr/bin/kdialog";
    argv[0] = (char *)prog;
    argv[1] = (char *)"--title";
    argv[2] = (char *)"YouGame";
    argv[3] = (char *)"--yes-label";
    argv[4] = (char *)w->main_label;
    argv[5] = (char *)"--no-label";
    argv[6] = (char *)(w->later_label ? w->later_label : w->main_label);
    argv[7] = (char *)"--yesno";
    argv[8] = message;
    argv[9] = NULL;
  } else return -1;
  if (!yg__start_spawn(prog, argv, &pid)) return -1;
  code = yg__start_wait_code(pid);
  /* 0 the main button, 1 the other (or Esc); anything else is an error (no display, a crash). */
  return code == 0 ? 1 : code == 1 ? 0 : -1;
#endif
}

static int yg__start_offer(const yg__start_win *w) {
  /* One button (nothing to choose): whatever the player presses is that button's answer. */
  int pressed = yg__start_show(w);
  return pressed > 0 ? w->answer : pressed == 0 ? YG__START_ANSWER_LATER : YG__START_ANSWER_NONE;
}
#endif /* platform implementation */

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__) || defined(YOUGAME_START_TESTING)
/* Which game sent the player: game is a checked plain slug, copied by the background worker. */
static int yg__start_download_page(int staging, const char *game) {
  char url[160];
  int n = snprintf(url, sizeof url, "%s/desktop?from=game&game=%s",
                   staging ? "https://staging.yougame.co" : "https://yougame.co", game);
  return n > 0 && (size_t)n < sizeof url && yg__start_shell_open(url);
}
#endif

int yg_start_get_progress(int *percent) {
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  long stage;
  if (!YG__START_UPDATE_SYSTEM) return YG_START_GET_NONE;
  stage = YG__START_GET_READ(&yg__start_g.stage);
  switch (stage) {
    case YG__START_GET_DOWNLOAD:
      if (percent) *percent = (int)YG__START_GET_READ(&yg__start_g.pct);
      return YG_START_GET_DOWNLOADING;
    case YG__START_GET_INSTALLING: return YG_START_GET_INSTALLING;
    case YG__START_GET_DONE: return YG_START_GET_READY;
    case YG__START_GET_FAILED: return YG_START_GET_FAILED;
    default: return YG_START_GET_NONE;
  }
#else
  (void)percent;
  return YG_START_GET_NONE;
#endif
}

#if defined(_WIN32) || defined(__APPLE__) || defined(YOUGAME_START_TESTING)
/* Windows and macOS: find the app, check its identity, version and signature, then launch it
   quietly. Windows additionally holds and binds the executable throughout these checks. */
static int yg__start_tray(int staging, const unsigned want[3], unsigned have[3]) {
  yg__start_app app;
  int r, signature;
  if (!yg__start_app_find(staging, &app)) return YG__START_TRAY_REFUSED;
  if (!yg__start_app_named(&app, staging))
    r = YG__START_TRAY_REFUSED;
  else if (!yg__start_app_version(&app, have) || !yg__start_version_at_least(have, want))
    r = YG__START_TRAY_OLD;
  else if ((signature = yg__start_app_signed(&app)) <= 0)
    r = signature < 0 ? YG__START_TRAY_FAILED : YG__START_TRAY_REFUSED;
  else
    r = yg__start_app_launch(&app, staging) ? YG__START_TRAY_STARTED : YG__START_TRAY_FAILED;
  yg__start_app_close(&app);
  return r;
}

#endif

#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
static void yg__start_get_note(int stage) {
  YG__START_GET_SET(&yg__start_g.stage, stage);
}

/* The worker completes the install without the caller's options or any window state. READY means
   the checked tray start succeeded, or the app was already answering. */
static void yg__start_get_finish(int installed) {
  unsigned any[3] = {0, 0, 0}, have[3] = {0, 0, 0};
  int staging = yg__start_g.staging, ready = 0;
  if (installed) {
    ready = yg__start_running(staging);
    if (!ready) {
      if (yg__start_tray(staging, any, have) == YG__START_TRAY_STARTED) ready = 1;
    }
  }
  if (!ready) (void)yg__start_download_page(staging, yg__start_g.game);
  yg__start_get_note(ready ? YG__START_GET_DONE : YG__START_GET_FAILED);
  YG__START_GET_SET(&yg__start_g.running, 0);
}

/* Only a first install, never raised. Publish the owned inputs before starting the worker. */
static int yg__start_get_begin(int staging, const char *game) {
  if (YG__START_GET_READ(&yg__start_g.running)) return 1;
  if (yg__start_raised() || yg__start_installed(staging) || yg__start_running(staging)) return 0;
  yg__start_g.staging = staging;
  memcpy(yg__start_g.game, game, strlen(game) + 1);
  YG__START_GET_SET(&yg__start_g.pct, 0);
  yg__start_get_note(YG__START_GET_DOWNLOAD);
  YG__START_GET_SET(&yg__start_g.running, 1);
  if (yg__start_get_launch()) return 1;
  yg__start_get_note(0);
  YG__START_GET_SET(&yg__start_g.running, 0);
  return 0;
}
#endif

#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
/* The marker's `to` is already installed: the registered program's file version
   (yg__start_installed_version, which never blocks an installer) reaches `to`
   (yg__start_version_reached). A program that cannot be read (an installer may be replacing it
   right now) is not done. */
static int yg__start_update_done(int staging, const char *to) {
  unsigned have[3];
  return yg__start_installed_version(staging, have) && yg__start_version_reached(have, to);
}

/* YG_START_UPDATING, with the marker's version copied to the game's update_to (cut to fit). */
static yg_start_result yg__start_updating(const yg_start_options *o, const char *to) {
  if (o->update_to && o->update_to_cap) {
    size_t n = strlen(to);
    if (n >= o->update_to_cap) n = o->update_to_cap - 1;
    memcpy(o->update_to, to, n);
    o->update_to[n] = 0;
  }
  return YG_START_UPDATING;
}

#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(YOUGAME_START_TESTING)
/* After a tray start: wait up to open_wait_ms (at most 60 s) for YouGame to be running. Polled every
   500 ms: the channel first, then (Windows only) the update marker. The wait is bounded by the clock as
   well as by the sleeps it adds up, so slow checks never stretch it. YG_START_OPENED, YG_START_UPDATING,
   or -1 when it is not up in time. */
#define YG__START_WAIT_MARK &mark, to
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
static int yg__start_wait_up(const yg_start_options *o, int staging, const unsigned have[3],
                             yg__start_mark *mark, char to[YG__START_TO_CAP]) {
#else
#undef YG__START_WAIT_MARK
#define YG__START_WAIT_MARK 0
static int yg__start_wait_up(const yg_start_options *o, int staging, const unsigned have[3], int unused) {
#endif
  unsigned wait = o->open_wait_ms > 60000 ? 60000 : o->open_wait_ms, waited = 0;
  unsigned long start, elapsed;
#if !defined(_WIN32) && !defined(YOUGAME_START_TESTING)
  (void)have;
  (void)unused;
#endif
  if (wait == 0) return YG_START_OPENED;
  start = yg__start_ms();
  while (waited < wait) {
    unsigned step = wait - waited < 500 ? wait - waited : 500;
    yg__start_sleep(step);
    waited += step;
    elapsed = (yg__start_ms() - start) & 0xFFFFFFFFUL;
    if (elapsed > waited) waited = elapsed < wait ? (unsigned)elapsed : wait;
    if (yg__start_running(staging)) return YG_START_OPENED;
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
    /* An older YouGame that finds an update quits to install it: say so, not "didn't open".
       The version it was started at (have) tells a leftover marker from a new one. */
    if (YG__START_UPDATE_SYSTEM && yg__start_marker(staging, mark, to) &&
        (mark->age_ms < 20000 || !yg__start_version_reached(have, to)))
      return yg__start_updating(o, to);
#endif
  }
  return -1;
}
#endif

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__) || defined(YOUGAME_START_TESTING)
yg_start_result yg_start(const yg_start_options *o) {
  int staging, quiet, installed, text, answer, ask, at_start, open_ok;
  yg__start_win win;
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  char to[YG__START_TO_CAP];
  yg__start_mark mark;
#endif
  /* Cleared at entry only for a caller that set the magic: a struct that was never zeroed never
     has its update_to written before a real YG_START_UPDATING. */
  if (o && o->update_to_magic == YG_START_UPDATE_TO_MAGIC && o->update_to && o->update_to_cap) o->update_to[0] = 0;
  if (!o || !yg__start_slug_ok(o->game)) return YG_START_UNAVAILABLE;
  staging = o->staging ? 1 : 0;
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  if (YG__START_UPDATE_SYSTEM && YG__START_GET_READ(&yg__start_g.running)) return YG_START_GETTING;
#endif
  quiet = o->quiet || yg__start_quiet_env();
  ask = o->ask; /* any value but LATER and ON_USE acts as at start */
  if (yg__start_running(staging)) return YG_START_RUNNING;
  installed = yg__start_installed(staging);
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  /* YouGame quit to install an update: a fact, so quiet runs report it too, and nothing is started
     or shown. Taken whether or not the link type is registered right now, since an installer may
     drop and rewrite it while it runs, unless the installed YouGame already has the marker's version
     and the marker file is at least 20 s old (a finished update, including installer post-steps). Windows only (YG__START_UPDATE_SYSTEM): macOS and
     Linux have no marker, so it is not even looked for there. */
  mark.looked = 0;
  if (YG__START_UPDATE_SYSTEM && yg__start_marker(staging, &mark, to) &&
      (mark.age_ms < 20000 || !yg__start_update_done(staging, to)))
    return yg__start_updating(o, to);
#endif
  if (quiet) return installed ? YG_START_INSTALLED : YG_START_MISSING;
  text = installed ? YG__START_TEXT_NOT_RUNNING : YG__START_TEXT_MISSING;
#if defined(_WIN32) || defined(__APPLE__) || defined(YOUGAME_START_TESTING)
  if (installed && YG__START_TRAY_SYSTEM && !o->no_open) {
    unsigned want[3], have[3] = {0, 0, 0};
    if (yg__start_version_parse(o->tray_from, want)) {
      int tray = yg__start_tray(staging, want, have);
      if (tray == YG__START_TRAY_OLD) text = YG__START_TEXT_UPDATE;
      else if (tray == YG__START_TRAY_FAILED) text = YG__START_TEXT_DIDNT_OPEN;
      else if (tray == YG__START_TRAY_STARTED) {
        int up = yg__start_wait_up(o, staging, have, YG__START_WAIT_MARK);
        if (up >= 0) return (yg_start_result)up;
        text = YG__START_TEXT_DIDNT_OPEN;
      }
    }
  }
#else
  /* Linux: no tray start, so these go unused there. */
  (void)yg__start_version_parse;
  (void)yg__start_version_at_least;
  (void)yg__start_sleep;
#endif
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  /* Run as administrator: no tray start was tried, and Open could not work either. */
  if (installed && YG__START_UPDATE_SYSTEM && yg__start_raised()) text = YG__START_TEXT_RAISED;
#endif
  /* An at-start game whose player already pressed Play offline once: from then on it starts like a
     game that asks on first use ("once, then at the button"). Not for a game that needs
     YouGame to run: that one asks at every start. */
  at_start = ask != YG_START_ASK_LATER && ask != YG_START_ASK_ON_USE;
  if (at_start && !o->needs && yg__start_offline(staging, o->game, 0)) ask = YG_START_ASK_LATER;
  /* Asked later (the game asks on first use): whatever the window would have said waits for the
     player's press; tell the game what was found. */
  if (ask == YG_START_ASK_LATER) return installed ? YG_START_INSTALLED : YG_START_MISSING;
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  open_ok = YG__START_UPDATE_SYSTEM; /* Open YouGame: Windows starts it from the window */
#else
  open_ok = 0;
#endif
  if (!yg__start_compose(text, ask == YG_START_ASK_ON_USE, o->name, o->needs, open_ok, &win))
    return YG_START_UNAVAILABLE;
  win.staging = staging;
  win.game = o->game;
  answer = yg__start_offer(&win);
  /* Remembered only from a real choice: not from the administrator window, which has no other button. */
  if (at_start && !o->needs && answer == YG__START_ANSWER_LATER && text != YG__START_TEXT_RAISED)
    yg__start_offline(staging, o->game, 1);
#if defined(_WIN32) || defined(YOUGAME_START_TESTING)
  /* Open YouGame: the same checked tray start as above, at any version, now the player asked (also
     with no_open or no tray_from: pressing Open is the player's own choice). The window may have been
     up a while: a YouGame that came up meanwhile is taken as is, and one that quit to install an
     update says so, before anything is started. Then open_wait_ms applies as on the first try. */
  if (answer == YG__START_ANSWER_OPEN && YG__START_UPDATE_SYSTEM && YG__START_OFFERS_OPEN(text)) {
    unsigned any[3] = {0, 0, 0}, have[3] = {0, 0, 0};
    if (yg__start_running(staging)) return YG_START_OPENED;
    if (yg__start_marker(staging, &mark, to) && (mark.age_ms < 20000 || !yg__start_update_done(staging, to)))
      return yg__start_updating(o, to);
    if (yg__start_tray(staging, any, have) == YG__START_TRAY_STARTED) {
      int up = yg__start_wait_up(o, staging, have, YG__START_WAIT_MARK);
      /* Started but slow to come up: never send the player to download an app that is starting. */
      return up >= 0 ? (yg_start_result)up : YG_START_DECLINED;
    }
    answer = YG__START_ANSWER_GET;   /* it would not start: the download page puts it right */
  }
  if (answer == YG__START_ANSWER_GETTING) return YG_START_GETTING;
#endif
  if (answer != YG__START_ANSWER_GET) return YG_START_DECLINED;
  if (!yg__start_download_page(staging, o->game)) return YG_START_UNAVAILABLE;
  return YG_START_OFFERED;
}
#else

yg_start_result yg_start(const yg_start_options *options) {
  if (options && options->update_to_magic == YG_START_UPDATE_TO_MAGIC && options->update_to && options->update_to_cap)
    options->update_to[0] = 0;
  (void)yg__start_slug_ok;
  (void)yg__start_version_parse;
  (void)yg__start_version_at_least;
  (void)yg__start_compose;
  return YG_START_UNAVAILABLE;
}
#endif

#endif /* YOUGAME_START_IMPLEMENTATION */

#endif /* YOUGAME_START_H */
