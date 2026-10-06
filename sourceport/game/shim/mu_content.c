/* Content views (Source Port mods).
 *
 * With a mod profile loaded (Akaneia or any other pack), the game can show two views of the disc:
 * the mod's files, or the retail files. The host serves them (source_host.cpp); the game says which
 * online mode the player picked, and the host answers with the view:
 *   offline modes                  the mod's files
 *   Unranked, Teams, Party         the retail files, always (vanilla gameplay online)
 *   Direct                         the mod's files when the player uses the mod in Direct,
 *                                  otherwise the retail files; the netplay build check then makes
 *                                  sure the opponent is on the same build
 * The switch happens between scenes, before the next major mode loads its files, so the m-ex
 * runtime (mu_mex.c) and every table follow it at that mode's load. Without a mod profile the answer
 * is always the retail view and nothing changes.
 *
 * The view is host-coupled state: it is never part of a savestate or rollback snapshot. */
#include <dolphin/os.h>
#include <melee/gm/forward.h>
#include <melee/lb/lbdvd.h>

#include "mu_native.h"

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);

#define CMD_CONTENT_MODE 0xF5   /* payload: online mode (0-4) or 0xFF offline; reply: 1 = retail view */

static int content_vanilla = -1;   /* -1: not asked yet (the host starts in the mod view) */
/* The view used by the current preload lifetime. Menu selection and the host's practice handoff
 * can update content_vanilla while the previous major still runs its unload callbacks. */
static int preload_view = -1;

int mu_content_mode(int online_mode)
{
    static unsigned char response[4096];   /* the host's minimum response capacity */
    unsigned char payload = online_mode < 0 ? 0xFF : (unsigned char) online_mode;
    unsigned int got = 0;
    if (mu_online_abi_command(CMD_CONTENT_MODE, &payload, 1, response, sizeof response, &got) != 0 ||
        got < 1)
    {
        content_vanilla = 0;   /* an older host: one view only */
        return 0;
    }
    if (content_vanilla != (int) response[0]) {
        OSReport("[content] %s view (%s)\n", response[0] ? "retail" : "mod",
                 online_mode < 0 ? "offline" : online_mode == 2 ? "Direct" : "Unranked, Teams or Party");
    }
    content_vanilla = response[0];
    return content_vanilla;
}

/* The view the host serves now (a launcher or harness match sets it on the host side, without the
 * online menu), asked at each major mode load. */
int mu_content_vanilla(void)
{
    static unsigned char response[4096];
    unsigned char payload = 0xFE;   /* query only */
    unsigned int got = 0;
    if (mu_online_abi_command(CMD_CONTENT_MODE, &payload, 1, response, sizeof response, &got) == 0 && got >= 1) {
        content_vanilla = response[0];
    }
    return content_vanilla > 0;
}

void mu_content_begin_major(int mode)
{
    int view;
    /* The menu's ordinary return-from-online prep runs after m-ex and major loads. Restore its
     * offline view here so both registries and its first archive reads see the right lifetime.
     * Other majors may be explicit online harness destinations (notably GM_DEBUG_VS). */
    if (mode == GM_MENU) {
        mu_content_mode(-1);
    }
    view = mu_content_vanilla();
    if (preload_view >= 0 && preload_view != view) {
        /* The previous major has finished unloading. Release every preloaded archive, including
         * files unchanged by the mod: removing changed files can compact their shared heap, while
         * parsed HSD_Archive objects still contain pointers into the previous raw-data location.
         * Release quiesces pending loads before destroying their heaps; metadata reset alone would
         * leak allocations and leave callbacks pointing at reused cache entries. The next major
         * reinstates its preload flag and loads the common files normally. */
        lbDvd_80018CF4(0);
        lbDvd_80018F68();
        OSReport("[content] preload archives reset for %s view\n", view ? "retail" : "mod");
    }
    preload_view = view;
}

/* Character select, L / R on a highlighted costume (mn/mncharsel.c): the host steps that costume
 * slot through its installed skins (the standard costume, then each skin in the catalog's order),
 * saves the pick like a Mods tab choice and serves the slot's file from the new skin under a new
 * entry number. Returns 1 when the skin changed; name gets the choice's label ("Standard", the
 * skin's or its pack's name), NUL-terminated. An older host, a replay, an online match that is
 * queued or running, or a skin that is not allowed online: 0, nothing changed. */
#define CMD_SKIN_GROWTH 0xFC   /* no payload; reply: u32 big endian */

/* The title demo preloads four fighters and a stage into a heap of a fixed size (heap 4, 0x64B400
 * bytes), sized for the original files: the largest original set leaves about 113 KB. A stage skin
 * a megabyte or more larger than the file it replaces no longer fits with most fighter draws, and
 * the game stops with lbmemory.c:233. gm_801BF684 (gm/gmopeningmode.c) passes the stage it drew
 * through here: when that stage's file cannot fit beside the largest original fighter set plus
 * the growth of the installed fighter skins, another stage of the game's own demo list that does
 * fit is returned in its place. With original files every stage fits and nothing changes. */
int mu_title_demo_stage(int stkind)
{
    extern const char* mu_ground_stage_file(int grkind);
    extern int Stage_8022519C(int stkind);
    extern int gm_801641CC(unsigned char index);
    extern unsigned int lbFileGetSize(const char* basename);
    static unsigned char response[64];
    unsigned int got = 0;
    unsigned int growth = 0;
    unsigned int budget;
    const char* file;
    unsigned int size;
    int i;
    if (stkind < 0 || stkind >= 0x11E) {
        return stkind;
    }
    if (mu_online_abi_command(CMD_SKIN_GROWTH, NULL, 0, response, sizeof response, &got) == 0 && got >= 4) {
        growth = ((unsigned int) response[0] << 24) | ((unsigned int) response[1] << 16) |
                 ((unsigned int) response[2] << 8) | response[3];
    }
    if (4170176u + growth >= 0x64B400u) {
        return stkind;   /* nothing would fit: leave the draw alone */
    }
    budget = 0x64B400u - 4170176u - growth;
    file = mu_ground_stage_file(Stage_8022519C(stkind));
    if (file == NULL) {
        return stkind;
    }
    size = lbFileGetSize(file);
    if (((size + 31u) & ~31u) + 0x60u <= budget) {
        return stkind;
    }
    for (i = 0; i < 0x1D; i++) {
        const int other = gm_801641CC((unsigned char) i);
        const char* name;
        if (other == stkind || other < 0 || other >= 0x11E) {
            continue;
        }
        name = mu_ground_stage_file(Stage_8022519C(other));
        if (name == NULL) {
            continue;
        }
        size = lbFileGetSize(name);
        if (size != 0 && ((size + 31u) & ~31u) + 0x60u <= budget) {
            OSReport("[mods] the title demo drew a stage whose skin does not fit its memory; stage %d plays instead of %d\n",
                     other, stkind);
            return other;
        }
    }
    return stkind;
}

#define CMD_STAGE_SKIN_CYCLE 0xFB   /* payload: 1 next / 0 previous, then the stage's file name */

/* Called after old preloads have quiesced, before this new match preloads its stage.
 * This boundary is outside rollback; Sudden Death retains the existing match's skin. */
void mu_stage_skin_match(int stkind)
{
    extern int Stage_8022519C(int stkind);
    extern const char* mu_ground_stage_file(int grkind);
    static unsigned char response[4096];
    unsigned int got = 0, n = 0;
    const char* file;
    if (stkind < 0 || stkind >= 0x148) return;
    file = mu_ground_stage_file(Stage_8022519C(stkind));
    if (file == NULL) return;
    while (n < 63 && file[n] != '\0') n++;
    if (!n || file[n] != '\0') return;
    if (mu_online_abi_command(0xFD, (const unsigned char*) file, n + 1, response, sizeof response, &got) == 0 &&
        got >= 1 && response[0]) lbDvd_8001823C();
}

/* Stage select, X / Y or R / L on a highlighted stage (mn/mnstagesel.c): the host steps that
 * stage's file through the standard stage and its installed stage skins and serves the file from
 * the pick under a new entry number. Returns 1 when it changed. Offline only (the host refuses
 * in an online session and in a replay). */
int mu_stage_skin_cycle(const char* file, int next)
{
    static unsigned char response[4096];
    unsigned char payload[65];
    unsigned int got = 0;
    unsigned int n = 0;
    if (file == NULL) {
        return 0;
    }
    payload[0] = next ? 1 : 0;
    while (n < 63 && file[n] != '\0') {
        payload[1 + n] = (unsigned char) file[n];
        n++;
    }
    payload[1 + n] = 0;
    if (mu_online_abi_command(CMD_STAGE_SKIN_CYCLE, payload, n + 2, response, sizeof response, &got) != 0 ||
        got < 1 || response[0] == 0)
    {
        return 0;
    }
    return 1;
}

#define CMD_SKIN_CYCLE 0xF9   /* payload: port, fighter (character select number), costume, 1 next / 0 previous */

int mu_skin_cycle(int port, int char_kind, int costume, int next, char* name, int capacity)
{
    static unsigned char response[4096];
    unsigned char payload[4];
    unsigned int got = 0;
    unsigned int i;
    if (name != NULL && capacity > 0) {
        name[0] = '\0';
    }
    payload[0] = (unsigned char) port;
    payload[1] = (unsigned char) char_kind;
    payload[2] = (unsigned char) costume;
    payload[3] = next ? 1 : 0;
    if (mu_online_abi_command(CMD_SKIN_CYCLE, payload, sizeof payload, response, sizeof response, &got) != 0 ||
        got < 1 || response[0] == 0)
    {
        return 0;
    }
    if (name != NULL && capacity > 0) {
        for (i = 0; i + 1 < got && (int) i + 1 < capacity; i++) {
            name[i] = (char) response[i + 1];
        }
        name[i] = '\0';
    }
    OSReport("[content] skin for fighter %d costume %d: %s\n", char_kind, costume,
             name != NULL && capacity > 0 ? name : "");
    return 1;
}

MU_EXCLUSIONS(content, MU_EXCLUDE(content_vanilla), MU_EXCLUDE(preload_view))
