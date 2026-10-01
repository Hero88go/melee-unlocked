/* Exercise the production content hook and the exact production runGameMode body with scene/disc
 * stubs. No game, archive, renderer or networking is involved. The generated runner avoids copying
 * the callback sequence into a test that would still pass if the real major boundary moved. */
#include <melee/gm/types.h>
#include <melee/lb/lbdvd.h>
#include <mu_tmce_event.h>
#include <placeholder.h>
#include <stdio.h>
#include <string.h>

struct routingInfo {
    u8 curr_mode, pending_mode, prev_mode;
    u8 curr_state_id, prev_state_id, next_state_id;
};
struct stateMachine {
    struct routingInfo routing, backup_routing;
    u8 pending_mode_change;
    u8 (*get_override)(void);
};
static struct stateMachine state_machine;
struct gmMainLib_8046B0F0_t gmMainLib_8046B0F0;

static int host_view, next_view = -1, releases, resets, unload_done, failures;
static int active_mode;
static char callbacks[128];
static int callback_count;

static void check(int ok, const char* what)
{
    if (!ok) { printf("FAIL: %s\n", what); ++failures; }
}
static void note(char event)
{
    callbacks[callback_count++] = event;
    callbacks[callback_count] = '\0';
}

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size)
{
    check(command == 0xF5 && size == 1 && capacity >= 1, "content command ABI");
    if (*payload != 0xFE) host_view = *payload != 0xFF;
    response[0] = (unsigned char) host_view;
    *response_size = 1;
    return 0;
}
void OSReport(char* fmt, ...) { (void) fmt; }
void lbDvd_80018CF4(int heaps)
{
    check(heaps == 0, "all preload heaps are released");
    check(unload_done, "release runs after the previous major unload");
    ++releases;
    note('D');
}
void lbDvd_80018F68(void)
{
    check(releases == resets + 1, "release precedes metadata reset exactly once");
    ++resets;
    note('R');
}
void lbDvd_80018F58(bool preloaded) { (void) preloaded; }
void mu_practice_enter_mode(int mode) { active_mode = mode; }
int mu_practice_resolve_pending_mode(int requested) { return requested; }
void mu_mex_boot(void)
{
    if (active_mode == GM_MENU) {
        check(host_view == 0, "menu uses the offline view before m-ex registry load");
    }
}
void mu_tmce_boot(void) {}
int mu_tmce_active(void) { return 0; }
void mu_tmce_ev_scene_lists(int active) { (void) active; }

static void old_load(void) { unload_done = 0; note('A'); }
static void new_load(void) { unload_done = 0; note('B'); }
static void menu_load(void)
{
    check(host_view == 0, "menu's first load sees the offline view");
    unload_done = 0; note('M');
    mu_content_mode(-1); /* The real menu prep's later, now-idempotent return-from-online call. */
}
static void major_unload(void) { unload_done = 1; note('U'); }
static GameMode old_mode = { .kind = GM_TRAINING, .on_load = old_load, .on_unload = major_unload };
static GameMode new_mode = { .kind = GM_HANYU_CSS, .on_load = new_load, .on_unload = major_unload };
static GameMode menu_mode = { .kind = GM_MENU, .on_load = menu_load, .on_unload = major_unload };
static GameMode* findMode(u8 kind)
{
    if (kind == GM_MENU) return &menu_mode;
    return kind == GM_TRAINING ? &old_mode : &new_mode;
}
static void gm_801A4014(GameMode* mode)
{
    if (next_view >= 0) {
        int before = releases;
        mu_content_mode(next_view ? 1 : -1);
        check(releases == before, "requesting a view leaves the active major's preloads intact");
        next_view = -1;
    }
    state_machine.routing.pending_mode = mode->kind == GM_TRAINING ? GM_HANYU_CSS : GM_TRAINING;
    state_machine.pending_mode_change = true;
}

#include "content_major_runner.h"

int main(void)
{
    u8 next;
    next_view = 1;
    next = runGameMode(GM_TRAINING);
    check(next == GM_HANYU_CSS && releases == 0 && resets == 0,
          "initial view and in-major request do not release archives");
    runGameMode(next);
    check(releases == 1 && resets == 1 && strcmp(callbacks, "AUDRBU") == 0,
          "old unload, full release/reset, new load callback order");

    callbacks[0] = '\0'; callback_count = 0;
    runGameMode(GM_HANYU_CSS);
    check(releases == 1 && resets == 1 && strcmp(callbacks, "BU") == 0,
          "another major with the same view does not reset archives");

    callbacks[0] = '\0'; callback_count = 0;
    next_view = 0;
    next = runGameMode(GM_HANYU_CSS);
    runGameMode(next);
    check(releases == 2 && resets == 2 && strcmp(callbacks, "BUDRAU") == 0,
          "returning to the mod view resets once after the online major unload");

    callbacks[0] = '\0'; callback_count = 0;
    mu_content_begin_major(GM_TRAINING);
    mu_content_begin_major(GM_TRAINING);
    check(releases == 2 && resets == 2 && callback_count == 0,
          "repeated queries of the applied view are inert");

    callbacks[0] = '\0'; callback_count = 0;
    next_view = 1;
    runGameMode(GM_TRAINING);
    runGameMode(GM_HANYU_CSS);
    check(releases == 3 && resets == 3 && host_view == 1,
          "another online handoff establishes the retail preload lifetime");
    callbacks[0] = '\0'; callback_count = 0;
    runGameMode(GM_MENU);
    check(releases == 4 && resets == 4 && strcmp(callbacks, "DRMU") == 0 && host_view == 0,
          "online-to-menu return selects mod view and resets before any menu load");

    callbacks[0] = '\0'; callback_count = 0;
    next_view = 1;
    runGameMode(GM_TRAINING);
    runGameMode(GM_DEBUG_VS);
    check(releases == 5 && resets == 5 && host_view == 1,
          "online DebugVS harness retains its explicitly requested retail view");
    return failures ? 1 : 0;
}
