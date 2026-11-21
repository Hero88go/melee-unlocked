/* yougame.h: the YouGame plugin for any native game, in one C header (v0.1.0).
 *
 * Scores, the leaderboard, saves, the player's name, YouGame's feedback sheet and its play menu for
 * a game written in C or C++ with any engine or none. It works when the YouGame desktop app starts
 * the game (lines on stdin and stdout) and when the player starts it on their own: then it looks for
 * the open YouGame app's local channel, and with no app the feedback call opens the game's YouGame
 * page in the player's browser. It speaks https://yougame.co/sdk.md "Native builds" and YouGame's
 * local-channel contract v1. An engine wrapper (Unreal's is planned) is meant to be thin glue over it.
 *
 * C99 or C++, the C runtime and the OS only (Win32 on Windows; POSIX threads elsewhere). In exactly
 * one source file:
 *
 *     #define YOUGAME_IMPLEMENTATION
 *     #include "yougame.h"
 *
 * Windows links advapi32, shell32 and ole32 (MSVC picks them up by itself; MinGW: -ladvapi32
 * -lshell32 -lole32) and needs Windows Vista or later. Mac and Linux link pthreads (-pthread).
 *
 * Threads: call everything from one thread (the game's), and call yg_update() once a frame there.
 * One background thread reads and writes; every callback, the log included, runs only inside
 * yg_update(), never inside the call that started it. No call waits on the app.
 *
 * In a game the app starts, stdout belongs to YouGame: end every line the game prints with "\n",
 * or the plugin's next line joins it.
 *
 * MIT licence: see the end of this file and LICENSE beside it.
 */
#if defined(YOUGAME_IMPLEMENTATION) && !defined(YOUGAME_IMPLEMENTED) && !defined(_WIN32) && \
    !defined(__APPLE__) && !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE)
/* glibc under -std=c99 hides pipe, poll, clock_gettime and posix_spawn without this. It only works
 * when this header comes before every system header in the implementation file. */
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef YOUGAME_H
#define YOUGAME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YG_VERSION "0.1.0"

/* How the game was started. */
typedef enum yg_started {
  YG_STARTED_ALONE = 0, /* by the player: from a folder, a shortcut, another launcher, a debugger */
  YG_STARTED_BY_APP = 1 /* by the YouGame app (YOUGAME_STDIO or YOUGAME_BRIDGE is set) */
} yg_started;

/* Which channel this run uses. Picked once, in yg_init. */
typedef enum yg_transport {
  YG_TRANSPORT_NONE = 0,  /* nothing to talk to: every call answers code "notAvailable" */
  YG_TRANSPORT_STDIO = 1, /* started by the app with YOUGAME_STDIO=1 (every Windows build) */
  YG_TRANSPORT_LOCAL = 2  /* started alone with a valid game slug: the app's local channel */
} yg_transport;

/* yg_open_feedback's kind. */
enum { YG_FEEDBACK_ASK = 0, YG_FEEDBACK_BUG = 1, YG_FEEDBACK_IDEA = 2 };

/* yg_leaderboard's period. */
enum { YG_PERIOD_ALL = 0, YG_PERIOD_DAY = 1, YG_PERIOD_WEEK = 2 };

/* Why the feedback sheet did not open (yg_result.feedback.reason). */
enum {
  YG_REASON_NONE = 0,          /* it opened (in the app, or in_browser) */
  YG_REASON_ALREADY_OPEN = 1,  /* a sheet is already open: do nothing */
  YG_REASON_TOO_SOON = 2,      /* one a minute: show player_message ("Feedback opens again in N s") */
  YG_REASON_NO_ACTIVATION = 3, /* only from the player's own press */
  YG_REASON_NOT_HERE = 4,      /* show player_message ("Open YouGame to send feedback") */
  YG_REASON_UNSUPPORTED = 5,   /* an older YouGame with no sheet for games: hide the button */
  YG_REASON_NO_ANSWER = 6,     /* nothing answered in time: the player can press again */
  YG_REASON_UNKNOWN = 7,       /* not opened, no reason given */
  YG_REASON_NOT_AVAILABLE = 8  /* no YouGame answered and no feedback page could open */
};

/* yg_set_game_state's state: what the game is doing now, for YouGame's button over it. */
typedef enum yg_game_state {
  YG_GAME_PLAYING = 1, /* in play: the button keeps out of the way */
  YG_GAME_PAUSED = 2,  /* the game's own pause: the button shows (controllers, settings, friends) */
  YG_GAME_MENU = 3     /* a main menu or another menu outside play: the button shows */
} yg_game_state;

/* yg_config.log's level. */
enum { YG_LOG_INFO = 0, YG_LOG_WARN = 1 };

/* Who the player is, as far as YouGame has said (yg_player_status). */
typedef enum yg_player_state {
  YG_PLAYER_UNKNOWN = 0, /* hello not answered yet, or no YouGame answers: nothing to show yet */
  YG_PLAYER_NONE = 1,    /* YouGame answered with no player: show "Player" */
  YG_PLAYER_PENDING = 2, /* not yet: someone is signed in to YouGame and it is still finding out
                            who. config.on_player fires when they arrive; the game never asks again */
  YG_PLAYER_KNOWN = 3    /* yg_player_name() and yg_player_id() are filled in */
} yg_player_state;

/* One leaderboard row. Strings are never NULL ("" when absent). */
typedef struct yg_entry {
  const char *score_id;
  int rank;
  const char *name;
  long long score;
  const char *player_id; /* this game's own id for the player */
  const char *at;
  int unverified;
} yg_entry;

/* What a call answers. Valid only inside the callback: copy what you keep. Strings are never NULL
 * ("" when absent). Only the group for the call's type is filled in; the rest stays zero/"". */
typedef struct yg_result {
  long long id;            /* the call's id, as the call returned it */
  const char *type;        /* the wire type: "submitScore", "saveLoad", ... */
  int ok;                  /* 1: YouGame answered ok. 0: refused or failed (see code, error) */
  /* notAvailable, noAnswer, notYet, tooLong, busy (64 MB of lines already wait to go out: the app
   * is not reading; try later), or YouGame's own code; may be "" */
  const char *code;
  const char *error;       /* "" when ok */
  /* When not ok: YouGame's "retryAfter", the seconds to wait before asking again (busy, or an
   * account switch under way); 0 when it gave none. */
  int retry_after_seconds;
  const char *result_json; /* the whole `result` as JSON when ok (read it with yg_json_*), else "" */

  struct { /* requestName, setName */
    const char *id;
    const char *name; /* "Player" from requestName while the player keeps their name from games */
    int shared;       /* the player shows their name in games */
  } player;

  struct { /* submitScore */
    const char *score_id;
    int rank; /* the run's place; 0 when none */
    int total;
    long long best;
    long long previous_best; /* -1 for the player's first score */
    int is_personal_best;
    const char *share_url;
    const char *status; /* "ranked", "suspect" or "unranked" */
  } score;

  struct { /* leaderboard */
    const yg_entry *entries;
    int count;
    int total;
    int has_me; /* me is the player's own best row */
    yg_entry me;
  } board;

  struct { /* saveWrite, saveClear, saveLoad */
    int cloud;              /* 0 when signed out: nothing is kept on the account */
    const char *updated_at;
    int has_data;           /* saveLoad: there is a save */
    const char *data_json;  /* saveLoad: the saved JSON */
    const char *build;      /* saveLoad */
  } save;

  struct { /* openFeedback */
    int opened;           /* the sheet (or, in_browser, the game's YouGame page) opened */
    int reason;           /* YG_REASON_* */
    int retry_in_seconds; /* TOO_SOON: when it opens again */
    int in_browser;       /* started alone, no app answered: the page opened in the browser */
    const char *player_message; /* words for the player, or "" */
  } feedback;

  int menu_opened; /* openMenu/menuButton: YouGame's play menu opened; else keep the game's own menu */
} yg_result;

/* Every call's answer. Runs inside yg_update(), exactly once per call made after yg_init. */
typedef void (*yg_callback)(void *user, const yg_result *result);

typedef struct yg_config {
  /* The listing slug: the part after /g/ in the game's YouGame address (a-z, 0-9, '-'). May be
   * NULL or "": then a copy started alone does not look for the app and feedback never opens a
   * page (leave it empty in a build for a place whose players may be under 18). */
  const char *game_slug;
  const char *game_version;  /* the build's version, told to an app found on the local channel */
  const char *engine;        /* a short label for hello's "engine"; NULL: "native" */
  const char *feedback_site; /* where the feedback page opens; NULL: https://yougame.co (staging: https://staging.yougame.co) */
  void *user;                /* passed to every callback below */
  /* One line, without secrets, names, drafts or answers. NULL: written to stderr. */
  void (*log)(void *user, int level, const char *message);
  void (*on_ready)(void *user); /* hello answered: the getters are filled in */
  /* A push from YouGame: its "type" and the whole object as JSON. */
  void (*on_push)(void *user, const char *type, const char *json);
  /* The app is closing the game (stdio only): save now; the app ends the process 5 s later. */
  void (*on_quit)(void *user);
  void (*on_volume)(void *user, float volume); /* the app's volume slider, 0..1 (stdio only) */
  /* Every line from the app that is not the answer to one of this header's calls (pushes and
   * "embed" lines too), for another client such as yougame_net.h (ygnet_dispatch). */
  void (*on_line)(void *user, const char *line, size_t len);
  /* The player arrived after hello: hello answered while YouGame was still finding out who is
   * signed in (yg_player_status() was YG_PLAYER_PENDING), and now yg_player_name(), yg_player_id()
   * and yg_hello_json() say who. At most once per connection to YouGame; never for a player hello
   * already named (on_ready covers that). */
  void (*on_player)(void *user);
  /* Which YouGame this build talks to. 0 (the default, also when the field is left out): production,
   * yougame.co and the YouGame app. 1: staging, staging.yougame.co and the YouGame Staging app, for a
   * test listing on staging. A copy started on its own looks only for that app's local channel, and
   * feedback_site defaults to that site. A copy the app starts uses that app's site, whatever this says.
   * Set yg_start_options.staging (yougame_start.h) to the same value. */
  int staging;
} yg_config;

/* Picks the channel from the environment and says hello. 0 ok, -1 already started. Lines it logs
 * reach config.log from the first yg_update. */
int yg_init(const yg_config *config);
/* Stops the thread. Calls still waiting are dropped without their callback, and lines not yet
 * written to the app are dropped unsent. Returns within about 2 s even when the app stopped
 * reading. Safe inside a callback. */
void yg_shutdown(void);
/* Once a frame, from the game's thread: every callback runs in here. */
void yg_update(void);

/* Each returns the call's id (> 0), or 0 before yg_init (then the callback never runs). */
long long yg_request_name(yg_callback cb, void *user);
long long yg_set_name(const char *name, yg_callback cb, void *user);
long long yg_submit_score(long long score, yg_callback cb, void *user);
/* Best-effort match hint: nonzero open means true. Ignore refusals; NULL cb is allowed. */
long long yg_match_state(int open, yg_callback cb, void *user);
/* period YG_PERIOD_*; limit 1..100 (0: 10); friends: only the player and their friends. */
long long yg_leaderboard(int period, int limit, int friends, yg_callback cb, void *user);
/* data_json: any JSON but null, at most 64 KiB. */
long long yg_save_write(const char *data_json, yg_callback cb, void *user);
long long yg_save_load(yg_callback cb, void *user);
long long yg_save_clear(yg_callback cb, void *user);
/* kind YG_FEEDBACK_*; draft_text (UTF-8, may be NULL) is cut to 2,000 characters. */
long long yg_open_feedback(int kind, const char *draft_text, yg_callback cb, void *user);
long long yg_open_menu(yg_callback cb, void *user);
/* Report the game's own input events, never poll input for the plugin. kind: keys, ps, pad, gc.
 * Call set_input_kind on changes; menu_button once on the menu press (PS/Home, or GC chord).
 * Best effort: older apps answer notYet; keep the game's own menu if result.opened is false. */
long long yg_set_input_kind(const char *kind, yg_callback cb, void *user);
long long yg_menu_button(const char *kind, yg_callback cb, void *user);
/* Report each change of the game's state (not every frame): playing, paused or in a menu, so the
 * app's overlay can keep YouGame's button out of play and show it on pause and in menus (an app
 * that does not follow it yet keeps the button's usual fade). Fire-and-forget, no callback: the
 * answer is ignored. Sent only when the state changed; called before YouGame answers (or while it
 * is away), the latest state goes once hello is answered, and again on every new connection. Kept
 * until yg_shutdown: after a new yg_init, say it again. 1 sent now, 0 kept for later or unchanged,
 * -1 not a state or before yg_init. */
int yg_set_game_state(yg_game_state state);
/* Any other wire type in sdk.md "Native builds" (saveFileList, entitlements, ...). body_json is a
 * JSON object or NULL; timeout_seconds <= 0 means 15. */
long long yg_call(const char *type, const char *body_json, double timeout_seconds, yg_callback cb,
                  void *user);

/* stdio only: "escape" or "shift-escape", for the game's own key handling (sdk.md). */
void yg_relay_key(const char *key);
/* For another client on the same channel (yougame_net.h): the shared request counter, and one
 * raw line to the app ("\n" added when missing). 0 queued, -1 refused. */
long long yg_next_id(void);
int yg_write_line(const char *line, size_t len);

/* State. Strings are "" when unknown and stay valid until the next yg_update or yg_shutdown. */
int yg_connected(void);              /* YouGame answers now */
int yg_ready(void);                  /* hello answered */
yg_started yg_started_by(void);
yg_transport yg_transport_kind(void);
yg_player_state yg_player_status(void); /* YG_PLAYER_PENDING: not yet, on_player will say */
const char *yg_player_name(void);    /* "" until known: show "Player" */
const char *yg_player_id(void);      /* this game's own id for the player */
const char *yg_game_id(void);
int yg_name_shared(void);
const char *yg_hello_json(void);     /* hello's whole answer */
int yg_should_show_feedback_button(void);
int yg_quit_requested(void);

/* JSON without a JSON library. path is dotted: "save.data.level", "entries.0.name" (a number
 * indexes an array); NULL or "" is the whole value. Every one checks the whole text first. */
/* A string, unescaped to UTF-8 into out (always NUL-terminated, cut on a character boundary).
 * Its full length in bytes, or -1 when absent, not a string, or json is not JSON. A "\u0000" in
 * the string becomes a NUL byte inside out, and the length counts it: use the length, not strlen,
 * when that matters. */
long yg_json_string(const char *json, const char *path, char *out, size_t out_size);
int yg_json_number(const char *json, const char *path, double *out); /* 1 found, 0 not */
int yg_json_bool(const char *json, const char *path, int *out);       /* 1 found, 0 not */
long yg_json_count(const char *json, const char *path); /* an array's or object's size, or -1 */
/* The value's JSON text, copied only when it fits. Its length, or -1. */
long yg_json_raw(const char *json, const char *path, char *out, size_t out_size);

const char *yg_reason_name(int reason);
int yg_valid_slug(const char *slug); /* ^[a-z0-9-]{1,80}$ */
/* `<site>/g/<slug>?feedback=bug|idea|1` (site NULL: https://yougame.co). Its length, or -1 when the
 * slug or site is not valid or out is too small (then out is ""). */
long yg_feedback_page_url(const char *site, const char *slug, int kind, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
#endif /* YOUGAME_H */

/* ============================================================================================= */

#ifdef YOUGAME_IMPLEMENTATION
#ifndef YOUGAME_IMPLEMENTED
#define YOUGAME_IMPLEMENTED

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <aclapi.h>
#include <objbase.h>
#include <process.h>
#include <sddl.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef PIPE_BUF
#define PIPE_BUF 512 /* the least POSIX allows */
#endif
#ifdef __APPLE__
#include <crt_externs.h>
#define YG__ENVIRON (*_NSGetEnviron())
#else
extern char **environ;
#define YG__ENVIRON environ
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------
 * Limits, from the app's bridge and local channel (desktop/src bridge.ts, local-channel.ts). */

#define YG__STDIO_LINE_MAX ((size_t)48 * 1024 * 1024) /* bridge.ts STDIO_LINE_LIMIT, both ways */
#define YG__LOCAL_LINE_MAX ((size_t)200 * 1024)        /* contract section 3: full escaped 32 KiB world message plus framing */
#define YG__LOCAL_LINE_OLD ((size_t)64 * 1024)        /* an app whose hello names no lineMax (older): its limit */
#define YG__LOCAL_READ_MAX ((size_t)8 * 1024 * 1024)      /* a 1 MiB relay frame can take 6 MiB escaped */
#define YG__INBOX_MAX ((size_t)64 * 1024 * 1024)      /* unread lines held for yg_update, in bytes */
#define YG__OUTBOX_MAX ((size_t)64 * 1024 * 1024)     /* lines waiting to go out, in bytes */
#define YG__ALLOC_SLACK 16                            /* what an allocation costs beyond its size */
#define YG__STOP_WAIT_MS 2000                         /* yg_shutdown waits this long for a write */
#define YG__TIMEOUT 15.0
#define YG__LOCAL_HELLO_TIMEOUT 6.0 /* contract section 1: no hello answer in 6 s is no host */
#define YG__PROBE_COOLDOWN 5.0      /* contract section 1.3 */
/* When a look finds no app (the game started first, then the player installed or opened YouGame)
 * or the app ends a connection (an update restarting it, the player quitting and opening it
 * again), the game looks again on its own, every 5 s for 5 minutes, so it connects without waiting
 * for its next call. After that it looks on calls only, and the next call that finds nothing starts
 * another 5 minutes. */
#define YG__RELOOK_EVERY 5.0
#define YG__RELOOK_FOR 300.0
/* Player pending (contract section 4; desktop/src/local-channel.ts "PLAYER PENDING"): after a hello
 * answered playerPending, hello is asked again about 2 s and about 6 s later, unless the player came
 * first (in an answer or the playerArrived push). */
#define YG__PLAYER_RETRY_1 2.0
#define YG__PLAYER_RETRY_2 6.0
#define YG__EVENTS_PER_UPDATE 1024
#define YG__BOARD_ROWS_MAX 100 /* leaderboard rows kept from one answer */
#define YG__DRAFT_MAX 2000 /* UTF-16 units, as YouGame.feedback() */
#define YG__NAME_MAX 24    /* UTF-16 units, as sdk.js cleanName */
#define YG__SAVE_MAX 65536
#define YG__MAX_SCORE 1000000000000000LL
#define YG__JSON_DEPTH 128
#define YG__READ_CHUNK 65536
#define YG__PIPE_BUSY_WAIT_MS 200
#define YG__READ_WAIT_MS 250
#define YG__DEFAULT_SITE "https://yougame.co"
#define YG__STAGING_SITE "https://staging.yougame.co"

#ifdef YOUGAME_TESTING
/* Test hooks: the test file sets these. Never defined in a game build. */
static const char *yg__test_socket_root = NULL; /* replaces "/tmp" in the Mac socket path */
static int (*yg__test_open_url)(const char *url) = NULL;
static double yg__test_local_hello_timeout = 0.0;
static double yg__test_probe_cooldown = -1.0;
static double yg__test_relook_every = -1.0; /* replaces YG__RELOOK_EVERY when set */
static size_t yg__test_outbox_max = 0;
static double yg__test_player_retry[2] = {0.0, 0.0}; /* replaces YG__PLAYER_RETRY_1 and _2 when set */
#endif

/* ---------------------------------------------------------------------------------------------
 * Small helpers */

typedef struct yg__buf {
  char *p;
  size_t len, cap;
  int failed;
} yg__buf;

static void yg__buf_put(yg__buf *b, const char *s, size_t n) {
  size_t cap;
  char *p;
  if (b->failed) return;
  if (n > (size_t)-1 / 2 - b->len) {
    b->failed = 1;
    return;
  }
  if (b->len + n + 1 > b->cap) {
    cap = b->cap ? b->cap : 64;
    while (cap < b->len + n + 1) cap *= 2;
    p = (char *)realloc(b->p, cap);
    if (!p) {
      b->failed = 1;
      return;
    }
    b->p = p;
    b->cap = cap;
  }
  if (n) memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = 0;
}

static void yg__buf_puts(yg__buf *b, const char *s) { yg__buf_put(b, s, strlen(s)); }
static void yg__buf_putc(yg__buf *b, char c) { yg__buf_put(b, &c, 1); }

static void yg__buf_int(yg__buf *b, long long v) {
  char t[24];
  int i = (int)sizeof t;
  unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
  do {
    t[--i] = (char)('0' + (int)(u % 10));
    u /= 10;
  } while (u && i > 1);
  if (v < 0) t[--i] = '-';
  yg__buf_put(b, t + i, sizeof t - (size_t)i);
}

static void yg__buf_free(yg__buf *b) {
  free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
  b->failed = 0;
}

static char *yg__strndup(const char *s, size_t n) {
  char *p = (char *)malloc(n + 1);
  if (!p) return NULL;
  if (n) memcpy(p, s, n);
  p[n] = 0;
  return p;
}

static int yg__is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

/* One code point of UTF-8; U+FFFD for a bad byte (one byte used). */
static unsigned long yg__utf8_next(const unsigned char *s, size_t n, size_t *used) {
  unsigned long c, min;
  size_t need, i;
  *used = 1;
  if (n == 0) return 0xFFFD;
  c = s[0];
  if (c < 0x80) return c;
  if (c >= 0xC2 && c <= 0xDF) {
    need = 1;
    c &= 0x1F;
    min = 0x80;
  } else if (c >= 0xE0 && c <= 0xEF) {
    need = 2;
    c &= 0x0F;
    min = 0x800;
  } else if (c >= 0xF0 && c <= 0xF4) {
    need = 3;
    c &= 0x07;
    min = 0x10000;
  } else {
    return 0xFFFD;
  }
  if (n < need + 1) return 0xFFFD;
  for (i = 1; i <= need; ++i) {
    if ((s[i] & 0xC0) != 0x80) return 0xFFFD;
    c = (c << 6) | (unsigned long)(s[i] & 0x3F);
  }
  if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0xFFFD;
  *used = need + 1;
  return c;
}

/* One code point as UTF-8 into t; its length. A surrogate or out-of-range value is U+FFFD. */
static size_t yg__utf8_enc(unsigned long c, char t[4]) {
  if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
  if (c < 0x80) {
    t[0] = (char)c;
    return 1;
  }
  if (c < 0x800) {
    t[0] = (char)(0xC0 | (c >> 6));
    t[1] = (char)(0x80 | (c & 0x3F));
    return 2;
  }
  if (c < 0x10000) {
    t[0] = (char)(0xE0 | (c >> 12));
    t[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    t[2] = (char)(0x80 | (c & 0x3F));
    return 3;
  }
  t[0] = (char)(0xF0 | (c >> 18));
  t[1] = (char)(0x80 | ((c >> 12) & 0x3F));
  t[2] = (char)(0x80 | ((c >> 6) & 0x3F));
  t[3] = (char)(0x80 | (c & 0x3F));
  return 4;
}

static void yg__utf8_put(yg__buf *b, unsigned long c) {
  char t[4];
  yg__buf_put(b, t, yg__utf8_enc(c, t));
}

/* ---------------------------------------------------------------------------------------------
 * JSON. Text is checked once (yg__json_check); after that the walkers trust its shape. Nothing is
 * built in memory: lookups scan the text, so a line costs only its own bytes. */

typedef struct yg__span {
  const char *p;
  size_t n;
} yg__span;

static const yg__span yg__null_span = {"null", 4};

static const char *yg__ws(const char *p, const char *e) {
  while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  return p;
}

static int yg__hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int yg__digit(const char *p, const char *e) { return p < e && *p >= '0' && *p <= '9'; }

/* p at the opening quote; after the closing one, or NULL. Raw control characters are refused. */
static const char *yg__skip_string(const char *p, const char *e) {
  int i;
  ++p;
  while (p < e) {
    unsigned char c = (unsigned char)*p;
    if (c == '"') return p + 1;
    if (c < 0x20) return NULL;
    if (c != '\\') {
      ++p;
      continue;
    }
    if (++p >= e) return NULL;
    switch (*p) {
      case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
        ++p;
        break;
      case 'u':
        if (e - p < 5) return NULL;
        for (i = 1; i <= 4; ++i)
          if (yg__hex(p[i]) < 0) return NULL;
        p += 5;
        break;
      default:
        return NULL;
    }
  }
  return NULL;
}

static const char *yg__skip_number(const char *p, const char *e) {
  if (p < e && *p == '-') ++p;
  if (p >= e) return NULL;
  if (*p == '0') {
    ++p;
  } else if (*p >= '1' && *p <= '9') {
    while (yg__digit(p, e)) ++p;
  } else {
    return NULL;
  }
  if (p < e && *p == '.') {
    ++p;
    if (!yg__digit(p, e)) return NULL;
    while (yg__digit(p, e)) ++p;
  }
  if (p < e && (*p == 'e' || *p == 'E')) {
    ++p;
    if (p < e && (*p == '+' || *p == '-')) ++p;
    if (!yg__digit(p, e)) return NULL;
    while (yg__digit(p, e)) ++p;
  }
  return p;
}

static const char *yg__skip_word(const char *p, const char *e, const char *word, size_t n) {
  if ((size_t)(e - p) < n || memcmp(p, word, n) != 0) return NULL;
  return p + n;
}

static const char *yg__skip_value(const char *p, const char *e, int depth) {
  char close;
  if (p >= e) return NULL;
  switch (*p) {
    case '"':
      return yg__skip_string(p, e);
    case '{':
    case '[':
      if (depth >= YG__JSON_DEPTH) return NULL;
      close = *p == '{' ? '}' : ']';
      p = yg__ws(p + 1, e);
      if (p < e && *p == close) return p + 1;
      for (;;) {
        if (close == '}') {
          if (p >= e || *p != '"') return NULL;
          p = yg__skip_string(p, e);
          if (!p) return NULL;
          p = yg__ws(p, e);
          if (p >= e || *p != ':') return NULL;
          p = yg__ws(p + 1, e);
        }
        p = yg__skip_value(p, e, depth + 1);
        if (!p) return NULL;
        p = yg__ws(p, e);
        if (p >= e) return NULL;
        if (*p == ',') {
          p = yg__ws(p + 1, e);
          continue;
        }
        return *p == close ? p + 1 : NULL;
      }
    case 't':
      return yg__skip_word(p, e, "true", 4);
    case 'f':
      return yg__skip_word(p, e, "false", 5);
    case 'n':
      return yg__skip_word(p, e, "null", 4);
    default:
      return yg__skip_number(p, e);
  }
}

/* Exactly one JSON value, with only whitespace around it. */
static int yg__json_check(const char *text, size_t n, yg__span *out) {
  const char *e = text + n;
  const char *p = yg__ws(text, e);
  const char *v = yg__skip_value(p, e, 0);
  if (!v || yg__ws(v, e) != e) return 0;
  out->p = p;
  out->n = (size_t)(v - p);
  return 1;
}

static char yg__kind(yg__span v) { return v.n ? v.p[0] : 0; }
static int yg__is_obj(yg__span v) { return yg__kind(v) == '{'; }
static int yg__is_str(yg__span v) { return yg__kind(v) == '"'; }
static int yg__is_num(yg__span v) { return yg__kind(v) == '-' || (yg__kind(v) >= '0' && yg__kind(v) <= '9'); }

/* The members of a checked object or array, in order. */
typedef struct yg__iter {
  const char *p, *e;
  int obj, first;
} yg__iter;

static int yg__iter_begin(yg__iter *it, yg__span v) {
  if (yg__kind(v) != '{' && yg__kind(v) != '[') return 0;
  it->obj = v.p[0] == '{';
  it->p = v.p + 1;
  it->e = v.p + v.n;
  it->first = 1;
  return 1;
}

static int yg__iter_next(yg__iter *it, yg__span *key, yg__span *val) {
  const char *p = yg__ws(it->p, it->e), *s;
  if (p >= it->e || *p == '}' || *p == ']') return 0;
  if (!it->first) {
    if (*p != ',') return 0;
    p = yg__ws(p + 1, it->e);
  }
  it->first = 0;
  if (it->obj) {
    s = p;
    p = yg__skip_string(p, it->e);
    if (!p) return 0;
    if (key) {
      key->p = s;
      key->n = (size_t)(p - s);
    }
    p = yg__ws(p, it->e);
    if (p >= it->e || *p != ':') return 0;
    p = yg__ws(p + 1, it->e);
  }
  s = p;
  p = yg__skip_value(p, it->e, 0);
  if (!p) return 0;
  val->p = s;
  val->n = (size_t)(p - s);
  it->p = p;
  return 1;
}

/* One code point of a checked string token; *pp starts after the opening quote. 0 at the end.
 * \uXXXX pairs join; a lone surrogate becomes U+FFFD, as does a bad UTF-8 byte. */
static int yg__str_next(const char **pp, const char *e, unsigned long *cp) {
  const char *p = *pp;
  unsigned long u, lo;
  size_t used;
  if (p >= e || *p == '"') return 0;
  if (*p != '\\') {
    *cp = yg__utf8_next((const unsigned char *)p, (size_t)(e - p), &used);
    *pp = p + used;
    return 1;
  }
  switch (p[1]) {
    case 'b': *cp = 8; break;
    case 'f': *cp = 12; break;
    case 'n': *cp = 10; break;
    case 'r': *cp = 13; break;
    case 't': *cp = 9; break;
    case 'u':
      u = (unsigned long)((yg__hex(p[2]) << 12) | (yg__hex(p[3]) << 8) | (yg__hex(p[4]) << 4) | yg__hex(p[5]));
      p += 6;
      if (u >= 0xD800 && u <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
        lo = (unsigned long)((yg__hex(p[2]) << 12) | (yg__hex(p[3]) << 8) | (yg__hex(p[4]) << 4) | yg__hex(p[5]));
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
          p += 6;
        } else {
          u = 0xFFFD;
        }
      } else if (u >= 0xD800 && u <= 0xDFFF) {
        u = 0xFFFD;
      }
      *cp = u;
      *pp = p;
      return 1;
    default: *cp = (unsigned char)p[1]; break; /* " \ / */
  }
  *pp = p + 2;
  return 1;
}

/* A checked string token's text as UTF-8. */
static void yg__str_decode(yg__buf *b, yg__span s) {
  const char *p = s.p + 1;
  unsigned long cp;
  yg__buf_put(b, "", 0);
  while (yg__str_next(&p, s.p + s.n, &cp)) yg__utf8_put(b, cp);
}

static int yg__str_equals(yg__span s, const char *key, size_t klen) {
  const char *p = s.p + 1;
  unsigned long cp;
  size_t at = 0, n;
  char t[4];
  while (yg__str_next(&p, s.p + s.n, &cp)) {
    n = yg__utf8_enc(cp, t);
    if (at + n > klen || memcmp(key + at, t, n) != 0) return 0;
    at += n;
  }
  return at == klen;
}

/* The value of key in a checked object; the last one wins, as in JSON.parse. */
static int yg__get(yg__span obj, const char *key, yg__span *out) {
  yg__iter it;
  yg__span k, v;
  size_t klen = strlen(key);
  int found = 0;
  if (!yg__is_obj(obj) || !yg__iter_begin(&it, obj)) return 0;
  while (yg__iter_next(&it, &k, &v)) {
    if (yg__str_equals(k, key, klen)) {
      *out = v;
      found = 1;
    }
  }
  return found;
}

static long yg__count(yg__span v) {
  yg__iter it;
  yg__span k, x;
  long n = 0;
  if (!yg__iter_begin(&it, v)) return -1;
  while (yg__iter_next(&it, &k, &x)) ++n;
  return n;
}

/* A dotted path from a checked value. */
static int yg__path(yg__span root, const char *path, yg__span *out) {
  yg__span cur = root, next;
  const char *seg = path, *dot;
  char key[256];
  size_t n, i, index;
  int numeric;
  yg__iter it;
  if (!path || !*path) {
    *out = root;
    return 1;
  }
  for (;;) {
    dot = strchr(seg, '.');
    n = dot ? (size_t)(dot - seg) : strlen(seg);
    if (n == 0 || n >= sizeof key) return 0;
    memcpy(key, seg, n);
    key[n] = 0;
    if (yg__kind(cur) == '{') {
      if (!yg__get(cur, key, &next)) return 0;
    } else if (yg__kind(cur) == '[') {
      numeric = n <= 9;
      index = 0;
      for (i = 0; i < n && numeric; ++i) {
        if (key[i] < '0' || key[i] > '9') numeric = 0;
        else index = index * 10 + (size_t)(key[i] - '0');
      }
      if (!numeric || !yg__iter_begin(&it, cur)) return 0;
      for (i = 0;; ++i) {
        if (!yg__iter_next(&it, NULL, &next)) return 0;
        if (i == index) break;
      }
    } else {
      return 0;
    }
    cur = next;
    if (!dot) break;
    seg = dot + 1;
  }
  *out = cur;
  return 1;
}

/* A checked number token as a double, whatever the C locale's decimal point is. */
static int yg__num(yg__span v, double *out) {
  char tmp[512];
  size_t i, digits = 0;
  int plain = 1;
  long long whole = 0;
  const char *point;
  char *end = NULL;
  if (!yg__is_num(v) || v.n >= sizeof tmp) return 0;
  for (i = v.p[0] == '-' ? 1 : 0; i < v.n; ++i) {
    if (v.p[i] < '0' || v.p[i] > '9') {
      plain = 0;
      break;
    }
    whole = whole * 10 + (v.p[i] - '0');
    if (++digits > 15) {
      plain = 0;
      break;
    }
  }
  if (plain) {
    *out = v.p[0] == '-' ? -(double)whole : (double)whole;
    return 1;
  }
  memcpy(tmp, v.p, v.n);
  tmp[v.n] = 0;
  point = localeconv()->decimal_point;
  if (point && point[0] && point[0] != '.' && !point[1]) {
    for (i = 0; i < v.n; ++i)
      if (tmp[i] == '.') tmp[i] = point[0];
  }
  *out = strtod(tmp, &end);
  return end == tmp + v.n;
}

/* A string as a JSON string token: the escapes JSON.stringify writes; bad UTF-8 becomes U+FFFD. */
static void yg__put_cp_escaped(yg__buf *b, unsigned long cp) {
  static const char hexd[] = "0123456789abcdef";
  char t[6];
  switch (cp) {
    case '"': yg__buf_put(b, "\\\"", 2); return;
    case '\\': yg__buf_put(b, "\\\\", 2); return;
    case 8: yg__buf_put(b, "\\b", 2); return;
    case 12: yg__buf_put(b, "\\f", 2); return;
    case 10: yg__buf_put(b, "\\n", 2); return;
    case 13: yg__buf_put(b, "\\r", 2); return;
    case 9: yg__buf_put(b, "\\t", 2); return;
    default: break;
  }
  if (cp < 0x20) {
    t[0] = '\\';
    t[1] = 'u';
    t[2] = '0';
    t[3] = '0';
    t[4] = hexd[(cp >> 4) & 0xF];
    t[5] = hexd[cp & 0xF];
    yg__buf_put(b, t, 6);
    return;
  }
  yg__utf8_put(b, cp);
}

static void yg__json_str(yg__buf *b, const char *s, size_t n) {
  size_t i = 0, used;
  yg__buf_putc(b, '"');
  while (i < n) {
    yg__put_cp_escaped(b, yg__utf8_next((const unsigned char *)s + i, n - i, &used));
    i += used;
  }
  yg__buf_putc(b, '"');
}

/* A checked value written condensed: no whitespace, strings re-escaped, numbers as they were. */
static void yg__emit(yg__buf *b, yg__span v) {
  yg__iter it;
  yg__span k, x;
  const char *p;
  unsigned long cp;
  int first = 1;
  switch (yg__kind(v)) {
    case '"':
      yg__buf_putc(b, '"');
      p = v.p + 1;
      while (yg__str_next(&p, v.p + v.n, &cp)) yg__put_cp_escaped(b, cp);
      yg__buf_putc(b, '"');
      return;
    case '{':
    case '[':
      yg__iter_begin(&it, v);
      yg__buf_putc(b, v.p[0]);
      while (yg__iter_next(&it, &k, &x)) {
        if (!first) yg__buf_putc(b, ',');
        first = 0;
        if (it.obj) {
          yg__emit(b, k);
          yg__buf_putc(b, ':');
        }
        yg__emit(b, x);
      }
      yg__buf_putc(b, v.p[0] == '{' ? '}' : ']');
      return;
    default:
      yg__buf_put(b, v.p, v.n);
      return;
  }
}

/* ---------------------------------------------------------------------------------------------
 * Lines: bytes split on "\n", a trailing "\r" dropped, and a line over the limit dropped whole up to
 * its newline, so a stream never grows memory without bound. */

typedef void (*yg__line_fn)(void *ctx, char *line, size_t len);

typedef struct yg__lines {
  char *p;
  size_t len, cap, limit;
  int dropping;
  long dropped;
} yg__lines;

static void yg__lines_append(yg__lines *s, const char *d, size_t n) {
  size_t cap;
  char *p;
  if (s->dropping || n == 0) return;
  if (s->len + n > s->limit) {
    free(s->p);
    s->p = NULL;
    s->len = s->cap = 0;
    s->dropping = 1;
    ++s->dropped;
    return;
  }
  if (s->len + n + 1 > s->cap) {
    cap = s->cap ? s->cap : 256;
    while (cap < s->len + n + 1) cap *= 2;
    p = (char *)realloc(s->p, cap);
    if (!p) { /* out of memory: as if the line were too long */
      free(s->p);
      s->p = NULL;
      s->len = s->cap = 0;
      s->dropping = 1;
      ++s->dropped;
      return;
    }
    s->p = p;
    s->cap = cap;
  }
  memcpy(s->p + s->len, d, n);
  s->len += n;
}

static void yg__lines_feed(yg__lines *s, const char *d, size_t n, yg__line_fn fn, void *ctx) {
  size_t i, start = 0, len;
  char empty[1];
  for (i = 0; i < n; ++i) {
    if (d[i] != '\n') continue;
    yg__lines_append(s, d + start, i - start);
    start = i + 1;
    if (s->dropping) {
      s->dropping = 0; /* the newline ends the dropped line */
      continue;
    }
    len = s->len;
    if (len > 0 && s->p[len - 1] == '\r') --len;
    if (s->p) {
      s->p[len] = 0;
      fn(ctx, s->p, len);
    } else {
      empty[0] = 0;
      fn(ctx, empty, 0);
    }
    if (s->cap > 1024 * 1024) { /* a big line's buffer goes back rather than staying for the run */
      free(s->p);
      s->p = NULL;
      s->cap = 0;
    }
    s->len = 0;
  }
  if (start < n) yg__lines_append(s, d + start, n - start);
}

static void yg__lines_free(yg__lines *s) {
  free(s->p);
  s->p = NULL;
  s->len = s->cap = 0;
  s->dropping = 0;
}

/* ---------------------------------------------------------------------------------------------
 * One line from the app. */

enum { YG__HOST_IGNORED, YG__HOST_ANSWER, YG__HOST_PUSH, YG__HOST_QUIT, YG__HOST_VOLUME, YG__HOST_EMBED };

typedef struct yg__host {
  int kind;
  long long id;
  yg__span obj;  /* the yg object */
  yg__span push; /* the push object */
  float volume;
} yg__host;

/* `embed volume v`: 0..1 with at most one dot, up to 8 characters (desktop native.ts). */
static int yg__parse_volume(const char *t, size_t n, float *out) {
  size_t i;
  int dots = 0, digits = 0, after = 0;
  double whole = 0, frac = 0, scale = 1;
  if (n == 0 || n > 8) return 0;
  for (i = 0; i < n; ++i) {
    if (t[i] == '.') {
      if (++dots > 1) return 0;
      after = 1;
    } else if (t[i] >= '0' && t[i] <= '9') {
      ++digits;
      if (after) {
        scale /= 10;
        frac += (t[i] - '0') * scale;
      } else {
        whole = whole * 10 + (t[i] - '0');
      }
    } else {
      return 0;
    }
  }
  if (digits == 0 || whole + frac > 1.0) return 0;
  *out = (float)(whole + frac);
  return 1;
}

static void yg__parse_host(const char *line, size_t n, yg__host *out) {
  yg__span obj, re, push;
  double id;
  memset(out, 0, sizeof *out);
  out->volume = 1.0f;
  if (n > 0 && line[n - 1] == '\r') --n;
  if (n >= 3 && memcmp(line, "yg ", 3) == 0) {
    if (!yg__json_check(line + 3, n - 3, &obj) || !yg__is_obj(obj)) return;
    out->obj = obj;
    if (yg__get(obj, "re", &re)) {
      /* Ids are integers within +-2^53 (sdk.md); anything else is not an answer. */
      if (!yg__num(re, &id) || !(id >= -9007199254740991.0 && id <= 9007199254740991.0) ||
          (double)(long long)id != id)
        return;
      out->kind = YG__HOST_ANSWER;
      out->id = (long long)id;
      return;
    }
    if (yg__get(obj, "push", &push) && yg__is_obj(push)) {
      out->kind = YG__HOST_PUSH;
      out->push = push;
    }
    return;
  }
  if (n >= 6 && memcmp(line, "embed ", 6) == 0) {
    line += 6;
    n -= 6;
    if (n == 4 && memcmp(line, "quit", 4) == 0) {
      out->kind = YG__HOST_QUIT;
      return;
    }
    out->kind = YG__HOST_EMBED;
    if (n >= 7 && memcmp(line, "volume ", 7) == 0 && yg__parse_volume(line + 7, n - 7, &out->volume))
      out->kind = YG__HOST_VOLUME;
  }
}

/* ---------------------------------------------------------------------------------------------
 * The rules the web SDK applies before a request leaves (sdk.js), kept the same here */

/* JS's \s, for sdk.js cleanName. */
static int yg__js_space(unsigned long c) {
  return (c >= 9 && c <= 13) || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

/* sdk.js cleanName: runs of whitespace become one space, trimmed, at most 24 UTF-16 units. */
static void yg__clean_name(yg__buf *out, const char *s) {
  size_t n = s ? strlen(s) : 0, i = 0, used, units = 0, w;
  unsigned long c;
  int space = 0;
  yg__buf_put(out, "", 0);
  while (i < n) {
    c = yg__utf8_next((const unsigned char *)s + i, n - i, &used);
    i += used;
    if (yg__js_space(c)) {
      space = 1;
      continue;
    }
    if (space && out->len > 0) {
      if (units + 1 > YG__NAME_MAX) break;
      yg__buf_putc(out, ' ');
      ++units;
    }
    space = 0;
    w = c > 0xFFFF ? 2 : 1;
    if (units + w > YG__NAME_MAX) break;
    yg__utf8_put(out, c);
    units += w;
  }
  /* A cut right after a space leaves it at the end: trim it, as .trim() before .slice() would not
   * leave one either when the next character was cut. */
  while (out->len > 0 && out->p[out->len - 1] == ' ') out->p[--out->len] = 0;
}

/* YouGame.feedback()'s cut: at most 2,000 UTF-16 units, never half a pair. */
static void yg__cut_draft(yg__buf *out, const char *s) {
  size_t n = s ? strlen(s) : 0, i = 0, used, units = 0, w;
  unsigned long c;
  yg__buf_put(out, "", 0);
  while (i < n) {
    c = yg__utf8_next((const unsigned char *)s + i, n - i, &used);
    w = c > 0xFFFF ? 2 : 1;
    if (units + w > YG__DRAFT_MAX) break;
    yg__utf8_put(out, c);
    units += w;
    i += used;
  }
}

/* hello's version: printable ASCII only, at most 64 characters, trimmed; "unknown" when empty. */
static void yg__clean_version(char out[65], const char *s) {
  size_t n = 0, start = 0;
  out[0] = 0;
  for (; s && *s && n < 64; ++s)
    if ((unsigned char)*s >= 0x20 && (unsigned char)*s <= 0x7E) out[n++] = *s;
  out[n] = 0;
  while (start < n && out[start] == ' ') ++start;
  while (n > start && out[n - 1] == ' ') out[--n] = 0;
  if (start) memmove(out, out + start, n - start + 1);
  if (!out[0]) memcpy(out, "unknown", 8);
}

int yg_valid_slug(const char *slug) {
  size_t n;
  if (!slug) return 0;
  for (n = 0; slug[n]; ++n) {
    char c = slug[n];
    if (n >= 80 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
  }
  return n >= 1;
}

/* A site for the feedback page: https:// with a host and optional port, no path, query or user; or
 * http:// on localhost or 127.0.0.1. Empty: https://yougame.co. Out: scheme://host[:port], lower. */
static int yg__normalize_site(const char *site, char *out, size_t out_size) {
  char rest[300];
  size_t n, i, port_len = 0, host_len;
  const char *s = site ? site : "";
  int https = 0;
  long port = 0;
  char *colon;
  while (yg__is_ws(*s)) ++s;
  n = strlen(s);
  while (n > 0 && yg__is_ws(s[n - 1])) --n;
  if (n == 0) {
    s = YG__DEFAULT_SITE;
    n = strlen(s);
  }
  if (n >= sizeof rest) return 0;
  for (i = 0; i < n; ++i) rest[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] - 'A' + 'a') : s[i];
  rest[n] = 0;
  if (strncmp(rest, "https://", 8) == 0) {
    https = 1;
    memmove(rest, rest + 8, n - 8 + 1);
  } else if (strncmp(rest, "http://", 7) == 0) {
    memmove(rest, rest + 7, n - 7 + 1);
  } else {
    return 0;
  }
  n = strlen(rest);
  if (n > 0 && rest[n - 1] == '/') rest[--n] = 0;
  colon = strchr(rest, ':');
  host_len = colon ? (size_t)(colon - rest) : n;
  if (colon) {
    port_len = strlen(colon + 1);
    if (port_len == 0 || port_len > 5) return 0;
    for (i = 1; i <= port_len; ++i) {
      if (colon[i] < '0' || colon[i] > '9') return 0;
      port = port * 10 + (colon[i] - '0');
    }
    if (port < 1 || port > 65535) return 0;
  }
  /* Letters, digits, dots and dashes only: no path, query, fragment, user or second colon. */
  if (host_len == 0 || host_len > 253 || rest[0] == '.' || rest[host_len - 1] == '.') return 0;
  for (i = 0; i < host_len; ++i) {
    char c = rest[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0;
    if (c == '.' && i + 1 < host_len && rest[i + 1] == '.') return 0;
  }
  if (!https && !(host_len == 9 && memcmp(rest, "localhost", 9) == 0) &&
      !(host_len == 9 && memcmp(rest, "127.0.0.1", 9) == 0))
    return 0; /* plain http only for a dev site on this machine */
  if (strlen(rest) + 8 + 1 > out_size) return 0;
  out[0] = 0;
  strcat(out, https ? "https://" : "http://");
  strcat(out, rest);
  return 1;
}

long yg_feedback_page_url(const char *site, const char *slug, int kind, char *out, size_t out_size) {
  char origin[320];
  const char *k = kind == YG_FEEDBACK_BUG ? "bug" : kind == YG_FEEDBACK_IDEA ? "idea" : "1";
  size_t need;
  if (out && out_size) out[0] = 0;
  if (!out || !yg_valid_slug(slug) || !yg__normalize_site(site, origin, sizeof origin)) return -1;
  need = strlen(origin) + 3 + strlen(slug) + 10 + strlen(k);
  if (need + 1 > out_size) return -1;
  strcpy(out, origin);
  strcat(out, "/g/");
  strcat(out, slug);
  strcat(out, "?feedback=");
  strcat(out, k);
  return (long)need;
}

#if defined(_WIN32) || defined(YOUGAME_TESTING)
/* A Windows user SID as text (S-1-5-21-...): ^S-1-[0-9]{1,14}(?:-[0-9]{1,10}){1,15}$ */
static int yg__valid_sid(const char *sid) {
  int parts = 0;
  size_t digits;
  const char *p;
  if (!sid || strncmp(sid, "S-1-", 4) != 0) return 0;
  p = sid + 4;
  for (;;) {
    digits = 0;
    while (*p >= '0' && *p <= '9') {
      ++p;
      ++digits;
    }
    if (digits == 0 || digits > (parts == 0 ? 14u : 10u)) return 0;
    ++parts;
    if (*p == 0) break;
    if (*p != '-') return 0;
    ++p;
  }
  return parts >= 2 && parts <= 16;
}

/* `\\.\pipe\yougame[-staging]-sdk-v1-<SID>`; 0 when the SID is not valid. */
static int yg__pipe_name(int staging, const char *sid, char *out, size_t out_size) {
  const char *base = staging ? "\\\\.\\pipe\\yougame-staging-sdk-v1-" : "\\\\.\\pipe\\yougame-sdk-v1-";
  if (!yg__valid_sid(sid) || strlen(base) + strlen(sid) + 1 > out_size) return 0;
  strcpy(out, base);
  strcat(out, sid);
  return 1;
}
#endif

#if !defined(_WIN32) || defined(YOUGAME_TESTING)
/* The unix sockets to try, in order (contract section 2). Returns how many (at most 3). */
static int yg__socket_paths(int is_linux, int staging, unsigned long uid, const char *runtime_dir,
                            char out[3][256]) {
  const char *name = staging ? "yougame-staging" : "yougame";
  const char *tmp = "/tmp";
  char runtime[200];
  size_t n;
  int count = 0, i, dup;
  char cand[256];
#ifdef YOUGAME_TESTING
  if (yg__test_socket_root) tmp = yg__test_socket_root;
#endif
  if (is_linux) {
    n = runtime_dir ? strlen(runtime_dir) : 0;
    if (n > 1 && n < sizeof runtime && runtime_dir[0] == '/') {
      memcpy(runtime, runtime_dir, n + 1);
      while (n > 1 && runtime[n - 1] == '/') runtime[--n] = 0;
      if (n > 1) {
        snprintf(out[count], 256, "%s/%s/sdk-v1.sock", runtime, name);
        ++count;
      }
    }
    snprintf(cand, sizeof cand, "/run/user/%lu/%s/sdk-v1.sock", uid, name);
    for (dup = 0, i = 0; i < count; ++i) dup |= strcmp(out[i], cand) == 0;
    if (!dup) strcpy(out[count++], cand);
  }
  snprintf(cand, sizeof cand, "%s/%s-%lu/sdk-v1.sock", tmp, name, uid);
  for (dup = 0, i = 0; i < count; ++i) dup |= strcmp(out[i], cand) == 0;
  if (!dup) strcpy(out[count++], cand);
  return count;
}
#endif

/* ---------------------------------------------------------------------------------------------
 * Platform: the lock, the clock, threads, the waker and the connections */

#ifdef _WIN32
static SRWLOCK yg__mu = SRWLOCK_INIT; /* a static initializer: never built, never torn down */
static void yg__lock(void) { AcquireSRWLockExclusive(&yg__mu); }
static void yg__unlock(void) { ReleaseSRWLockExclusive(&yg__mu); }
static double yg__now(void) { return (double)GetTickCount64() / 1000.0; }
#else
static pthread_mutex_t yg__mu = PTHREAD_MUTEX_INITIALIZER;
static void yg__lock(void) { pthread_mutex_lock(&yg__mu); }
static void yg__unlock(void) { pthread_mutex_unlock(&yg__mu); }
static double yg__now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
#endif

/* ---------------------------------------------------------------------------------------------
 * State */

enum { YG__IDLE, YG__PROBING, YG__CONNECTED };

enum {
  YG__EV_LINE,     /* a line from the app */
  YG__EV_REPLY,    /* a failure made here, for call id */
  YG__EV_CLOSED,   /* stdin ended: nothing will answer again */
  YG__EV_UP,       /* the local channel connected for look id */
  YG__EV_DOWN,     /* look id found nothing (ok 1: no app channel to open), or its connection ended */
  YG__EV_BROWSER,  /* the feedback page opener for call id finished; ok */
  YG__EV_LOG       /* a log line from the thread (text is a literal) */
};

typedef struct yg__event {
  struct yg__event *next;
  int kind;
  long long id;
  int ok;
  const char *code;  /* literals only */
  const char *text;  /* literals only */
  char *line;
  size_t len;
} yg__event;

typedef struct yg__out {
  struct yg__out *next;
  long long gen;
  long long id; /* 0: a raw line with no answer to fail */
  char *line;
  size_t len;
} yg__out;

typedef struct yg__call {
  struct yg__call *next;
  long long id;
  char type[32];
  yg_callback cb;
  void *user;
  double deadline;
  char *held; /* the line, while it waits for hello */
  size_t held_len;
  int is_hello; /* 1: the connection's hello; 2, 3: the first and second re-ask while pending */
  long long hello_gen;
  int feedback_kind;
  int browser_wait;  /* the feedback page is opening: the answer waits for the opener */
  int browser_tried; /* the page was tried once: never again for this call */
} yg__call;

static struct {
  int inited, in_update, shutdown_wanted, transport, started_by_app;
  yg_config cfg;
  char slug[96], version[260], engine[33], site[300];
  /* the game's thread only */
  yg__call *calls; /* in id order */
  long long next_id, local_gen;
  int local_state, hello_sent, hello_done, ready, closed, quit_requested, name_shared, feedback_unsupported;
  double last_probe_at;
  /* Looking again on its own (YG__RELOOK_EVERY) after a look found no app channel or the app ended a
   * connection, until relook_until. */
  int relook;
  double relook_until;
  /* Who the player is (yg_player_status); on this connection: the game was told a player, how many
   * pending re-asks of hello went out, and when the first pending answer came. */
  int player_state, player_told, player_retries;
  double pending_at;
  /* yg_set_game_state: the game's latest state (0: never said), and the one sent on this
   * connection (0: none yet; every new hello starts again). */
  int game_state, game_state_sent;
  char *player_id, *player_name, *game_id, *hello_json;
  /* The longest line this connection's app takes: YG__LOCAL_LINE_OLD until its hello names a
   * larger `lineMax` (never above YG__LOCAL_LINE_MAX); back to it on every new look. */
  size_t local_line_max;
  /* under yg__mu */
  yg__event *in_head, *in_tail;
  size_t in_bytes, out_bytes; /* what the queues cost, allocations included */
  long in_dropped;
  yg__out *out_head, *out_tail;
  long long wanted_gen, epoch;
  int drop_wanted, stopping, io_dead;
  /* the thread */
  int thread_up;
#ifdef _WIN32
  HANDLE thread, wake;
#else
  pthread_t thread;
  int wake_r, wake_w;
#endif
} yg__g;

static void yg__log(int level, const char *fmt, const char *arg) {
  char msg[256];
  snprintf(msg, sizeof msg, fmt, arg ? arg : "");
  if (yg__g.cfg.log) yg__g.cfg.log(yg__g.cfg.user, level, msg);
  else fprintf(stderr, "%s\n", msg);
}

static void yg__wake(void) {
#ifdef _WIN32
  if (yg__g.wake) SetEvent(yg__g.wake);
#else
  char byte = 1;
  if (yg__g.wake_w >= 0) {
    while (write(yg__g.wake_w, &byte, 1) < 0 && errno == EINTR) {
    }
  }
#endif
}

#ifndef _WIN32
static void yg__drain_wake(void) {
  char bytes[64];
  if (yg__g.wake_r >= 0) {
    while (read(yg__g.wake_r, bytes, sizeof bytes) > 0) {
    }
  }
}
#endif

static size_t yg__event_cost(size_t len) { return len + 1 + YG__ALLOC_SLACK + sizeof(yg__event) + YG__ALLOC_SLACK; }

static size_t yg__out_cost(size_t len) { return len + 1 + YG__ALLOC_SLACK + sizeof(yg__out) + YG__ALLOC_SLACK; }

static size_t yg__outbox_max(void) {
#ifdef YOUGAME_TESTING
  if (yg__test_outbox_max) return yg__test_outbox_max;
#endif
  return YG__OUTBOX_MAX;
}

/* Under yg__mu. Takes line. */
static void yg__post_locked(int kind, long long id, int ok, const char *code, const char *text, char *line, size_t len) {
  yg__event *e;
  if (yg__g.stopping) {
    free(line);
    return;
  }
  /* Every queued line costs its event too, so a flood of short lines is capped like long ones. */
  if (line && yg__g.in_bytes + yg__event_cost(len) > YG__INBOX_MAX) {
    free(line);
    ++yg__g.in_dropped;
    return;
  }
  e = (yg__event *)calloc(1, sizeof *e);
  if (!e) {
    free(line);
    return;
  }
  e->kind = kind;
  e->id = id;
  e->ok = ok;
  e->code = code;
  e->text = text;
  e->line = line;
  e->len = len;
  if (line) yg__g.in_bytes += yg__event_cost(len);
  if (yg__g.in_tail) yg__g.in_tail->next = e;
  else yg__g.in_head = e;
  yg__g.in_tail = e;
}

static void yg__post(int kind, long long id, int ok, const char *code, const char *text) {
  yg__lock();
  yg__post_locked(kind, id, ok, code, text, NULL, 0);
  yg__unlock();
}

static void yg__post_line(void *ctx, char *line, size_t len) {
  char *copy;
  (void)ctx;
  if (len == 0) return; /* an empty line means nothing to anyone */
  copy = yg__strndup(line, len);
  if (!copy) return;
  yg__lock();
  yg__post_locked(YG__EV_LINE, 0, 0, NULL, NULL, copy, len);
  yg__unlock();
}

static const char yg__not_sent[] = "The request could not reach YouGame";

/* A line that will not go out: its call fails with noAnswer. Under yg__mu. */
static void yg__not_sent_locked(yg__out *o) {
  if (o->id > 0) yg__post_locked(YG__EV_REPLY, o->id, 0, "noAnswer", yg__not_sent, NULL, 0);
  free(o->line);
  free(o);
}

/* Takes the next queued line, or NULL. */
static yg__out *yg__take_out(void) {
  yg__out *o;
  yg__lock();
  o = yg__g.out_head;
  if (o) {
    yg__g.out_head = o->next;
    if (!yg__g.out_head) yg__g.out_tail = NULL;
    o->next = NULL;
    yg__g.out_bytes -= yg__out_cost(o->len);
  }
  yg__unlock();
  return o;
}

static int yg__stopping(void) {
  int s;
  yg__lock();
  s = yg__g.stopping;
  yg__unlock();
  return s;
}

/* The thread is ending: nothing queued now or later will go out. */
static void yg__io_dead(void) {
  yg__out *o, *next;
  yg__lock();
  yg__g.io_dead = 1;
  for (o = yg__g.out_head; o; o = next) {
    next = o->next;
    yg__not_sent_locked(o);
  }
  yg__g.out_head = yg__g.out_tail = NULL;
  yg__g.out_bytes = 0;
  yg__unlock();
}

/* A call's line that has not gone out yet stays home (its call was answered already). */
static void yg__unqueue(long long id) {
  yg__out **pp, *o;
  yg__lock();
  for (pp = &yg__g.out_head; *pp; pp = &(*pp)->next) {
    if ((*pp)->id != id) continue;
    o = *pp;
    *pp = o->next;
    if (yg__g.out_tail == o) {
      yg__out *t = yg__g.out_head;
      while (t && t->next) t = t->next;
      yg__g.out_tail = t;
    }
    yg__g.out_bytes -= yg__out_cost(o->len);
    free(o->line);
    free(o);
    break;
  }
  yg__unlock();
}

/* ---------------------------------------------------------------------------------------------
 * Stdio: requests on stdout, answers on stdin (sdk.md "Over stdin and stdout") */

/* The game's own buffered stdout is not flushed first: a flush could wait on the app as long as a
 * write can, and a line the game left without "\n" breaks the channel either way (README). */

static void yg__drop_out(yg__out *o) {
  yg__lock();
  yg__not_sent_locked(o);
  yg__unlock();
}

#ifdef _WIN32
/* A write the app does not read blocks here; yg_shutdown ends it with CancelSynchronousIo. */
static int yg__stdout_write(const char *d, size_t n) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD wrote;
  if (out == NULL || out == INVALID_HANDLE_VALUE) return 0;
  while (n > 0) {
    wrote = 0;
    if (yg__stopping()) return 0;
    if (!WriteFile(out, d, (DWORD)(n > 65536 ? 65536 : n), &wrote, NULL) || wrote == 0) return 0;
    d += wrote;
    n -= wrote;
  }
  return 1;
}

static unsigned __stdcall yg__stdio_main(void *arg) {
  yg__lines lines;
  yg__out *o;
  char *buf = (char *)malloc(YG__READ_CHUNK);
  int closed = 0;
  (void)arg;
  memset(&lines, 0, sizeof lines);
  lines.limit = YG__STDIO_LINE_MAX;
  if (!buf) closed = 1;
  while (!closed && !yg__stopping()) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD avail = 0, got = 0;
    while (!yg__stopping() && (o = yg__take_out()) != NULL) {
      if (!yg__stdout_write(o->line, o->len)) {
        yg__drop_out(o);
        continue;
      }
      free(o->line);
      free(o);
    }
    /* Peek first, so a stop never waits on ReadFile. Not a pipe, or the app's end closed: done.
     * An anonymous pipe cannot be waited on, so this looks again every 20 ms (or at once on a wake). */
    if (in == NULL || in == INVALID_HANDLE_VALUE || !PeekNamedPipe(in, NULL, 0, NULL, &avail, NULL)) {
      closed = 1;
      break;
    }
    if (avail == 0) {
      WaitForSingleObject(yg__g.wake, 20);
      continue;
    }
    if (!ReadFile(in, buf, avail < YG__READ_CHUNK ? avail : YG__READ_CHUNK, &got, NULL) || got == 0) {
      closed = 1;
      break;
    }
    yg__lines_feed(&lines, buf, got, yg__post_line, NULL);
  }
#else
/* One loop waits on stdin, stdout and the wake together, so an app that stops reading never blocks
 * the reading of its answers, an `embed quit`, or yg_shutdown. Writes after POLLOUT are at most
 * PIPE_BUF bytes, which a pipe takes without waiting. SIGPIPE is blocked on this thread only: a
 * closed stdout fails the write with EPIPE and the game's own signal handling is untouched. */
static void *yg__stdio_main(void *arg) {
  yg__lines lines;
  yg__out *cur = NULL;
  size_t off = 0, chunk;
  struct pollfd fds[3];
  nfds_t count, wake_at, out_at;
  sigset_t pipe_sig;
  int ready;
  ssize_t got, w;
  char *buf = (char *)malloc(YG__READ_CHUNK);
  int closed = 0;
  (void)arg;
  sigemptyset(&pipe_sig);
  sigaddset(&pipe_sig, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &pipe_sig, NULL);
  memset(&lines, 0, sizeof lines);
  lines.limit = YG__STDIO_LINE_MAX;
  if (!buf) closed = 1;
  while (!closed && !yg__stopping()) {
    if (!cur) {
      cur = yg__take_out();
      off = 0;
    }
    count = 0;
    fds[count].fd = STDIN_FILENO;
    fds[count].events = POLLIN;
    fds[count++].revents = 0;
    wake_at = out_at = 99;
    if (yg__g.wake_r >= 0) {
      wake_at = count;
      fds[count].fd = yg__g.wake_r;
      fds[count].events = POLLIN;
      fds[count++].revents = 0;
    }
    if (cur) {
      out_at = count;
      fds[count].fd = STDOUT_FILENO;
      fds[count].events = POLLOUT;
      fds[count++].revents = 0;
    }
    ready = poll(fds, count, wake_at != 99 ? YG__READ_WAIT_MS : 10);
    if (ready < 0 && errno == EINTR) continue;
    if (ready < 0) {
      closed = 1;
      break;
    }
    if (wake_at != 99 && fds[wake_at].revents) yg__drain_wake();
    if (out_at != 99 && fds[out_at].revents) {
      chunk = cur->len - off;
      if (chunk > PIPE_BUF) chunk = PIPE_BUF;
      w = write(STDOUT_FILENO, cur->line + off, chunk);
      if (w > 0) {
        off += (size_t)w;
        if (off == cur->len) {
          free(cur->line);
          free(cur);
          cur = NULL;
        }
      } else if (!(w < 0 && (errno == EINTR || errno == EAGAIN))) {
        yg__drop_out(cur); /* the app's end of stdout is gone */
        cur = NULL;
      }
    }
    if (!fds[0].revents) continue;
    got = read(STDIN_FILENO, buf, YG__READ_CHUNK);
    if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (got <= 0) {
      closed = 1; /* end of stdin */
      break;
    }
    yg__lines_feed(&lines, buf, (size_t)got, yg__post_line, NULL);
  }
  if (cur) yg__drop_out(cur); /* stopping: a line part-written is dropped (README) */
#endif
  if (lines.dropped) yg__post(YG__EV_LOG, 0, 0, NULL, "YouGame: dropped over-long lines from the app");
  yg__lines_free(&lines);
  free(buf);
  if (closed) {
    /* Calls in flight fail with noAnswer; the game keeps running (the app ends it when it means to). */
    yg__io_dead();
    yg__post(YG__EV_CLOSED, 0, 0, NULL, NULL);
  }
  return 0;
}

/* ---------------------------------------------------------------------------------------------
 * The local channel: a game the player started on their own (local-channel-contract.md) */

typedef struct yg__conn {
#ifdef _WIN32
  HANDLE pipe, rev, wev;
  OVERLAPPED ro, wo;
  int read_pending;
#else
  int fd;
#endif
  char *buf;
} yg__conn;

#ifdef _WIN32
/* The token's user SID, copied into a fresh allocation. */
static PSID yg__token_sid(HANDLE token) {
  DWORD size = 0, len;
  BYTE *buf;
  TOKEN_USER *user;
  PSID out = NULL;
  GetTokenInformation(token, TokenUser, NULL, 0, &size);
  if (size == 0 || size > 65536) return NULL;
  buf = (BYTE *)calloc(1, size);
  if (!buf) return NULL;
  if (GetTokenInformation(token, TokenUser, buf, size, &size)) {
    user = (TOKEN_USER *)buf;
    if (user->User.Sid && IsValidSid(user->User.Sid)) {
      len = GetLengthSid(user->User.Sid);
      out = (PSID)calloc(1, len);
      if (out && !CopySid(len, out, user->User.Sid)) {
        free(out);
        out = NULL;
      }
    }
  }
  free(buf);
  return out;
}

/* Medium integrity or higher (contract section 5). */
static int yg__token_medium(HANDLE token) {
  DWORD size = 0, rid;
  BYTE *buf;
  TOKEN_MANDATORY_LABEL *label;
  int ok = 0;
  GetTokenInformation(token, TokenIntegrityLevel, NULL, 0, &size);
  if (size == 0 || size > 65536) return 0;
  buf = (BYTE *)calloc(1, size);
  if (!buf) return 0;
  if (GetTokenInformation(token, TokenIntegrityLevel, buf, size, &size)) {
    label = (TOKEN_MANDATORY_LABEL *)buf;
    if (label->Label.Sid && IsValidSid(label->Label.Sid) && *GetSidSubAuthorityCount(label->Label.Sid) > 0) {
      rid = *GetSidSubAuthority(label->Label.Sid, (DWORD)(*GetSidSubAuthorityCount(label->Label.Sid) - 1));
      ok = rid >= SECURITY_MANDATORY_MEDIUM_RID;
    }
  }
  free(buf);
  return ok;
}

/* This process's user: the SID and its text. */
static PSID yg__our_sid(char *text, size_t text_size) {
  HANDLE token = NULL;
  PSID sid;
  LPWSTR wide = NULL;
  size_t i;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return NULL;
  sid = yg__token_sid(token);
  CloseHandle(token);
  if (!sid) return NULL;
  if (!ConvertSidToStringSidW(sid, &wide) || !wide) {
    free(sid);
    return NULL;
  }
  for (i = 0; wide[i] && i + 1 < text_size; ++i) text[i] = wide[i] < 0x80 ? (char)wide[i] : '?';
  text[i] = 0;
  LocalFree(wide);
  return sid;
}

/* Contract section 5: the pipe's server process runs as this user at medium integrity or higher,
 * and the pipe object itself is owned by this user. */
static int yg__pipe_is_ours(HANDLE pipe, PSID ours) {
  ULONG pid = 0;
  HANDLE process, token = NULL;
  PSID theirs, owner = NULL;
  PSECURITY_DESCRIPTOR sd = NULL;
  int ok = 0;
  if (!GetNamedPipeServerProcessId(pipe, &pid) || pid == 0) return 0;
  process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return 0;
  if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
    theirs = yg__token_sid(token);
    ok = theirs && EqualSid(theirs, ours) && yg__token_medium(token);
    free(theirs);
    CloseHandle(token);
  }
  CloseHandle(process);
  if (!ok) return 0;
  if (GetSecurityInfo(pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, &sd) !=
      ERROR_SUCCESS)
    return 0;
  ok = owner && IsValidSid(owner) && EqualSid(owner, ours);
  if (sd) LocalFree(sd);
  return ok;
}

static HANDLE yg__open_pipe(const wchar_t *name) {
  /* SECURITY_IDENTIFICATION: the host may learn who we are, never act as us. */
  return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
}

static int yg__conn_open(int staging, yg__conn *c) {
  char sid_text[200], name[300];
  wchar_t wname[300];
  size_t i;
  PSID ours = yg__our_sid(sid_text, sizeof sid_text);
  HANDLE pipe;
  if (!ours) return 0;
  if (!yg__pipe_name(staging, sid_text, name, sizeof name)) {
    free(ours);
    return 0;
  }
  for (i = 0; name[i]; ++i) wname[i] = (wchar_t)(unsigned char)name[i];
  wname[i] = 0;
  pipe = yg__open_pipe(wname);
  if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
    if (WaitNamedPipeW(wname, YG__PIPE_BUSY_WAIT_MS)) pipe = yg__open_pipe(wname);
  }
  if (pipe == INVALID_HANDLE_VALUE) { /* ERROR_FILE_NOT_FOUND: that YouGame is not open */
    free(ours);
    return 0;
  }
  if (!yg__pipe_is_ours(pipe, ours)) {
    yg__post(YG__EV_LOG, 0, 0, NULL, "YouGame: a local channel answered from a program that is not this user's; ignored");
    CloseHandle(pipe);
    free(ours);
    return 0;
  }
  free(ours);
  memset(c, 0, sizeof *c);
  c->rev = CreateEventW(NULL, TRUE, FALSE, NULL);
  c->wev = CreateEventW(NULL, TRUE, FALSE, NULL);
  c->buf = (char *)malloc(YG__READ_CHUNK);
  if (!c->rev || !c->wev || !c->buf) {
    if (c->rev) CloseHandle(c->rev);
    if (c->wev) CloseHandle(c->wev);
    free(c->buf);
    CloseHandle(pipe);
    return 0;
  }
  c->pipe = pipe;
  return 1;
}

static void yg__conn_close(yg__conn *c) {
  DWORD ignored;
  if (c->read_pending) { /* the read must be over before its buffer and OVERLAPPED go */
    CancelIoEx(c->pipe, &c->ro);
    GetOverlappedResult(c->pipe, &c->ro, &ignored, TRUE);
    c->read_pending = 0;
  }
  CloseHandle(c->pipe);
  CloseHandle(c->rev);
  CloseHandle(c->wev);
  free(c->buf);
}

/* >0 bytes, 0 nothing yet (or woken), -1 closed. */
static long yg__conn_read(yg__conn *c, const char **data, unsigned wait_ms) {
  HANDLE handles[2];
  DWORD wait, got = 0;
  if (!c->read_pending) {
    memset(&c->ro, 0, sizeof c->ro);
    ResetEvent(c->rev);
    c->ro.hEvent = c->rev;
    if (!ReadFile(c->pipe, c->buf, YG__READ_CHUNK, NULL, &c->ro) && GetLastError() != ERROR_IO_PENDING)
      return -1; /* ERROR_BROKEN_PIPE: the host closed */
    c->read_pending = 1;
  }
  /* The read first: when both are set the lowest index wins, and the wake stays set. */
  handles[0] = c->rev;
  handles[1] = yg__g.wake;
  wait = WaitForMultipleObjects(2, handles, FALSE, wait_ms);
  if (wait == WAIT_TIMEOUT || wait == WAIT_OBJECT_0 + 1) return 0;
  if (wait != WAIT_OBJECT_0) return -1;
  c->read_pending = 0;
  if (!GetOverlappedResult(c->pipe, &c->ro, &got, FALSE)) return -1;
  *data = c->buf;
  return (long)got;
}

static int yg__conn_write(yg__conn *c, const char *d, size_t n) {
  DWORD wrote, wait, ignored;
  while (n > 0) {
    memset(&c->wo, 0, sizeof c->wo);
    ResetEvent(c->wev);
    c->wo.hEvent = c->wev;
    if (!WriteFile(c->pipe, d, (DWORD)n, NULL, &c->wo) && GetLastError() != ERROR_IO_PENDING) return 0;
    for (;;) {
      wait = WaitForSingleObject(c->wev, 100);
      if (wait == WAIT_OBJECT_0) break;
      if (wait != WAIT_TIMEOUT || yg__stopping()) {
        CancelIoEx(c->pipe, &c->wo);
        GetOverlappedResult(c->pipe, &c->wo, &ignored, TRUE);
        return 0;
      }
    }
    wrote = 0;
    if (!GetOverlappedResult(c->pipe, &c->wo, &wrote, FALSE) || wrote == 0) return 0;
    d += wrote;
    n -= wrote;
  }
  return 1;
}

static void yg__wait_wake(unsigned ms) { WaitForSingleObject(yg__g.wake, ms); }

#else /* Mac and Linux: a unix socket in a user-only folder */

/* Contract section 5: a real folder (not a link), owned by this user, mode exactly 0700. */
static int yg__folder_is_ours(const char *path, uid_t uid) {
  char folder[256];
  const char *slash = strrchr(path, '/');
  struct stat st;
  size_t n;
  if (!slash) return 0;
  n = (size_t)(slash - path);
  if (n == 0 || n >= sizeof folder) return 0;
  memcpy(folder, path, n);
  folder[n] = 0;
  if (lstat(folder, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) && st.st_uid == uid && (st.st_mode & 0777) == 0700;
}

static int yg__peer_is_ours(int fd, uid_t uid) {
#if defined(__linux__)
  /* struct ucred's layout, spelled out so this does not depend on _GNU_SOURCE. */
  struct {
    pid_t pid;
    uid_t uid;
    gid_t gid;
  } cred;
  socklen_t len = sizeof cred;
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || len != sizeof cred) return 0;
  return cred.uid == uid;
#elif defined(__EMSCRIPTEN__)
  /* A browser build has no local socket to check: refuse, so it never talks to one. */
  (void)fd;
  (void)uid;
  return 0;
#else
  uid_t peer = 0;
  gid_t group = 0;
  if (getpeereid(fd, &peer, &group) != 0) return 0;
  return peer == uid;
#endif
}

static int yg__connect_path(const char *path, uid_t uid, yg__conn *c) {
  struct sockaddr_un addr;
  struct pollfd pf;
  int fd, err = 0, connected;
  socklen_t len = sizeof err;
  if (!yg__folder_is_ours(path, uid)) return 0; /* missing, or not ours: no host there */
  memset(&addr, 0, sizeof addr);
  if (strlen(path) >= sizeof addr.sun_path) return 0;
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);
#if defined(__linux__) && defined(SOCK_CLOEXEC)
  fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
  if (fd < 0) return 0;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
  {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
  }
#endif
  connected = connect(fd, (const struct sockaddr *)&addr, sizeof addr) == 0;
  if (!connected && errno == EINPROGRESS) {
    pf.fd = fd;
    pf.events = POLLOUT;
    pf.revents = 0;
    connected = poll(&pf, 1, YG__PIPE_BUSY_WAIT_MS) == 1 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
  }
  /* ENOENT / ECONNREFUSED: nobody listens. EAGAIN: the host is busy; the next look tries again. */
  if (!connected) {
    close(fd);
    return 0;
  }
  if (!yg__peer_is_ours(fd, uid)) {
    yg__post(YG__EV_LOG, 0, 0, NULL, "YouGame: a local channel answered from a program that is not this user's; ignored");
    close(fd);
    return 0;
  }
  c->fd = fd;
  c->buf = (char *)malloc(YG__READ_CHUNK);
  if (!c->buf) {
    close(fd);
    return 0;
  }
  return 1;
}

static int yg__conn_open(int staging, yg__conn *c) {
  char paths[3][256];
  uid_t uid = getuid();
  int count, i, is_linux = 0;
  const char *runtime = NULL;
#if defined(__linux__)
  is_linux = 1;
  runtime = getenv("XDG_RUNTIME_DIR");
#endif
  count = yg__socket_paths(is_linux, staging, (unsigned long)uid, runtime, paths);
  for (i = 0; i < count; ++i)
    if (yg__connect_path(paths[i], uid, c)) return 1;
  return 0;
}

static void yg__conn_close(yg__conn *c) {
  close(c->fd);
  free(c->buf);
}

static long yg__conn_read(yg__conn *c, const char **data, unsigned wait_ms) {
  struct pollfd fds[2];
  nfds_t count = 1;
  int ready;
  ssize_t got;
  fds[0].fd = c->fd;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  if (yg__g.wake_r >= 0) {
    fds[1].fd = yg__g.wake_r;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    count = 2;
  }
  ready = poll(fds, count, count == 2 ? (int)wait_ms : 10);
  if (ready == 0 || (ready < 0 && errno == EINTR)) return 0;
  if (ready < 0) return -1;
  if (count == 2 && fds[1].revents) yg__drain_wake();
  if (!fds[0].revents) return 0; /* only woken */
  got = recv(c->fd, c->buf, YG__READ_CHUNK, 0);
  if (got > 0) {
    *data = c->buf;
    return (long)got;
  }
  if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
  return -1; /* 0 bytes: the host closed */
}

static int yg__conn_write(yg__conn *c, const char *d, size_t n) {
  ssize_t w;
  struct pollfd pf;
#if defined(MSG_NOSIGNAL)
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0; /* SO_NOSIGPIPE is set on the socket */
#endif
  while (n > 0) {
    w = send(c->fd, d, n, flags);
    if (w > 0) {
      d += w;
      n -= (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR) continue;
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && !yg__stopping()) {
      pf.fd = c->fd;
      pf.events = POLLOUT;
      pf.revents = 0;
      poll(&pf, 1, 100);
      continue;
    }
    return 0;
  }
  return 1;
}

static void yg__wait_wake(unsigned ms) {
  struct pollfd pf;
  if (yg__g.wake_r < 0) {
    poll(NULL, 0, 10);
    return;
  }
  pf.fd = yg__g.wake_r;
  pf.events = POLLIN;
  pf.revents = 0;
  if (poll(&pf, 1, (int)ms) > 0) yg__drain_wake();
}
#endif

/* Lines queued for another look (or any, when keep < 0) fail with noAnswer. */
static void yg__fail_out(long long keep) {
  yg__out *o, *next, *head = NULL, *tail = NULL;
  yg__lock();
  for (o = yg__g.out_head; o; o = next) {
    next = o->next;
    o->next = NULL;
    if (keep >= 0 && o->gen == keep) {
      if (tail) tail->next = o;
      else head = o;
      tail = o;
    } else {
      yg__g.out_bytes -= yg__out_cost(o->len);
      yg__not_sent_locked(o);
    }
  }
  yg__g.out_head = head;
  yg__g.out_tail = tail;
  yg__unlock();
}

static void yg__local_serve(yg__conn *c, long long gen) {
  yg__lines lines;
  yg__out *o;
  const char *data = NULL;
  long got;
  int broken = 0, go;
  memset(&lines, 0, sizeof lines);
  lines.limit = YG__LOCAL_READ_MAX;
  for (;;) {
    yg__lock();
    go = !yg__g.stopping && !yg__g.drop_wanted && yg__g.wanted_gen == gen;
    yg__unlock();
    if (!go) break;
    while ((o = yg__take_out()) != NULL) {
      if (o->gen != gen || !yg__conn_write(c, o->line, o->len)) {
        broken = o->gen == gen;
        yg__lock();
        yg__not_sent_locked(o);
        yg__unlock();
        if (broken) break;
        continue;
      }
      free(o->line);
      free(o);
    }
    if (broken) break;
    got = yg__conn_read(c, &data, YG__READ_WAIT_MS);
    if (got < 0) break;
    if (got > 0) yg__lines_feed(&lines, data, (size_t)got, yg__post_line, NULL);
  }
  if (lines.dropped) yg__post(YG__EV_LOG, 0, 0, NULL, "YouGame: dropped over-long lines from the local channel");
  yg__lines_free(&lines);
}

#ifdef _WIN32
static unsigned __stdcall yg__local_main(void *arg)
#else
static void *yg__local_main(void *arg)
#endif
{
  long long handled = 0, wanted;
  int stop, pass, said_none = 0;
  yg__conn c;
  (void)arg;
  for (;;) {
    yg__lock();
    stop = yg__g.stopping;
    wanted = yg__g.wanted_gen;
    if (wanted != handled) yg__g.drop_wanted = 0;
    yg__unlock();
    if (stop) break;
    if (wanted == handled) {
      yg__fail_out(-1);
      yg__wait_wake(YG__READ_WAIT_MS);
      continue;
    }
    handled = wanted;
    memset(&c, 0, sizeof c);
    /* Only the app of this build's YouGame (config.staging; contract section 1): a production game
     * never talks to the staging app, nor a staging game to the production one. */
    pass = yg__g.cfg.staging ? 1 : 0;
    if (yg__conn_open(pass, &c)) {
      yg__post(YG__EV_LOG, 0, 0, NULL,
               pass ? "YouGame: found the YouGame app's local channel (staging)"
                    : "YouGame: found the YouGame app's local channel (production)");
    } else {
      pass = 2;
      /* Said once per run: which app it looked for, so a production build next to only the staging app
       * (or the reverse) is plain in the log. */
      if (!said_none) {
        said_none = 1;
        yg__post(YG__EV_LOG, 0, 0, NULL,
                 yg__g.cfg.staging ? "YouGame: no YouGame Staging app open (this build is set to staging)"
                                   : "YouGame: no YouGame app open (this build is set to production)");
      }
    }
    if (yg__stopping()) {
      if (pass < 2) yg__conn_close(&c);
      break;
    }
    if (pass == 2) {
      yg__post(YG__EV_DOWN, wanted, 1, NULL, NULL); /* no app channel at all */
      continue;
    }
    yg__post(YG__EV_UP, wanted, 0, NULL, NULL);
    yg__local_serve(&c, wanted);
    yg__conn_close(&c);
    yg__post(YG__EV_DOWN, wanted, 0, NULL, NULL);
  }
  yg__io_dead();
  return 0;
}

/* ---------------------------------------------------------------------------------------------
 * The feedback page in the player's browser, on its own short thread (the game never waits) */

typedef struct yg__open_job {
  long long id, epoch;
  char url[400];
} yg__open_job;

static void yg__open_done(yg__open_job *job, int ok) {
  yg__lock();
  if (yg__g.inited && yg__g.epoch == job->epoch) yg__post_locked(YG__EV_BROWSER, job->id, ok, NULL, NULL, NULL, 0);
  yg__unlock();
}

#ifdef _WIN32
static unsigned __stdcall yg__open_main(void *arg) {
  yg__open_job *job = (yg__open_job *)arg;
  wchar_t wurl[400];
  size_t i;
  int ok;
  HRESULT co;
  for (i = 0; job->url[i] && i + 1 < sizeof wurl / sizeof wurl[0]; ++i) wurl[i] = (wchar_t)(unsigned char)job->url[i];
  wurl[i] = 0;
#ifdef YOUGAME_TESTING
  if (yg__test_open_url) {
    yg__open_done(job, yg__test_open_url(job->url));
    free(job);
    return 0;
  }
#endif
  co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ok = (INT_PTR)ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL) > 32;
  if (SUCCEEDED(co)) CoUninitialize();
  yg__open_done(job, ok);
  free(job);
  return 0;
}
#else
static void *yg__open_main(void *arg) {
  yg__open_job *job = (yg__open_job *)arg;
  posix_spawn_file_actions_t fa;
  pid_t pid = 0;
  int rc = -1, status;
  /* By absolute path only: a game's PATH (or its working folder) never picks the program. */
#ifdef __APPLE__
  static const char *const progs[] = {"/usr/bin/open", NULL};
#else
  static const char *const progs[] = {"/usr/bin/xdg-open", "/bin/xdg-open", NULL};
#endif
  const char *prog = NULL;
  char *argv[3];
  size_t i;
#ifdef YOUGAME_TESTING
  if (yg__test_open_url) {
    yg__open_done(job, yg__test_open_url(job->url));
    free(job);
    return NULL;
  }
#endif
  for (i = 0; progs[i] && !prog; ++i)
    if (access(progs[i], X_OK) == 0) prog = progs[i];
  if (!prog) { /* no opener installed: the page did not open */
    yg__open_done(job, 0);
    free(job);
    return NULL;
  }
  argv[0] = (char *)prog;
  argv[1] = job->url;
  argv[2] = NULL;
  /* The opener gets nothing of ours: its stdin, stdout and stderr are /dev/null. */
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  rc = posix_spawn(&pid, prog, &fa, NULL, argv, YG__ENVIRON);
  posix_spawn_file_actions_destroy(&fa);
  /* Started is opened: xdg-open may stay until the browser closes, so its exit is not awaited here. */
  yg__open_done(job, rc == 0);
  free(job);
  if (rc == 0) {
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  }
  return NULL;
}
#endif

/* 0: no page to open (then the result stays NotAvailable). */
static int yg__open_page(long long id, int kind) {
  yg__open_job *job = (yg__open_job *)calloc(1, sizeof *job);
  if (!job) return 0;
  if (yg_feedback_page_url(yg__g.site, yg__g.slug, kind, job->url, sizeof job->url) < 0) {
    free(job);
    yg__log(YG_LOG_WARN, "YouGame: no feedback page to open; set a valid game slug (and feedback site)%s", NULL);
    return 0;
  }
  job->id = id;
  job->epoch = yg__g.epoch;
  {
#ifdef _WIN32
    HANDLE t = (HANDLE)_beginthreadex(NULL, 0, yg__open_main, job, 0, NULL);
    if (!t) {
      free(job);
      return 0;
    }
    CloseHandle(t);
#else
    pthread_t t;
    pthread_attr_t attr;
    int rc;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(&t, &attr, yg__open_main, job);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
      free(job);
      return 0;
    }
#endif
  }
  return 1;
}

/* ---------------------------------------------------------------------------------------------
 * Answers into yg_result */

typedef struct yg__reply {
  int ok;
  yg__span result; /* checked JSON; "null" when none */
  const char *error;
  const char *code;
  int retry_after; /* the answer's "retryAfter" in whole seconds, or 0 */
} yg__reply;

typedef struct yg__pool {
  void **items;
  size_t n, cap;
} yg__pool;

static void *yg__pool_add(yg__pool *pool, void *p) {
  void **items;
  if (!p) return NULL;
  if (pool->n == pool->cap) {
    size_t cap = pool->cap ? pool->cap * 2 : 16;
    items = (void **)realloc(pool->items, cap * sizeof *items);
    if (!items) {
      free(p);
      return NULL;
    }
    pool->items = items;
    pool->cap = cap;
  }
  pool->items[pool->n++] = p;
  return p;
}

static void yg__pool_free(yg__pool *pool) {
  size_t i;
  for (i = 0; i < pool->n; ++i) free(pool->items[i]);
  free(pool->items);
  pool->items = NULL;
  pool->n = pool->cap = 0;
}

static const char *yg__pool_copy(yg__pool *pool, const char *s, size_t n) {
  char *p = (char *)yg__pool_add(pool, yg__strndup(s, n));
  return p ? p : "";
}

static const char *yg__pool_text(yg__pool *pool, yg__span str) {
  yg__buf b = {NULL, 0, 0, 0};
  yg__str_decode(&b, str);
  if (b.failed) {
    yg__buf_free(&b);
    return "";
  }
  return yg__pool_add(pool, b.p) ? b.p : "";
}

static const char *yg__f_str(yg__pool *pool, yg__span obj, const char *key) {
  yg__span v;
  return yg__get(obj, key, &v) && yg__is_str(v) ? yg__pool_text(pool, v) : "";
}

static int yg__f_bool(yg__span obj, const char *key, int def) {
  yg__span v;
  if (!yg__get(obj, key, &v)) return def;
  if (yg__kind(v) == 't') return 1;
  if (yg__kind(v) == 'f') return 0;
  return def;
}

static int yg__f_num(yg__span obj, const char *key, double *out) {
  yg__span v;
  return yg__get(obj, key, &v) && yg__num(v, out);
}

static long long yg__f_int(yg__span obj, const char *key, long long def) {
  double d;
  if (!yg__f_num(obj, key, &d) || d != d) return def;
  /* Clamped first: converting a double outside long long's range is undefined. */
  if (d > 9.0e18) d = 9.0e18;
  if (d < -9.0e18) d = -9.0e18;
  return (long long)d;
}

static int yg__f_int32(yg__span obj, const char *key, int def) {
  long long v = yg__f_int(obj, key, def);
  return v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : (int)v;
}

static void yg__entry(yg__pool *pool, yg__span o, yg_entry *e) {
  e->score_id = yg__f_str(pool, o, "id");
  e->rank = yg__f_int32(o, "rank", 0);
  e->name = yg__f_str(pool, o, "name");
  e->score = yg__f_int(o, "score", 0);
  e->player_id = yg__f_str(pool, o, "player_id");
  e->at = yg__f_str(pool, o, "at");
  e->unverified = yg__f_bool(o, "unverified", 0);
}

static void yg__blank_entry(yg_entry *e) {
  memset(e, 0, sizeof *e);
  e->score_id = e->name = e->player_id = e->at = "";
}

static int yg__reason_from(yg__pool *pool, yg__span answer) {
  const char *r = yg__f_str(pool, answer, "reason");
  if (!strcmp(r, "alreadyOpen")) return YG_REASON_ALREADY_OPEN;
  if (!strcmp(r, "tooSoon")) return YG_REASON_TOO_SOON;
  if (!strcmp(r, "noActivation")) return YG_REASON_NO_ACTIVATION;
  if (!strcmp(r, "notHere")) return YG_REASON_NOT_HERE;
  if (!strcmp(r, "unsupported")) return YG_REASON_UNSUPPORTED;
  if (!strcmp(r, "noAnswer")) return YG_REASON_NO_ANSWER;
  if (!*r && yg__f_bool(answer, "alreadyOpen", 0)) return YG_REASON_ALREADY_OPEN; /* before reasons */
  return YG_REASON_UNKNOWN;
}

static int yg__contains_nocase(const char *hay, const char *needle) {
  size_t n = strlen(needle), i, j;
  for (i = 0; hay[i]; ++i) {
    for (j = 0; j < n; ++j) {
      char a = hay[i + j], b = needle[j];
      if (!a) return 0;
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
    }
    if (j == n) return 1;
  }
  return 0;
}

/* An openFeedback answer as the button's outcome. */
static void yg__map_feedback(yg__pool *pool, const yg__reply *r, yg_result *out) {
  double ms;
  char msg[64];
  out->feedback.player_message = "";
  if (!r->ok) {
    if (!strcmp(r->code, "notAvailable")) out->feedback.reason = YG_REASON_NOT_AVAILABLE;
    else if (yg__contains_nocase(r->error, "Unknown request")) out->feedback.reason = YG_REASON_UNSUPPORTED;
    else if (!strcmp(r->code, "noAnswer") || !strcmp(r->code, "timeout") || !strcmp(r->code, "busy"))
      out->feedback.reason = YG_REASON_NO_ANSWER;
    else out->feedback.reason = YG_REASON_UNKNOWN;
  } else if (yg__is_obj(r->result) && yg__f_bool(r->result, "opened", 0)) {
    out->feedback.opened = 1;
    out->feedback.reason = YG_REASON_NONE;
  } else {
    out->feedback.reason = yg__is_obj(r->result) ? yg__reason_from(pool, r->result) : YG_REASON_UNKNOWN;
    if (yg__is_obj(r->result) && yg__f_num(r->result, "retryInMs", &ms) && ms > 0) {
      ms = ms / 1000.0;
      if (ms > 86400.0) ms = 86400.0;
      out->feedback.retry_in_seconds = (int)ms + ((double)(int)ms < ms ? 1 : 0);
    }
  }
  if (out->feedback.reason == YG_REASON_TOO_SOON && out->feedback.retry_in_seconds > 0) {
    snprintf(msg, sizeof msg, "Feedback opens again in %d s", out->feedback.retry_in_seconds);
    out->feedback.player_message = yg__pool_copy(pool, msg, strlen(msg));
  } else if (out->feedback.reason == YG_REASON_NOT_HERE) {
    out->feedback.player_message = "Open YouGame to send feedback";
  }
}

static void yg__fill(yg__pool *pool, const yg__call *c, const yg__reply *r, int browser, yg_result *out) {
  yg__span a = r->result, v, x;
  yg__iter it;
  yg_entry *entries;
  long n;
  int answer = r->ok && yg__is_obj(a);
  memset(out, 0, sizeof *out);
  out->id = c->id;
  out->type = c->type;
  out->ok = r->ok;
  out->code = r->code;
  out->error = r->ok ? "" : r->error;
  out->retry_after_seconds = r->ok ? 0 : r->retry_after;
  out->result_json = r->ok ? yg__pool_copy(pool, r->result.p, r->result.n) : "";
  out->player.id = out->player.name = "";
  out->score.score_id = out->score.share_url = out->score.status = "";
  out->score.previous_best = -1;
  out->board.entries = NULL;
  yg__blank_entry(&out->board.me);
  out->save.updated_at = out->save.data_json = out->save.build = "";
  out->feedback.player_message = "";

  if (!strcmp(c->type, "requestName") || !strcmp(c->type, "setName")) {
    int request = c->type[0] == 'r';
    /* setName's answer has no `shared`: what hello said stands. */
    out->player.shared = request ? 0 : yg__g.name_shared;
    if (answer) {
      out->player.shared = yg__f_bool(a, "shared", out->player.shared);
      if (yg__get(a, "player", &v) && yg__is_obj(v)) {
        out->player.id = yg__f_str(pool, v, "id");
        out->player.name = yg__f_str(pool, v, "name");
      }
    }
    /* As sdk.js requestName: a name the player keeps from games is "Player". */
    if (request && r->ok && !out->player.shared) out->player.name = "Player";
  } else if (!strcmp(c->type, "submitScore")) {
    if (answer) {
      out->score.score_id = yg__f_str(pool, a, "score_id");
      out->score.rank = yg__f_int32(a, "rank", 0);
      out->score.total = yg__f_int32(a, "total", 0);
      out->score.best = yg__f_int(a, "best", 0);
      out->score.previous_best = yg__f_int(a, "previous_best", -1);
      out->score.is_personal_best = yg__f_bool(a, "is_personal_best", 0);
      out->score.share_url = yg__f_str(pool, a, "share_url");
      out->score.status = yg__f_str(pool, a, "status");
    }
  } else if (!strcmp(c->type, "leaderboard")) {
    if (answer) {
      if (yg__get(a, "entries", &v) && yg__kind(v) == '[' && (n = yg__count(v)) > 0) {
        if (n > YG__BOARD_ROWS_MAX) n = YG__BOARD_ROWS_MAX; /* the site sends at most 100 */
        entries = (yg_entry *)yg__pool_add(pool, calloc((size_t)n, sizeof *entries));
        if (entries && yg__iter_begin(&it, v)) {
          while (out->board.count < n && yg__iter_next(&it, NULL, &x))
            if (yg__is_obj(x)) yg__entry(pool, x, &entries[out->board.count++]);
          out->board.entries = entries;
        }
      }
      out->board.total = yg__f_int32(a, "total", out->board.count);
      if (yg__get(a, "me", &v) && yg__is_obj(v)) {
        out->board.has_me = 1;
        yg__entry(pool, v, &out->board.me);
      }
    }
  } else if (!strcmp(c->type, "saveWrite") || !strcmp(c->type, "saveClear")) {
    if (answer) {
      out->save.cloud = yg__f_bool(a, "cloud", 0);
      out->save.updated_at = yg__f_str(pool, a, "updated_at");
    }
  } else if (!strcmp(c->type, "saveLoad")) {
    if (answer) {
      out->save.cloud = yg__f_bool(a, "cloud", 0);
      if (yg__get(a, "save", &v) && yg__is_obj(v)) {
        if (yg__get(v, "data", &x) && yg__kind(x) != 'n') {
          out->save.has_data = 1;
          out->save.data_json = yg__pool_copy(pool, x.p, x.n);
        }
        out->save.updated_at = yg__f_str(pool, v, "updated_at");
        out->save.build = yg__f_str(pool, v, "build");
      }
    }
  } else if (!strcmp(c->type, "openFeedback")) {
    yg__map_feedback(pool, r, out);
    if (browser) {
      out->feedback.opened = 1;
      out->feedback.reason = YG_REASON_NONE;
      out->feedback.in_browser = 1;
      out->feedback.retry_in_seconds = 0;
      out->feedback.player_message = "";
    }
  } else if (!strcmp(c->type, "openMenu") || !strcmp(c->type, "menuButton")) {
    out->menu_opened = answer && yg__f_bool(a, "opened", 0);
  }
}

/* ---------------------------------------------------------------------------------------------
 * Calls */

static void yg__set_str(char **slot, const char *s) {
  char *copy = yg__strndup(s, strlen(s));
  if (!copy) return;
  free(*slot);
  *slot = copy;
}

static yg__call *yg__find(long long id) {
  yg__call *c;
  for (c = yg__g.calls; c; c = c->next)
    if (c->id == id) return c;
  return NULL;
}

static void yg__unlink(yg__call *call) {
  yg__call **pp;
  for (pp = &yg__g.calls; *pp; pp = &(*pp)->next) {
    if (*pp == call) {
      *pp = call->next;
      call->next = NULL;
      return;
    }
  }
}

static void yg__free_call(yg__call *c) {
  free(c->held);
  free(c);
}

/* A reply made here (a refusal, a line that could not go): it arrives in the next yg_update. */
static void yg__post_reply(long long id, const char *code, const char *error) {
  yg__post(YG__EV_REPLY, id, 0, code, error);
}

static const char yg__queue_full[] = "Too many requests are waiting to go to YouGame";

/* Queues a line for the thread. 0 queued; -1 refused (a call's answer then says why). Takes line.
 * The queue holds at most YG__OUTBOX_MAX bytes (an app that stops reading cannot grow it without
 * end); one line always fits in an empty queue. Over it, the call answers code "busy". */
static int yg__enqueue(long long id, long long gen, char *line, size_t len) {
  yg__out *o = (yg__out *)calloc(1, sizeof *o);
  size_t cost = yg__out_cost(len);
  yg__lock();
  if (!o || yg__g.io_dead || (yg__g.out_head && yg__g.out_bytes + cost > yg__outbox_max())) {
    if (id > 0) {
      if (o && !yg__g.io_dead) yg__post_locked(YG__EV_REPLY, id, 0, "busy", yg__queue_full, NULL, 0);
      else yg__post_locked(YG__EV_REPLY, id, 0, "noAnswer", yg__not_sent, NULL, 0);
    }
    free(line);
    free(o);
    yg__unlock();
    return -1;
  }
  o->gen = gen;
  o->id = id;
  o->line = line;
  o->len = len;
  yg__g.out_bytes += cost;
  if (yg__g.out_tail) yg__g.out_tail->next = o;
  else yg__g.out_head = o;
  yg__g.out_tail = o;
  yg__unlock();
  yg__wake(); /* after the enqueue: the thread sees the line on the wake, or on its next pass */
  return 0;
}

/* Hands one request line to the thread. Takes line. */
static void yg__send(long long id, char *line, size_t len) {
  size_t line_max = yg__g.local_line_max ? yg__g.local_line_max : YG__LOCAL_LINE_OLD;
  if (yg__g.transport == YG_TRANSPORT_LOCAL && len > line_max) {
    /* The host ends a connection that sends a longer line, so it is never sent. */
    free(line);
    yg__post_reply(id, "tooLong", line_max > YG__LOCAL_LINE_OLD
                                      ? "The request is longer than YouGame's local channel takes (200 KB)"
                                      : "The request is longer than this YouGame app's local channel takes (64 KB): update YouGame");
    return;
  }
  if (len > YG__STDIO_LINE_MAX) {
    free(line);
    yg__post_reply(id, "noAnswer", yg__not_sent); /* the app would drop it unread */
    return;
  }
  yg__enqueue(id, yg__g.transport == YG_TRANSPORT_LOCAL ? yg__g.local_gen : 0, line, len);
}

static void yg__start_probe(void) {
  ++yg__g.local_gen;
  yg__g.last_probe_at = yg__now();
  yg__g.local_state = YG__PROBING;
  yg__g.hello_sent = yg__g.hello_done = 0;
  yg__g.local_line_max = YG__LOCAL_LINE_OLD;
  if (!yg__g.thread_up) {
    yg__post(YG__EV_DOWN, yg__g.local_gen, 0, NULL, NULL);
    return;
  }
  yg__lock();
  yg__g.wanted_gen = yg__g.local_gen;
  yg__unlock();
  yg__wake();
}

static yg__call *yg__new_call(const char *type, yg_callback cb, void *user, double timeout) {
  yg__call *c = (yg__call *)calloc(1, sizeof *c), **pp;
  if (!c) return NULL;
  c->id = yg__g.next_id++;
  strncpy(c->type, type, sizeof c->type - 1);
  c->cb = cb;
  c->user = user;
  c->deadline = yg__now() + (timeout < 0.1 ? 0.1 : timeout);
  for (pp = &yg__g.calls; *pp; pp = &(*pp)->next) {
  }
  *pp = c;
  return c;
}

/* `yg {"id":N,"type":T` + body members (minus id and type) + `}\n`. body: a checked object or empty. */
static char *yg__encode(long long id, const char *type, yg__span body, const yg__buf *extra, size_t *len) {
  yg__buf b = {NULL, 0, 0, 0};
  yg__iter it;
  yg__span k, v;
  yg__buf_puts(&b, "yg {\"id\":");
  yg__buf_int(&b, id);
  yg__buf_puts(&b, ",\"type\":");
  yg__json_str(&b, type, strlen(type));
  if (body.n && yg__iter_begin(&it, body)) {
    while (yg__iter_next(&it, &k, &v)) {
      if (yg__str_equals(k, "id", 2) || yg__str_equals(k, "type", 4)) continue;
      yg__buf_putc(&b, ',');
      yg__emit(&b, k);
      yg__buf_putc(&b, ':');
      yg__emit(&b, v);
    }
  }
  if (extra && extra->len) yg__buf_put(&b, extra->p, extra->len);
  yg__buf_puts(&b, "}\n");
  if (b.failed) {
    yg__buf_free(&b);
    return NULL;
  }
  *len = b.len;
  return b.p;
}

/* One call: refuse, look for the app, hold until hello, or send. */
static long long yg__call_start(const char *type, yg__span body, const yg__buf *extra, double timeout, yg_callback cb,
                                void *user, int is_hello) {
  yg__call *c;
  char *line;
  size_t len = 0;
  if (!yg__g.inited) return 0;
  c = yg__new_call(type, cb, user, timeout);
  if (!c) return 0;
  c->is_hello = is_hello;
  c->hello_gen = yg__g.local_gen;
  if (yg__g.transport == YG_TRANSPORT_NONE) {
    yg__post_reply(c->id, "notAvailable", "not available");
    return c->id;
  }
  if (yg__g.closed) {
    yg__post_reply(c->id, "noAnswer", "YouGame closed the channel");
    return c->id;
  }
  if (yg__g.transport == YG_TRANSPORT_LOCAL && yg__g.local_state == YG__IDLE) {
    /* Started on its own and not connected: look again now (the call waits for that look), unless
     * the last look was under 5 s ago (contract section 1.3). */
    double cooldown = YG__PROBE_COOLDOWN;
#ifdef YOUGAME_TESTING
    if (yg__test_probe_cooldown >= 0) cooldown = yg__test_probe_cooldown;
#endif
    if (yg__now() - yg__g.last_probe_at >= cooldown) {
      yg__start_probe();
      c->hello_gen = yg__g.local_gen;
    } else {
      yg__post_reply(c->id, "notAvailable", "not available");
      return c->id;
    }
  }
  line = yg__encode(c->id, type, body, extra, &len);
  if (!line) {
    yg__post_reply(c->id, "", "Out of memory");
    return c->id;
  }
  if (!yg__g.hello_done && !is_hello) {
    c->held = line; /* YouGame.ready() in the web SDK: calls wait for hello */
    c->held_len = len;
    return c->id;
  }
  yg__send(c->id, line, len);
  return c->id;
}

/* A call answered here without the app (bad arguments), in the next yg_update like any answer. */
static long long yg__refuse(const char *type, const char *error, yg_callback cb, void *user) {
  yg__call *c;
  if (!yg__g.inited) return 0;
  c = yg__new_call(type, cb, user, YG__TIMEOUT);
  if (!c) return 0;
  yg__post_reply(c->id, "", error);
  return c->id;
}

static void yg__send_hello(void);
static void yg__complete(yg__call *c, const yg__reply *r, int browser);
static int yg__game_state_flush(void);

static void yg__flush_held(void) {
  yg__call *c;
  char *line;
  for (c = yg__g.calls; c; c = c->next) {
    if (!c->held) continue;
    line = c->held;
    c->held = NULL;
    yg__send(c->id, line, c->held_len);
  }
}

/* The calls held for hello, or every call (all), fail with r. Callbacks may add calls: those stay. */
static void yg__fail(const yg__reply *r, int all) {
  long long *ids;
  size_t n = 0, cap = 0, i;
  yg__call *c;
  for (c = yg__g.calls; c; c = c->next) ++cap;
  if (!cap) return;
  ids = (long long *)malloc(cap * sizeof *ids);
  if (!ids) return;
  for (c = yg__g.calls; c; c = c->next) {
    /* A call whose feedback page is opening is answered by the opener (or its deadline). */
    if ((all && !c->browser_wait) || c->held) {
      ids[n++] = c->id;
      free(c->held); /* answered now: its line never goes */
      c->held = NULL;
    }
  }
  for (i = 0; i < n && !yg__g.shutdown_wanted; ++i) {
    c = yg__find(ids[i]);
    if (c) yg__complete(c, r, 0);
  }
  free(ids);
}

static void yg__fail_with(const char *code, const char *error, int all) {
  yg__reply r;
  memset(&r, 0, sizeof r);
  r.result = yg__null_span;
  r.code = code;
  r.error = error;
  yg__fail(&r, all);
}

/* A hello result (hello's answer, a re-ask's, or the playerArrived push's): the state takes it whole.
 * Returns 1 when it names a player. */
static int yg__take_hello(yg__span a) {
  yg__span v, p, s;
  yg__pool pool = {NULL, 0, 0};
  char *json = yg__strndup(a.p, a.n);
  int player = 0;
  if (json) {
    free(yg__g.hello_json);
    yg__g.hello_json = json;
  }
  if (yg__get(a, "game", &v) && yg__is_obj(v) && yg__get(v, "id", &v) && yg__is_str(v))
    yg__set_str(&yg__g.game_id, yg__pool_text(&pool, v));
  if (yg__get(a, "player", &p) && yg__is_obj(p)) {
    /* A player is one with an id or a name ("" strings are 2 bytes with their quotes). */
    if (yg__get(p, "id", &s) && yg__is_str(s)) {
      yg__set_str(&yg__g.player_id, yg__pool_text(&pool, s));
      player |= s.n > 2;
    }
    if (yg__get(p, "name", &s) && yg__is_str(s)) {
      yg__set_str(&yg__g.player_name, yg__pool_text(&pool, s));
      player |= s.n > 2;
    }
  }
  if (yg__get(a, "nameShared", &v) && (yg__kind(v) == 't' || yg__kind(v) == 'f'))
    yg__g.name_shared = yg__kind(v) == 't';
  {
    /* An app that takes longer lines says so (desktop/src/local-channel.ts sendHello `lineMax`). */
    double line_max;
    if (yg__get(a, "lineMax", &v) && yg__num(v, &line_max) && line_max >= (double)YG__LOCAL_LINE_OLD)
      yg__g.local_line_max = line_max >= (double)YG__LOCAL_LINE_MAX ? YG__LOCAL_LINE_MAX : (size_t)line_max;
  }
  yg__pool_free(&pool);
  return player;
}

/* The player is here after a pending hello: once per connection, the game hears it (on_player). */
static void yg__player_arrived(yg__span result, const char *how) {
  if (yg__g.player_told) {
    yg__take_hello(result); /* a re-ask that crossed the push: the same player, told once */
    return;
  }
  if (!yg__take_hello(result)) return;
  yg__g.player_told = 1;
  yg__g.player_state = YG_PLAYER_KNOWN;
  yg__g.player_retries = 2; /* nothing more to ask */
  yg__log(YG_LOG_INFO, "YouGame: the player arrived (%s)", how);
  if (yg__g.cfg.on_player) yg__g.cfg.on_player(yg__g.cfg.user);
}

/* A re-ask of hello while the player was pending (is_hello 2 or 3) answered. */
static void yg__handle_retry(const yg__reply *r, long long gen, int last) {
  yg__span a = r->result, v;
  if (gen != yg__g.local_gen || yg__g.local_state != YG__CONNECTED) return; /* an older connection's */
  if (r->ok && yg__is_obj(a)) {
    if (yg__get(a, "player", &v) && yg__is_obj(v)) {
      yg__player_arrived(a, "hello asked again");
      if (yg__g.player_told) return;
    }
  }
  /* No player yet: as the app's comment on playerPending says, whatever the first re-ask answers,
   * the second follows; only a player, the push, or the second re-ask ends the wait. */
  if (!last || yg__g.player_told || yg__g.player_state != YG_PLAYER_PENDING) return;
  yg__g.player_retries = 2;
  yg__g.player_state = YG_PLAYER_NONE;
  yg__log(YG_LOG_INFO, "YouGame: no player came after hello; playing without one%s", NULL);
}

static void yg__send_hello_line(int is_hello);

/* Once a frame: a pending player's re-asks of hello, when due (contract section 4). */
static void yg__player_tick(void) {
  double due;
  if (yg__g.transport != YG_TRANSPORT_LOCAL || yg__g.local_state != YG__CONNECTED ||
      yg__g.player_state != YG_PLAYER_PENDING || yg__g.player_retries >= 2)
    return;
  due = yg__g.player_retries == 0 ? YG__PLAYER_RETRY_1 : YG__PLAYER_RETRY_2;
#ifdef YOUGAME_TESTING
  if (yg__test_player_retry[yg__g.player_retries] > 0) due = yg__test_player_retry[yg__g.player_retries];
#endif
  if (yg__now() < yg__g.pending_at + due) return;
  ++yg__g.player_retries;
  yg__send_hello_line(1 + yg__g.player_retries);
}

static void yg__handle_hello(const yg__reply *r, long long gen) {
  yg__span a = r->result, v;
  if (yg__g.transport == YG_TRANSPORT_LOCAL) {
    if (gen != yg__g.local_gen || yg__g.local_state != YG__PROBING) return; /* an older look's hello */
    if (!r->ok) {
      /* No YouGame that answers this game: as if none were open. */
      yg__log(YG_LOG_INFO, "YouGame: the local channel did not answer hello (%s); running without YouGame",
              *r->code ? r->code : "no code");
      yg__g.local_state = YG__IDLE;
      yg__g.relook = 0; /* an app that refuses this game: no more looks on its own */
      yg__g.hello_sent = yg__g.hello_done = 0;
      yg__lock();
      yg__g.drop_wanted = 1;
      yg__unlock();
      yg__wake();
      yg__fail_with("notAvailable", "not available", 0);
      return;
    }
    yg__g.local_state = YG__CONNECTED;
    yg__g.relook = 0;
    yg__log(YG_LOG_INFO, "YouGame: connected to the YouGame app (the game was started on its own)%s", NULL);
  }
  yg__g.hello_done = 1;
  if (r->ok && yg__is_obj(a)) {
    /* A new connection's hello names the player afresh: one from an earlier connection (another
     * account may have signed in since: accountChanged) is never kept. */
    free(yg__g.player_id);
    free(yg__g.player_name);
    yg__g.player_id = yg__g.player_name = NULL;
    yg__g.player_told = yg__g.player_retries = 0;
    if (yg__take_hello(a)) {
      yg__g.player_told = 1;
      yg__g.player_state = YG_PLAYER_KNOWN;
    } else if (yg__g.transport == YG_TRANSPORT_LOCAL && yg__get(a, "playerPending", &v) && yg__kind(v) == 't') {
      /* Someone is signed in and the app's page was not ready: the player comes by push or by
       * asking hello again (yg__player_tick), never by the game asking. */
      yg__g.player_state = YG_PLAYER_PENDING;
      yg__g.pending_at = yg__now();
      yg__log(YG_LOG_INFO, "YouGame: hello answered before the player was ready; waiting for them%s", NULL);
    } else {
      yg__g.player_state = YG_PLAYER_NONE;
    }
    yg__g.ready = 1;
  }
  yg__flush_held();
  yg__g.game_state_sent = 0; /* a new connection hears the latest state once */
  yg__game_state_flush();
  if (yg__g.ready && r->ok && yg__g.cfg.on_ready) yg__g.cfg.on_ready(yg__g.cfg.user);
}

/* What an answer teaches the state: feedback's feature detect, and the player's name. */
static void yg__note(const yg__call *c, const yg_result *res) {
  if (!strcmp(c->type, "openFeedback")) {
    if (res->feedback.reason == YG_REASON_UNSUPPORTED) yg__g.feedback_unsupported = 1;
    return;
  }
  if (!res->ok || (strcmp(c->type, "setName") != 0 && strcmp(c->type, "requestName") != 0)) return;
  if (c->type[0] == 'r') {
    /* As sdk.js requestName: the name is taken only when the player shares it. */
    yg__g.name_shared = res->player.shared;
    if (!yg__g.name_shared) return;
  }
  if (*res->player.id) yg__set_str(&yg__g.player_id, res->player.id);
  if (*res->player.name) yg__set_str(&yg__g.player_name, res->player.name);
  /* The getters are filled in now. on_player stays for the hello result (push or re-ask), which
   * still fires it once if the player was pending. */
  if (*res->player.id || *res->player.name) yg__g.player_state = YG_PLAYER_KNOWN;
}

/* The one answer of call c. browser: the feedback page opened in the browser instead. */
static void yg__complete(yg__call *c, const yg__reply *r, int browser) {
  yg_result res;
  yg__pool pool = {NULL, 0, 0};
  if (c->is_hello) {
    yg__unlink(c);
    if (c->is_hello == 1) {
      yg__log(YG_LOG_INFO, r->ok ? "YouGame %s: ok" : "YouGame %s: failed", c->type);
      yg__handle_hello(r, c->hello_gen);
    } else {
      yg__handle_retry(r, c->hello_gen, c->is_hello == 3);
    }
    yg__free_call(c);
    return;
  }
  yg__fill(&pool, c, r, browser, &res);
  /* Only a copy started on its own looks for the browser: the app started any other, so a failed
   * ask there is reported as it is. */
  if (!browser && !c->browser_tried && yg__g.transport == YG_TRANSPORT_LOCAL && !strcmp(c->type, "openFeedback") &&
      res.feedback.reason == YG_REASON_NOT_AVAILABLE) {
    yg__pool_free(&pool);
    if (yg__open_page(c->id, c->feedback_kind)) {
      c->browser_wait = c->browser_tried = 1; /* the answer comes when the opener says how it went */
      c->deadline = yg__now() + YG__TIMEOUT;
      return;
    }
    yg__fill(&pool, c, r, 0, &res);
  }
  yg__unlink(c);
  if (browser) yg__log(YG_LOG_INFO, "YouGame: no YouGame app answered; opened the feedback page in the browser%s", NULL);
  yg__log(YG_LOG_INFO, r->ok ? "YouGame %s: ok" : "YouGame %s: failed", c->type);
  yg__note(c, &res);
  if (c->cb) c->cb(c->user, &res);
  yg__pool_free(&pool);
  yg__free_call(c);
}

static void yg__send_hello(void) {
  if (yg__g.hello_sent) return;
  yg__g.hello_sent = 1;
  yg__send_hello_line(1);
}

/* hello's line: the connection's own (is_hello 1), or a re-ask while the player is pending (2, 3),
 * which says the same. The app answers a repeated hello on the connection from what its page knows
 * then, and keeps the first hello's claim. */
static void yg__send_hello_line(int is_hello) {
  yg__buf extra = {NULL, 0, 0, 0};
  char version[65];
  yg__span none = {NULL, 0};
  yg__buf_puts(&extra, ",\"v\":\"" YG_VERSION "\",\"engine\":");
  yg__json_str(&extra, yg__g.engine, strlen(yg__g.engine));
  if (yg__g.transport == YG_TRANSPORT_LOCAL) {
    /* Contract section 4: the listing slug, the build's version and channel 1. */
    double timeout = YG__LOCAL_HELLO_TIMEOUT;
#ifdef YOUGAME_TESTING
    if (yg__test_local_hello_timeout > 0) timeout = yg__test_local_hello_timeout;
#endif
    yg__clean_version(version, yg__g.version);
    yg__buf_puts(&extra, ",\"game\":");
    yg__json_str(&extra, yg__g.slug, strlen(yg__g.slug));
    yg__buf_puts(&extra, ",\"version\":");
    yg__json_str(&extra, version, strlen(version));
    yg__buf_puts(&extra, ",\"channel\":1");
#ifdef YOUGAME_TEST_BUILD
    /* Presentation only: the standalone overlay keeps the purple YouGame button present. */
    yg__buf_puts(&extra, ",\"testBuild\":true");
#endif
    yg__call_start("hello", none, &extra, timeout, NULL, NULL, is_hello);
  } else {
    yg__call_start("hello", none, &extra, YG__TIMEOUT, NULL, NULL, is_hello);
  }
  yg__buf_free(&extra);
}

static void yg__handle_line(char *line, size_t len) {
  yg__host h;
  yg__reply r;
  yg__call *c;
  yg__span v;
  yg__pool pool = {NULL, 0, 0};
  int used = 0;
  yg__parse_host(line, len, &h);
  if (h.kind == YG__HOST_ANSWER) {
    c = yg__find(h.id);
    if (c && !c->browser_wait) {
      double after = 0;
      memset(&r, 0, sizeof r);
      r.ok = yg__f_bool(h.obj, "ok", 0);
      if (!r.ok && yg__f_num(h.obj, "retryAfter", &after) && after > 0)
        r.retry_after = after >= 86400.0 ? 86400 : (int)after + ((double)(int)after < after ? 1 : 0);
      r.result = yg__get(h.obj, "result", &v) ? v : yg__null_span;
      r.error = yg__f_str(&pool, h.obj, "error");
      r.code = yg__f_str(&pool, h.obj, "code");
      if (!r.ok && !*r.error) r.error = "YouGame refused the request";
      yg__complete(c, &r, 0);
      used = 1;
    }
  } else if (h.kind == YG__HOST_PUSH) {
    const char *type = yg__f_str(&pool, h.push, "type");
    if (!strcmp(type, "playerArrived")) {
      /* The local channel's player, after a hello that said playerPending: taken here, once, and
       * told through on_player (never on_push). The result is what a hello would answer now. */
      if (yg__g.transport == YG_TRANSPORT_LOCAL && yg__g.local_state == YG__CONNECTED &&
          yg__get(h.push, "result", &v) && yg__is_obj(v))
        yg__player_arrived(v, "pushed by YouGame");
    } else if (yg__g.cfg.on_push) {
      const char *json = yg__pool_copy(&pool, h.push.p, h.push.n);
      yg__g.cfg.on_push(yg__g.cfg.user, type, json);
    }
  } else if (yg__g.transport == YG_TRANSPORT_STDIO && h.kind == YG__HOST_QUIT) {
    /* `embed` lines belong to a game the app started; the local channel has none. */
    if (!yg__g.quit_requested) {
      yg__g.quit_requested = 1;
      yg__log(YG_LOG_INFO, "YouGame: the app asked the game to quit%s", NULL);
      if (yg__g.cfg.on_quit) yg__g.cfg.on_quit(yg__g.cfg.user);
    }
  } else if (yg__g.transport == YG_TRANSPORT_STDIO && h.kind == YG__HOST_VOLUME) {
    if (yg__g.cfg.on_volume) yg__g.cfg.on_volume(yg__g.cfg.user, h.volume);
  }
  yg__pool_free(&pool);
  if (!used && yg__g.cfg.on_line && !yg__g.shutdown_wanted) yg__g.cfg.on_line(yg__g.cfg.user, line, len);
}

static void yg__handle_event(yg__event *e) {
  yg__reply r;
  yg__call *c;
  int was_connected;
  switch (e->kind) {
    case YG__EV_LINE:
      yg__handle_line(e->line, e->len);
      break;
    case YG__EV_REPLY:
      c = yg__find(e->id);
      if (c && !c->browser_wait) {
        memset(&r, 0, sizeof r);
        r.result = yg__null_span;
        r.code = e->code ? e->code : "";
        r.error = e->text ? e->text : "";
        yg__complete(c, &r, 0);
      }
      break;
    case YG__EV_BROWSER:
      c = yg__find(e->id);
      if (c && c->browser_wait) {
        memset(&r, 0, sizeof r);
        r.result = yg__null_span;
        r.code = "notAvailable";
        r.error = "not available";
        if (!e->ok) yg__log(YG_LOG_WARN, "YouGame: the browser did not open the feedback page%s", NULL);
        c->browser_wait = 0;
        yg__complete(c, &r, e->ok);
      }
      break;
    case YG__EV_CLOSED:
      if (!yg__g.closed) {
        yg__g.closed = 1;
        yg__log(YG_LOG_WARN, "YouGame: the app's channel closed; calls now fail%s", NULL);
        yg__fail_with("noAnswer", "YouGame closed the channel", 1);
      }
      break;
    case YG__EV_UP:
      if (e->id == yg__g.local_gen && yg__g.local_state == YG__PROBING) yg__send_hello();
      break;
    case YG__EV_DOWN:
      if (e->id == yg__g.local_gen && yg__g.local_state != YG__IDLE) {
        was_connected = yg__g.local_state == YG__CONNECTED;
        yg__g.local_state = YG__IDLE;
        yg__g.hello_sent = yg__g.hello_done = 0;
        /* The player belonged to the connection that ended (a pending one never comes): unknown,
         * with no name or id, until the next connection's hello names them. */
        free(yg__g.player_id);
        free(yg__g.player_name);
        yg__g.player_id = yg__g.player_name = NULL;
        yg__g.player_state = YG_PLAYER_UNKNOWN;
        yg__g.player_told = yg__g.player_retries = 0;
        if (was_connected) {
          /* As stdio's end closing: what was asked fails; the next call looks again, and so does
           * yg_update on its own for a while (yg__relook_tick). */
          yg__g.relook = 1;
          yg__g.relook_until = yg__now() + YG__RELOOK_FOR;
          yg__log(YG_LOG_INFO, "YouGame: the YouGame app closed the local channel%s", NULL);
          yg__fail_with("noAnswer", "YouGame closed the channel", 1);
        } else {
          /* Nothing found: the calls that waited for this look answer "not available". When no
           * app channel was there at all (e->ok), yg_update keeps looking on its own for a while:
           * a game started before the app (the start window's Get, or the player opening YouGame
           * by hand) finds it with no call. An app that opened the channel and then dropped it
           * unanswered (it refused this program, or was full) is not looked at again on its own.
           * A look that fails while already looking never stretches the window. */
          if (e->ok && !yg__g.relook) {
            yg__g.relook = 1;
            yg__g.relook_until = yg__now() + YG__RELOOK_FOR;
          }
          yg__fail_with("notAvailable", "not available", 0);
        }
      }
      break;
    case YG__EV_LOG: /* ok carries the level; 0 is info */
      yg__log(e->ok ? e->ok : YG_LOG_INFO, "%s", e->text);
      break;
    default:
      break;
  }
}

static void yg__expire(void) {
  double now = yg__now();
  long long *ids;
  size_t n = 0, cap = 0, i;
  yg__call *c;
  yg__reply r;
  for (c = yg__g.calls; c; c = c->next) ++cap;
  if (!cap) return;
  ids = (long long *)malloc(cap * sizeof *ids);
  if (!ids) return;
  for (c = yg__g.calls; c; c = c->next)
    if (now >= c->deadline) ids[n++] = c->id;
  memset(&r, 0, sizeof r);
  r.result = yg__null_span;
  r.code = "noAnswer";
  r.error = "YouGame did not answer in time";
  for (i = 0; i < n && !yg__g.shutdown_wanted; ++i) {
    c = yg__find(ids[i]);
    if (!c) continue;
    c->browser_wait = 0;
    yg__unqueue(c->id); /* an expired call's line that has not gone out never goes */
    yg__complete(c, &r, 0);
  }
  free(ids);
}

/* ---------------------------------------------------------------------------------------------
 * The public calls */

static void yg__copy_trimmed(char *out, size_t size, const char *s) {
  size_t n;
  out[0] = 0;
  if (!s) return;
  while (yg__is_ws(*s)) ++s;
  n = strlen(s);
  while (n > 0 && yg__is_ws(s[n - 1])) --n;
  if (n >= size) n = size - 1;
  memcpy(out, s, n);
  out[n] = 0;
}

int yg_init(const yg_config *config) {
  const char *stdio_env, *bridge, *token;
  char raw_slug[200];
  size_t i, n;
  if (yg__g.inited) return -1;
  yg__lock(); /* a feedback opener from an earlier run may still read the epoch */
  {
    long long epoch = yg__g.epoch;
    memset(&yg__g, 0, sizeof yg__g);
    yg__g.epoch = epoch + 1;
  }
  yg__unlock();
  if (config) yg__g.cfg = *config;
  yg__g.next_id = 1;
  yg__g.last_probe_at = -1e9;
  yg__copy_trimmed(raw_slug, sizeof raw_slug, config ? config->game_slug : NULL);
  if (strlen(raw_slug) < sizeof yg__g.slug) strcpy(yg__g.slug, raw_slug);
  if (config && config->game_version) {
    n = strlen(config->game_version);
    if (n >= sizeof yg__g.version) n = sizeof yg__g.version - 1;
    memcpy(yg__g.version, config->game_version, n);
  }
  /* hello's engine label: printable ASCII, at most 32 characters. */
  for (i = 0, n = 0; config && config->engine && config->engine[i] && n < 32; ++i)
    if ((unsigned char)config->engine[i] > 0x20 && (unsigned char)config->engine[i] < 0x7F)
      yg__g.engine[n++] = config->engine[i];
  if (!n) strcpy(yg__g.engine, "native");
  yg__copy_trimmed(yg__g.site, sizeof yg__g.site, config ? config->feedback_site : NULL);
  if (!yg__g.site[0] && yg__g.cfg.staging) strcpy(yg__g.site, YG__STAGING_SITE);
  /* The strings in the copy of config belong to the game; only the copies above are used. */
  yg__g.cfg.game_slug = yg__g.cfg.game_version = yg__g.cfg.engine = yg__g.cfg.feedback_site = NULL;
#ifdef _WIN32
  yg__g.wake = CreateEventW(NULL, FALSE, FALSE, NULL); /* auto-reset: the wait that sees it clears it */
#else
  {
    int fds[2];
    yg__g.wake_r = yg__g.wake_w = -1;
    if (pipe(fds) == 0) {
      for (i = 0; i < 2; ++i) {
        fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL, 0) | O_NONBLOCK);
      }
      yg__g.wake_r = fds[0];
      yg__g.wake_w = fds[1];
    }
  }
#endif

  /* The app's environment (desktop native.ts): YOUGAME_STDIO=1 on every Windows build and on a Mac
   * or Linux build with yougame-native.json, else the loopback bridge (README). Neither set: the player started the game on their own, and the local
   * channel to an open YouGame app is tried (contract section 1). Either set: it never is. */
  stdio_env = getenv("YOUGAME_STDIO");
  bridge = getenv("YOUGAME_BRIDGE");
  token = getenv("YOUGAME_TOKEN");
  yg__g.started_by_app = (stdio_env && *stdio_env) || (bridge && *bridge);
  if (stdio_env && !strcmp(stdio_env, "1")) {
    yg__g.transport = YG_TRANSPORT_STDIO;
  } else if (bridge && *bridge && token && *token) {
    yg__post(YG__EV_LOG, 0, YG_LOG_WARN, NULL, "YouGame: started by the app over its loopback bridge, which this header does not speak yet; calls answer \"not available\"");
  } else if (!yg__g.started_by_app) {
    if (yg_valid_slug(yg__g.slug)) {
      yg__g.transport = YG_TRANSPORT_LOCAL;
    } else if (!raw_slug[0]) {
      yg__post(YG__EV_LOG, 0, YG_LOG_WARN, NULL, "YouGame: the game slug is empty. Until it is set (the part after /g/ in your listing's address), a copy started on its own does not look for the YouGame app and the feedback page cannot open");
    } else {
      yg__post(YG__EV_LOG, 0, YG_LOG_WARN, NULL, "YouGame: the game slug is not a listing slug (a-z, 0-9 and dashes); not looking for the YouGame app");
    }
  }
  /* Logged from the first yg_update, like every other line: config.log only ever runs there. */
  yg__post(YG__EV_LOG, 0, YG_LOG_INFO, NULL,
           yg__g.transport == YG_TRANSPORT_STDIO   ? "YouGame: stdio"
           : yg__g.transport == YG_TRANSPORT_LOCAL ? "YouGame: started alone: looking for the YouGame app's local channel"
                                                    : "YouGame: not started by YouGame: calls answer \"not available\"");
  yg__lock();
  yg__g.inited = 1;
  yg__unlock();

  if (yg__g.transport != YG_TRANSPORT_NONE) {
    int ok;
#ifdef _WIN32
    yg__g.thread = (HANDLE)_beginthreadex(NULL, 0, yg__g.transport == YG_TRANSPORT_STDIO ? yg__stdio_main : yg__local_main,
                                          NULL, 0, NULL);
    ok = yg__g.thread != NULL && yg__g.wake != NULL;
    if (!ok && yg__g.thread) {
      yg__lock();
      yg__g.stopping = 1;
      yg__unlock();
      WaitForSingleObject(yg__g.thread, INFINITE);
      CloseHandle(yg__g.thread);
      yg__g.thread = NULL;
      yg__lock();
      yg__g.stopping = 0;
      yg__unlock();
    }
#else
    /* SIGPIPE: the stdio thread blocks it on itself only (yg__stdio_main); sockets use
     * MSG_NOSIGNAL or SO_NOSIGPIPE. The process's own handler is never touched. */
    ok = pthread_create(&yg__g.thread, NULL, yg__g.transport == YG_TRANSPORT_STDIO ? yg__stdio_main : yg__local_main,
                        NULL) == 0;
#endif
    yg__g.thread_up = ok;
    if (!ok) {
      yg__post(YG__EV_LOG, 0, YG_LOG_WARN, NULL, "YouGame: could not start its thread");
      yg__lock();
      yg__g.io_dead = 1;
      yg__unlock();
      if (yg__g.transport == YG_TRANSPORT_STDIO) {
        yg__g.closed = 1;
        yg__post(YG__EV_CLOSED, 0, 0, NULL, NULL);
      }
    }
  }

  switch (yg__g.transport) {
    case YG_TRANSPORT_STDIO:
      yg__send_hello();
      break;
    case YG_TRANSPORT_LOCAL:
      yg__start_probe();
      break;
    default:
      yg__g.hello_done = 1;
      break;
  }
  return 0;
}

void yg_shutdown(void) {
  yg__call *c, *next;
  yg__event *e, *en;
  yg__out *o, *on;
  if (!yg__g.inited) return;
  if (yg__g.in_update) {
    yg__g.shutdown_wanted = 1; /* inside a callback: done when yg_update returns */
    return;
  }
  yg__lock();
  yg__g.stopping = 1;
  ++yg__g.epoch; /* a feedback opener still running posts nothing now */
  yg__unlock();
  yg__wake();
  if (yg__g.thread_up) {
#ifdef _WIN32
    /* A write the app is not reading blocks the thread; after about 2 s it is cancelled and the
     * lines not yet sent are dropped. */
    if (WaitForSingleObject(yg__g.thread, YG__STOP_WAIT_MS) == WAIT_TIMEOUT) {
      CancelSynchronousIo(yg__g.thread);
      WaitForSingleObject(yg__g.thread, INFINITE);
    }
    CloseHandle(yg__g.thread);
#else
    pthread_join(yg__g.thread, NULL);
#endif
  }
  yg__lock();
  for (e = yg__g.in_head; e; e = en) {
    en = e->next;
    free(e->line);
    free(e);
  }
  for (o = yg__g.out_head; o; o = on) {
    on = o->next;
    free(o->line);
    free(o);
  }
  yg__g.in_head = yg__g.in_tail = NULL;
  yg__g.out_head = yg__g.out_tail = NULL;
  yg__g.inited = 0;
  yg__unlock();
  /* The game is shutting the plugin down: callbacks still waiting are dropped, not called. */
  for (c = yg__g.calls; c; c = next) {
    next = c->next;
    yg__free_call(c);
  }
  yg__g.calls = NULL;
#ifdef _WIN32
  if (yg__g.wake) CloseHandle(yg__g.wake);
  yg__g.wake = NULL;
#else
  if (yg__g.wake_r >= 0) close(yg__g.wake_r);
  if (yg__g.wake_w >= 0) close(yg__g.wake_w);
  yg__g.wake_r = yg__g.wake_w = -1;
#endif
  free(yg__g.player_id);
  free(yg__g.player_name);
  free(yg__g.game_id);
  free(yg__g.hello_json);
  yg__g.player_id = yg__g.player_name = yg__g.game_id = yg__g.hello_json = NULL;
  yg__g.thread_up = 0;
}

/* Started on its own, and a look found nothing or the app ended the connection: look again every
 * YG__RELOOK_EVERY s until it answers or YG__RELOOK_FOR s have passed (then the next call looks). */
static void yg__relook_tick(void) {
  double every = YG__RELOOK_EVERY, now;
#ifdef YOUGAME_TESTING
  if (yg__test_relook_every >= 0) every = yg__test_relook_every;
#endif
  if (!yg__g.relook || yg__g.transport != YG_TRANSPORT_LOCAL || yg__g.local_state != YG__IDLE || yg__g.closed)
    return;
  now = yg__now();
  if (now > yg__g.relook_until) {
    yg__g.relook = 0;
    return;
  }
  if (now - yg__g.last_probe_at >= every) yg__start_probe();
}

void yg_update(void) {
  yg__event *head, *tail, *e, *next;
  long dropped;
  int n = 0;
  if (!yg__g.inited || yg__g.in_update) return;
  yg__g.in_update = 1;
  /* What arrived before now; anything a callback causes waits for the next update. */
  yg__lock();
  head = yg__g.in_head;
  tail = head;
  while (tail && tail->next && ++n < YG__EVENTS_PER_UPDATE) tail = tail->next;
  if (tail) {
    yg__g.in_head = tail->next;
    if (!yg__g.in_head) yg__g.in_tail = NULL;
    tail->next = NULL;
  }
  for (e = head; e; e = e->next) yg__g.in_bytes -= e->line ? yg__event_cost(e->len) : 0;
  dropped = yg__g.in_dropped;
  yg__g.in_dropped = 0;
  yg__unlock();
  if (dropped) yg__log(YG_LOG_WARN, "YouGame: dropped lines from the app while the game was not reading%s", NULL);
  for (e = head; e; e = next) {
    next = e->next;
    if (!yg__g.shutdown_wanted) yg__handle_event(e);
    free(e->line);
    free(e);
  }
  if (!yg__g.shutdown_wanted) yg__expire();
  if (!yg__g.shutdown_wanted) yg__player_tick();
  if (!yg__g.shutdown_wanted) yg__relook_tick();
  yg__g.in_update = 0;
  if (yg__g.shutdown_wanted) {
    yg__g.shutdown_wanted = 0;
    yg_shutdown();
  }
}

static long long yg__simple(const char *type, yg_callback cb, void *user) {
  yg__span none = {NULL, 0};
  return yg__call_start(type, none, NULL, YG__TIMEOUT, cb, user, 0);
}

long long yg_request_name(yg_callback cb, void *user) {
  /* sdk.md: requestName answers at once, no prompt, so the default timeout applies. */
  return yg__simple("requestName", cb, user);
}

long long yg_set_name(const char *name, yg_callback cb, void *user) {
  yg__buf clean = {NULL, 0, 0, 0}, extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  long long id;
  yg__clean_name(&clean, name);
  if (clean.failed || clean.len == 0) {
    yg__buf_free(&clean);
    return yg__refuse("setName", "Name is empty", cb, user);
  }
  yg__buf_puts(&extra, ",\"name\":");
  yg__json_str(&extra, clean.p, clean.len);
  id = yg__call_start("setName", none, &extra, YG__TIMEOUT, cb, user, 0);
  yg__buf_free(&clean);
  yg__buf_free(&extra);
  return id;
}

long long yg_submit_score(long long score, yg_callback cb, void *user) {
  yg__buf extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  long long id;
  if (score < 0) return yg__refuse("submitScore", "Score must be a non-negative number", cb, user);
  yg__buf_puts(&extra, ",\"score\":");
  yg__buf_int(&extra, score > YG__MAX_SCORE ? YG__MAX_SCORE : score); /* sdk.js cleanScore's ceiling */
  id = yg__call_start("submitScore", none, &extra, YG__TIMEOUT, cb, user, 0);
  yg__buf_free(&extra);
  return id;
}

long long yg_match_state(int open, yg_callback cb, void *user) {
  yg__buf extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  long long id;
  yg__buf_puts(&extra, open ? ",\"open\":true" : ",\"open\":false");
  id = yg__call_start("matchState", none, &extra, YG__TIMEOUT, cb, user, 0);
  yg__buf_free(&extra);
  return id;
}

long long yg_leaderboard(int period, int limit, int friends, yg_callback cb, void *user) {
  yg__buf extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  long long id;
  yg__buf_puts(&extra, period == YG_PERIOD_DAY ? ",\"period\":\"day\"" : period == YG_PERIOD_WEEK ? ",\"period\":\"week\"" : ",\"period\":\"all\"");
  yg__buf_puts(&extra, ",\"limit\":");
  yg__buf_int(&extra, limit <= 0 ? 10 : limit > 100 ? 100 : limit);
  yg__buf_puts(&extra, friends ? ",\"scope\":\"friends\"" : ",\"scope\":\"all\"");
  id = yg__call_start("leaderboard", none, &extra, YG__TIMEOUT, cb, user, 0);
  yg__buf_free(&extra);
  return id;
}

/* sdk.js encodeSave: JSON other than null, no NUL characters, at most 64 KiB once written out. */
static const char *yg__check_save(const char *json, yg__buf *out) {
  yg__span v;
  if (!json || !yg__json_check(json, strlen(json), &v) || yg__kind(v) == 'n')
    return "The save must be JSON (an object, array, string, number or boolean); use yg_save_clear to remove it";
  yg__emit(out, v);
  if (out->failed) return "Out of memory";
  /* Also refuses a string whose text is a backslash then "u0000" (not a NUL), on purpose: simple. */
  if (strstr(out->p, "\\u0000")) return "The save cannot contain NUL characters";
  if (out->len > YG__SAVE_MAX) return "The save is over 65536 bytes";
  return NULL;
}

long long yg_save_write(const char *data_json, yg_callback cb, void *user) {
  yg__buf data = {NULL, 0, 0, 0}, extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  const char *problem = yg__check_save(data_json, &data);
  long long id;
  if (problem) {
    yg__buf_free(&data);
    return yg__refuse("saveWrite", problem, cb, user);
  }
  yg__buf_puts(&extra, ",\"data\":");
  yg__buf_put(&extra, data.p, data.len);
  id = yg__call_start("saveWrite", none, &extra, YG__TIMEOUT, cb, user, 0);
  yg__buf_free(&data);
  yg__buf_free(&extra);
  return id;
}

long long yg_save_load(yg_callback cb, void *user) { return yg__simple("saveLoad", cb, user); }
long long yg_save_clear(yg_callback cb, void *user) { return yg__simple("saveClear", cb, user); }
long long yg_open_menu(yg_callback cb, void *user) { return yg__simple("openMenu", cb, user); }

static long long yg__input_event(const char *type, const char *kind, yg_callback cb, void *user) {
  char body[32];
  if (!kind || (strcmp(kind, "keys") && strcmp(kind, "ps") && strcmp(kind, "pad") && strcmp(kind, "gc")))
    return yg__refuse(type, "Expected an input kind: keys, ps, pad or gc.", cb, user);
  snprintf(body, sizeof body, "{\"kind\":\"%s\"}", kind);
  return yg_call(type, body, 0, cb, user);
}
long long yg_set_input_kind(const char *kind, yg_callback cb, void *user) { return yg__input_event("inputKind", kind, cb, user); }
long long yg_menu_button(const char *kind, yg_callback cb, void *user) { return yg__input_event("menuButton", kind, cb, user); }

/* The latest game state to the app, when this connection has not heard it yet. Only once hello
 * answered: a held line would be dropped with hello's failure, and the next hello sends it anyway. */
static int yg__game_state_flush(void) {
  yg__buf extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  int s = yg__g.game_state;
  if (!s || s == yg__g.game_state_sent || !yg__g.hello_done || yg__g.closed) return 0;
  if (yg__g.transport == YG_TRANSPORT_NONE) return 0;
  if (yg__g.transport == YG_TRANSPORT_LOCAL && yg__g.local_state != YG__CONNECTED) return 0;
  yg__buf_puts(&extra, s == YG_GAME_PAUSED ? ",\"state\":\"paused\"" : s == YG_GAME_MENU ? ",\"state\":\"menu\"" : ",\"state\":\"playing\"");
  if (!yg__call_start("gameState", none, &extra, YG__TIMEOUT, NULL, NULL, 0)) {
    yg__buf_free(&extra); /* out of memory: the next call or hello tries again */
    return 0;
  }
  yg__buf_free(&extra);
  yg__g.game_state_sent = s;
  return 1;
}

int yg_set_game_state(yg_game_state state) {
  int s = (int)state;
  if (!yg__g.inited || (s != YG_GAME_PLAYING && s != YG_GAME_PAUSED && s != YG_GAME_MENU)) return -1;
  yg__g.game_state = s;
  if (yg__g.transport == YG_TRANSPORT_LOCAL && yg__g.local_state == YG__IDLE && !yg__g.closed) {
    /* Started on its own and not connected: look again like any call (at most once every 5 s,
     * contract section 1.3); the state goes after that look's hello. */
    double cooldown = YG__PROBE_COOLDOWN;
#ifdef YOUGAME_TESTING
    if (yg__test_probe_cooldown >= 0) cooldown = yg__test_probe_cooldown;
#endif
    if (yg__now() - yg__g.last_probe_at >= cooldown) yg__start_probe();
    return 0;
  }
  return yg__game_state_flush();
}

long long yg_open_feedback(int kind, const char *draft_text, yg_callback cb, void *user) {
  yg__buf draft = {NULL, 0, 0, 0}, extra = {NULL, 0, 0, 0};
  yg__span none = {NULL, 0};
  yg__call *c;
  long long id;
  /* As YouGame.feedback(): kind only for bug or idea, the draft cut to 2,000 characters. */
  if (kind == YG_FEEDBACK_BUG) yg__buf_puts(&extra, ",\"kind\":\"bug\"");
  else if (kind == YG_FEEDBACK_IDEA) yg__buf_puts(&extra, ",\"kind\":\"idea\"");
  yg__cut_draft(&draft, draft_text);
  if (draft.len) {
    yg__buf_puts(&extra, ",\"draftText\":");
    yg__json_str(&extra, draft.p, draft.len);
  }
  id = yg__call_start("openFeedback", none, &extra, YG__TIMEOUT, cb, user, 0);
  c = id ? yg__find(id) : NULL;
  if (c) c->feedback_kind = kind;
  yg__buf_free(&draft);
  yg__buf_free(&extra);
  return id;
}

long long yg_call(const char *type, const char *body_json, double timeout_seconds, yg_callback cb, void *user) {
  yg__span body = {NULL, 0};
  size_t i, n = type ? strlen(type) : 0;
  int ok = n >= 1 && n < 32;
  for (i = 0; ok && i < n; ++i)
    ok = (type[i] >= 'a' && type[i] <= 'z') || (type[i] >= 'A' && type[i] <= 'Z') || (type[i] >= '0' && type[i] <= '9');
  if (!ok) return yg__refuse("call", "The type is not a request type", cb, user);
  if (!strcmp(type, "hello")) return yg__refuse(type, "hello is sent by yg_init", cb, user);
  if (body_json && *body_json && (!yg__json_check(body_json, strlen(body_json), &body) || !yg__is_obj(body)))
    return yg__refuse(type, "The body must be a JSON object", cb, user);
  return yg__call_start(type, body, NULL, timeout_seconds > 0 ? timeout_seconds : YG__TIMEOUT, cb, user, 0);
}

void yg_relay_key(const char *key) {
  const char *line;
  char *copy;
  if (!yg__g.inited || yg__g.transport != YG_TRANSPORT_STDIO || yg__g.closed || !key) return;
  if (!strcmp(key, "escape")) line = "yg {\"key\":\"escape\"}\n";
  else if (!strcmp(key, "shift-escape")) line = "yg {\"key\":\"shift-escape\"}\n";
  else return;
  copy = yg__strndup(line, strlen(line));
  if (copy) yg__enqueue(0, 0, copy, strlen(copy));
}

long long yg_next_id(void) { return yg__g.inited ? yg__g.next_id++ : 0; }

int yg_write_line(const char *line, size_t len) {
  char *copy;
  size_t i;
  if (!yg__g.inited || !line || yg__g.closed || !yg_connected()) return -1;
  if (len > 0 && line[len - 1] == '\n') --len;
  for (i = 0; i < len; ++i)
    if (line[i] == '\n' || line[i] == 0) return -1;
  if (len + 1 > (yg__g.transport == YG_TRANSPORT_LOCAL
                     ? (yg__g.local_line_max ? yg__g.local_line_max : YG__LOCAL_LINE_OLD)
                     : YG__STDIO_LINE_MAX))
    return -1;
  copy = (char *)malloc(len + 2);
  if (!copy) return -1;
  memcpy(copy, line, len);
  copy[len] = '\n';
  copy[len + 1] = 0;
  return yg__enqueue(0, yg__g.transport == YG_TRANSPORT_LOCAL ? yg__g.local_gen : 0, copy, len + 1);
}

int yg_connected(void) {
  if (!yg__g.inited) return 0;
  if (yg__g.transport == YG_TRANSPORT_LOCAL) return yg__g.local_state == YG__CONNECTED;
  return yg__g.transport != YG_TRANSPORT_NONE && !yg__g.closed;
}

int yg_ready(void) { return yg__g.inited && yg__g.ready; }
yg_started yg_started_by(void) { return yg__g.started_by_app ? YG_STARTED_BY_APP : YG_STARTED_ALONE; }
yg_transport yg_transport_kind(void) { return (yg_transport)yg__g.transport; }
yg_player_state yg_player_status(void) { return yg__g.inited ? (yg_player_state)yg__g.player_state : YG_PLAYER_UNKNOWN; }
const char *yg_player_name(void) { return yg__g.player_name ? yg__g.player_name : ""; }
const char *yg_player_id(void) { return yg__g.player_id ? yg__g.player_id : ""; }
const char *yg_game_id(void) { return yg__g.game_id ? yg__g.game_id : ""; }
int yg_name_shared(void) { return yg__g.name_shared; }
const char *yg_hello_json(void) { return yg__g.hello_json ? yg__g.hello_json : ""; }
int yg_quit_requested(void) { return yg__g.quit_requested; }

int yg_should_show_feedback_button(void) {
  char url[400];
  if (!yg__g.inited) return 0;
  if (yg__g.transport != YG_TRANSPORT_LOCAL) return yg_connected() && !yg__g.feedback_unsupported;
  /* Started on its own: the app's sheet while it answers, else the feedback page in the browser. */
  if (yg_connected()) return !yg__g.feedback_unsupported;
  return yg_feedback_page_url(yg__g.site, yg__g.slug, YG_FEEDBACK_ASK, url, sizeof url) >= 0;
}

const char *yg_reason_name(int reason) {
  switch (reason) {
    case YG_REASON_NONE: return "None";
    case YG_REASON_ALREADY_OPEN: return "AlreadyOpen";
    case YG_REASON_TOO_SOON: return "TooSoon";
    case YG_REASON_NO_ACTIVATION: return "NoActivation";
    case YG_REASON_NOT_HERE: return "NotHere";
    case YG_REASON_UNSUPPORTED: return "Unsupported";
    case YG_REASON_NO_ANSWER: return "NoAnswer";
    case YG_REASON_NOT_AVAILABLE: return "NotAvailable";
    default: return "Unknown";
  }
}

/* ---------------------------------------------------------------------------------------------
 * JSON getters */

static int yg__json_find(const char *json, const char *path, yg__span *out) {
  yg__span root;
  return json && yg__json_check(json, strlen(json), &root) && yg__path(root, path, out);
}

long yg_json_string(const char *json, const char *path, char *out, size_t out_size) {
  yg__span v;
  yg__buf b = {NULL, 0, 0, 0};
  size_t n;
  long len;
  if (out && out_size) out[0] = 0;
  if (!yg__json_find(json, path, &v) || !yg__is_str(v)) return -1;
  yg__str_decode(&b, v);
  if (b.failed) {
    yg__buf_free(&b);
    return -1;
  }
  len = (long)b.len;
  if (out && out_size) {
    n = b.len;
    if (n >= out_size) {
      n = out_size - 1;
      while (n > 0 && ((unsigned char)b.p[n] & 0xC0) == 0x80) --n; /* not half a character */
    }
    memcpy(out, b.p, n);
    out[n] = 0;
  }
  yg__buf_free(&b);
  return len;
}

int yg_json_number(const char *json, const char *path, double *out) {
  yg__span v;
  double d;
  if (!yg__json_find(json, path, &v) || !yg__num(v, &d)) return 0;
  if (out) *out = d;
  return 1;
}

int yg_json_bool(const char *json, const char *path, int *out) {
  yg__span v;
  if (!yg__json_find(json, path, &v) || (yg__kind(v) != 't' && yg__kind(v) != 'f')) return 0;
  if (out) *out = yg__kind(v) == 't';
  return 1;
}

long yg_json_count(const char *json, const char *path) {
  yg__span v;
  if (!yg__json_find(json, path, &v)) return -1;
  return yg__count(v);
}

long yg_json_raw(const char *json, const char *path, char *out, size_t out_size) {
  yg__span v;
  if (out && out_size) out[0] = 0;
  if (!yg__json_find(json, path, &v)) return -1;
  if (out && v.n < out_size) {
    memcpy(out, v.p, v.n);
    out[v.n] = 0;
  }
  return (long)v.n;
}

#ifdef __cplusplus
}
#endif
#endif /* YOUGAME_IMPLEMENTED */
#endif /* YOUGAME_IMPLEMENTATION */

/*
MIT License

Copyright (c) 2026 YouGame (https://yougame.co)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
