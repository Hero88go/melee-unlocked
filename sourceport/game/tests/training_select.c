/* Exercise the production Training select exit with an absent CPU after an online match. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int32_t s32;
typedef uint64_t u64;
typedef struct { s8 ckind; u8 color, nametag, cpu_kind, slot_type, slot; } Player;
typedef struct { struct { Player players[4]; } start; } VsModeData;
typedef struct { int pending_scene_change; } CSSData;
typedef struct { CSSData css; } GameModeState;
struct GameCache { struct { s8 char_id; u8 color; } entries[4]; };
static struct { struct GameCache game_cache; } preload;
static struct { struct { VsModeData table[1]; } modes; } main_data;
static __typeof__(main_data)* gmMainLib_804D3EE0 = &main_data;
enum { GmVsMode_Training, GM_MENU = 1 };
static int gm_804D68C0, returned_to_menu;
static s8 selected_human, selected_cpu;
static u8 selected_color;
#define PAD_STACK(n)
#define MU_NATIVE 1
static CSSData* gm_GetGameModeStateExitData(GameModeState* state) { return &state->css; }
static void gm_ChangeGameModeAfterCurrentScene(int mode) { returned_to_menu = mode; }
static void gm_SetupAllPlayerDefaults(Player* players) { memset(players, 0, 4 * sizeof(*players)); }
static void gm_801B0730(CSSData* css, s8* kind, void* unused, u8* color, u8* tag, void* level)
{ (void)css; (void)unused; (void)tag; (void)level; *kind = selected_human; *color = selected_color; }
static void gm_801B07E8_layer(CSSData* css, s8* kind, void* unused, s8* color, s8* tag, void* level)
{ (void)css; (void)unused; (void)tag; (void)level; *kind = selected_cpu; *color = 0; }
static u8 gm_GetNumCostumesForCKind(int kind) { return kind >= 0 && kind < 26 ? 5 : 0; }
static void OSReport(const char* format, ...) { (void)format; }
static __typeof__(preload)* lbDvd_GetPreloadCacheScene(void) { return &preload; }
static void lbDvd_80018254(void) {}
static u64 lbAudioAx_80026E84(int kind) { (void)kind; return 0; }
static void lbAudioAx_80026F2C(int unused) { (void)unused; }
static void lbAudioAx_8002702C(int unused, u64 mask) { (void)unused; (void)mask; }
static void lbAudioAx_80027168(void) {}

#include "training_select_body.h"

int main(void)
{
    GameModeState state = {0};
    for (int human = 0; human < 26; ++human) {
        for (int cpu = -1; cpu <= 26; ++cpu) {
            for (int port = 0; port < 4; ++port) {
                selected_human = human; selected_cpu = cpu; selected_color = 3;
                gm_804D68C0 = port;
                gm_801B1C24(&state);
                const Player* players = main_data.modes.table[0].start.players;
                assert(players[0].ckind == human);
                assert(players[1].ckind == (cpu < 0 || cpu >= 26 ? human : cpu));
                for (int i = 1; i < 4; ++i) {
                    assert(players[i].color < gm_GetNumCostumesForCKind(players[i].ckind));
                    assert(players[i].color != players[0].color);
                }
                assert(preload.game_cache.entries[2].char_id == players[2].ckind);
                assert(preload.game_cache.entries[3].char_id == players[3].ckind);
            }
        }
    }
    state.css.pending_scene_change = 2;
    gm_801B1C24(&state);
    assert(returned_to_menu == GM_MENU);
    return 0;
}
