/* Triples: VS matches with five or six players (3v3, 2v2v2, free-for-all, five players).
 *
 * Native C port of digicannon's Triples patches, https://github.com/digicannon/ssbm-triples-asm (GPL-3.0),
 * each at the game function the patch hooks and with the patch's own values. The patches exist only
 * because a console holds four pads and four fighters' files; where the native game already has room
 * (six pad entries, six HUD positions, the preload cache) it is used as is. File by file:
 *   match_store_player_count.asm      mu_triples_match_enter (gm_Scene_Vs_OnEnter)
 *   always_show_player_indicator.asm  mu_triples_indicators (fn_8016CD98, each match frame)
 *   disable_screen_shake.asm          mu_triples_no_quake (Camera_RequestQuake)
 *   transparent_hud.asm               mu_triples_hud_fade (mu_hud_fade, UnclePunch's HUD Transparency)
 *   pal_stock_icons.asm               mu_triples_active (ifStock_802F98E8, PAL Stock Icons)
 *   hide_nametag_when_invisible.asm   mu_triples_active (ifnametag.c, in team matches too)
 *   neutral_spawn.asm                 mu_triples_spawn (fn_8016E2BC) with mu_triples_spawns.h
 *   fix_results_screen.asm            mu_triples_match_exit (gm_Scene_Vs_OnExit)
 * The Triples idea and these patches: digicannon. */
#include <melee/gm/gmvs.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/types.h>
#include <melee/gr/stage.h>
#include <melee/pl/player.h>
#include <dolphin/os.h>
#include <string.h>

#include "mu_native.h"
#include "mu_triples_spawns.h"
#include "mu_test_env.h"
#include <stdio.h>

static int tri_camera_logged;     /* the wider camera was reported for this match */
static int tri_players;            /* players in the VS match being played, 0 outside one */
static unsigned int tri_fade_frames;   /* frames of flicker since the alive count fell to four */

unsigned int mu_triples_flags(void);   /* shim/mu_entry.c: MuHostApi.triples_flags, the 5-6 player options */

int mu_triples_players(void)
{
    return tri_players;
}

int mu_triples_active(void)
{
    return tri_players >= 5;
}

/* match_store_player_count.asm: the number of slots with a player, as the match begins. */
void mu_triples_match_enter(const StartMeleeData* start)
{
    int i, n = 0;
    for (i = 0; i < 6; i++) {
        if (start->players[i].slot_type != Gm_PKind_NA) {
            n++;
        }
    }
    tri_players = n;
    tri_fade_frames = 0;
    tri_camera_logged = 0;
    mu_triples_art_reset();   /* the match's heap is new */
    if (n >= 5) {
        OSReport("[triples] %d players\n", n);
    }
}

/* The HUD layout for the match: the game's own six-player layout (ifAll_802F343C kinds 5 and 6, the
 * percents at 0.65 scale, Multi-Man Melee's) once there are five or six players. */
int mu_triples_hud_layout(int kind)
{
    return mu_triples_active() ? 6 : kind;
}

/* always_show_player_indicator.asm: with more than four players alive the P1-P6 markers stay up; when
 * the count falls to four they flicker for two seconds (every other frame) and then follow the game's own
 * rules again. The patch writes the six "show" flags (un_804D6D70) every frame. */
void mu_triples_indicators(unsigned int frame)
{
    extern void mu_ifnametag_force(int slot, int on);   /* if/ifnametag.c */
    int i, alive = 0, on;
    if (!mu_triples_active() || !(mu_triples_flags() & MU_TRIPLES_TAGS)) {
        return;
    }
    for (i = 0; i < 6; i++) {
        if (Player_GetPlayerSlotType(i) != Gm_PKind_NA && Player_GetStocks(i) > 0) {
            alive++;
        }
    }
    if (alive >= 5) {
        tri_fade_frames = 0;
        on = 1;
    } else if (tri_fade_frames < 120) {
        tri_fade_frames++;
        on = (int) (frame & 1);
    } else {
        on = 0;
    }
    for (i = 0; i < 6; i++) {
        mu_ifnametag_force(i, on);
    }
}

/* disable_screen_shake.asm: no camera quakes in a match of five or six (the patch: team matches; owner
 * 11-27: every five- or six-player match). Fixed by the player count, so every machine in a match agrees. */
int mu_triples_no_quake(void)
{
    return mu_triples_active();
}

/* transparent_hud.asm: UnclePunch's HUD Transparency, on for matches of five or six. */
int mu_triples_hud_fade(void)
{
    return mu_triples_active() && (mu_triples_flags() & MU_TRIPLES_HUD_FADE) != 0;
}

/* neutral_spawn.asm: each player starts at the neutral point for their place among the players, team by
 * team, from the patch's table (mu_triples_spawns.h: six points for free-for-all, six for teams, the
 * first team's three on the left). Stages without a row use the stage's own points (teams 0, 3, 1, 2).
 * The entry is staggered by five frames a place (Player_SetUnk4C), the facing points to the middle.
 * Two teams of uneven size (3v2) keep each team on its own side; three teams (2v2v2) use the
 * free-for-all points, which the patch never had to place. Returns 0 when it did not place the slot. */
int mu_triples_spawn(int slot, int is_teams)
{
    static const u8 not_found_teams[4] = { 0, 3, 1, 2 };
    int n = 0, place = -1, team, s, row;
    int team_size[3] = { 0, 0, 0 }, teams_present = 0, side = 0, in_team = 0;
    int stage;
    Vec3 pos;

    if (!mu_triples_active() || gm_IsCurrently1PMode_inline()) {
        return 0;
    }
    for (s = 0; s < 6; s++) {
        if (Player_GetPlayerSlotType(s) != Gm_PKind_NA) {
            team = is_teams ? Player_GetTeam(s) : 0;
            if (team >= 0 && team < 3) team_size[team]++;
        }
    }
    for (team = 0; team < 3; team++) {
        if (team_size[team] > 0) teams_present++;
    }
    if (is_teams && teams_present == 2) {
        for (team = 0; team < 3; team++) {
            if (team_size[team] == 0) continue;
            in_team = 0;
            for (s = 0; s < 6; s++) {
                if (Player_GetPlayerSlotType(s) == Gm_PKind_NA || Player_GetTeam(s) != team) continue;
                if (s == slot) place = side * 3 + (in_team < 3 ? in_team : 2);
                in_team++;
            }
            side++;
        }
    } else {
        is_teams = 0;
        for (s = 0; s < 6; s++) {
            if (Player_GetPlayerSlotType(s) == Gm_PKind_NA) continue;
            if (s == slot) place = n;
            n++;
        }
    }
    if (place < 0) {
        return 0;
    }
    stage = (int) Stage_80225194();
    for (row = 0; row < (int) (sizeof mu_triples_spawns / sizeof mu_triples_spawns[0]); row++) {
        if (mu_triples_spawns[row].stage == stage) break;
    }
    if (row < (int) (sizeof mu_triples_spawns / sizeof mu_triples_spawns[0])) {
        const float* p = is_teams ? mu_triples_spawns[row].teams[place] : mu_triples_spawns[row].singles[place];
        pos.x = p[0];
        pos.y = p[1];
        pos.z = 0.0f;
    } else {
        /* The patch's fallback holds four entries ({0, 3, 1, 2}: the first team on the stage's points 0
         * and 3, the second on 1 and 2); a team's third player shares its first player's point. These
         * are the stages the stage select marks red (unfair starts for players 5 and 6). */
        static const u8 team_place[6] = { 0, 1, 0, 2, 3, 2 };
        int point = is_teams ? not_found_teams[team_place[place]] : (place & 3);
        Stage_80224E64(point, &pos);
    }
    Player_80032768(slot, &pos);
    Player_LoadPlayerCoords(slot, &pos);
    Player_SetFacingDirection(slot, pos.x > 0.0f ? -1.0F : 1.0F);
    Player_SetUnk4C(slot, (u8) (place * 5));
    return 1;
}

/* fix_results_screen.asm: the results screen shows four panels. Every placement is kept within 1st-4th;
 * a winning player 5 or 6 takes a panel (free-for-all: player 1's for player 5, player 2's for player 6;
 * teams: an empty panel, else a losing player's), shown as a CPU so nobody has to press START for it;
 * then players 5 and 6 are cleared, which the results screen needs. */
void mu_triples_match_exit(MatchEnd* end)
{
    MatchPlayerData* p = end->player_standings;
    int i;
    if (tri_players >= 5) {
        for (i = 0; i < 6; i++) {
            if (p[i].is_big_loser >= 4) {        /* the placement byte (0 = 1st) */
                p[i].is_big_loser = 3;
                p[i].is_small_loser = 3;
            }
        }
        /* A game nobody won (no contest) shows the four as they are: everyone's placement is 0 then,
         * and the patch's copy would put player 5 in player 1's place. */
        if (end->outcome != OUTCOME_TIMEOUT && end->outcome != OUTCOME_ELIMINATION &&
            end->outcome != OUTCOME_TEAM_ELIMINATION)
        {
            /* nothing to copy */
        } else if (!end->is_teams) {
            if (p[4].pkind != Gm_PKind_NA && p[4].is_big_loser == 0) {
                p[0] = p[4];
                p[0].pkind = Gm_PKind_Cpu;
            } else if (p[5].pkind != Gm_PKind_NA && p[5].is_big_loser == 0) {
                p[1] = p[5];
                p[1].pkind = Gm_PKind_Cpu;
            }
        } else {
            int winning_team = 0, replace = -1;
            for (i = 0; i < 6; i++) {
                if (p[i].pkind != Gm_PKind_NA && p[i].is_big_loser == 0) {
                    winning_team = p[i].team;
                    break;
                }
            }
            for (i = 0; i < 4; i++) {
                if (p[i].pkind == Gm_PKind_NA) {
                    replace = i;
                    break;
                }
                if (replace < 0 && p[i].team != winning_team) {
                    replace = i;
                }
            }
            if (replace >= 0) {
                for (i = 4; i < 6; i++) {
                    if (p[i].pkind != Gm_PKind_NA && p[i].team == winning_team) {
                        p[replace] = p[i];
                        p[replace].pkind = Gm_PKind_Cpu;
                        break;
                    }
                }
            }
        }
        for (i = 4; i < 6; i++) {
            memset(&p[i], 0, sizeof p[i]);
            p[i].pkind = Gm_PKind_NA;
        }
    }
    tri_players = 0;
}

/* ---- P5 and P6 art (the select screen's cards, hands and coins; the match's markers) ----
 * The game's art goes to P4. "P5" and "P6" are made at run time from the same texture's own "P3", in its own
 * strokes: a 5 is a 3 with its upper side mirrored (the vertical on the left), a 6 is that 5 with the lower half
 * made symmetric (a closed bowl). Only the digit's own texels are read and written; the "P" stays as it is. */
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/memory.h>
#include <melee/lb/lbspdisplay.h>

typedef struct MuTriGlyph { const HSD_ImageDesc* src; int digit; HSD_ImageDesc* out; } MuTriGlyph;
static MuTriGlyph tri_glyphs[16];
static int tri_nmarkers;   /* the match's P5/P6 marker images (below) */
static int tri_nglyphs;
static u8 tri_mask[64 * 64];
static short tri_stack[64 * 64];

/* A scene's heap goes with the scene: the select screen and each match start a new list. */
void mu_triples_art_reset(void)
{
    tri_nglyphs = 0;
    tri_nmarkers = 0;
}

/* One texel: I4 and CI4 4-bit tiles 8x8; I8, IA4, CI8 8-bit tiles 8x4. */
static int tri_px_at(unsigned fmt, unsigned w, int x, int y, int* nibble)
{
    const int tw = 8, th = (fmt == 0 || fmt == 8) ? 8 : 4;
    const int tiles_x = ((int) w + tw - 1) / tw;
    const int tile = (y / th) * tiles_x + (x / tw);
    const int px = (y % th) * tw + (x % tw);
    if (fmt == 0 || fmt == 8) {
        *nibble = px & 1;
        return tile * 32 + px / 2;
    }
    *nibble = -1;
    return tile * 32 + px;
}
static int tri_px_get(const u8* bits, unsigned fmt, unsigned w, int x, int y)
{
    int nib;
    const int at = tri_px_at(fmt, w, x, y, &nib);
    return nib < 0 ? bits[at] : nib == 0 ? bits[at] >> 4 : bits[at] & 15;
}
static void tri_px_set(u8* bits, unsigned fmt, unsigned w, int x, int y, int v)
{
    int nib;
    const int at = tri_px_at(fmt, w, x, y, &nib);
    if (nib < 0) bits[at] = (u8) v;
    else if (nib == 0) bits[at] = (u8) ((bits[at] & 0x0F) | (v << 4));
    else bits[at] = (u8) ((bits[at] & 0xF0) | (v & 15));
}
/* How strongly a texel is drawn: I4 its value, IA4 its alpha nibble, I8 its top nibble. */
static int tri_ink(unsigned fmt, int v)
{
    return fmt == 0 ? v : (v >> 4);
}

/* The label may be stored upright or turned a quarter (the match's markers run top to bottom): the digit is
 * worked on in an upright view. Orientation 0: as stored; 1: image x = w-1-vy, y = vx; 2: image x = vy, y = h-1-vx. */
static int tri_o;
static unsigned tri_w, tri_h;
static void tri_v2i(int vx, int vy, int* x, int* y)
{
    if (tri_o == 0) { *x = vx; *y = vy; }
    else if (tri_o == 1) { *x = (int) tri_w - 1 - vy; *y = vx; }
    else { *x = vy; *y = (int) tri_h - 1 - vx; }
}
static int tri_vget(const u8* bits, unsigned fmt, int vx, int vy)
{
    int x, y;
    tri_v2i(vx, vy, &x, &y);
    return tri_px_get(bits, fmt, tri_w, x, y);
}
static void tri_vset(u8* bits, unsigned fmt, int vx, int vy, int v)
{
    int x, y;
    tri_v2i(vx, vy, &x, &y);
    tri_px_set(bits, fmt, tri_w, x, y, v);
}

/* The digit's shape and bars in the current orientation; 1 when it reads as a 3 (three bars, spine on the right). */
static int tri_find_three(const u8* in, unsigned fmt, int W, int H, int* x0p, int* x1p, int band_lo[3], int band_hi[3])
{
    int x, y, i, ncomp = 0, best = -1, best_lr = -1, x0 = -1, x1 = -1, nbands = 0, left_ink = 0, right_ink = 0;
    int lo[4], hi[4];
    memset(tri_mask, 0, sizeof tri_mask);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int sp = 0, size = 0, l = x, r = x;
            if (tri_mask[y * 64 + x] || tri_ink(fmt, tri_vget(in, fmt, x, y)) <= 1 || ncomp == 255) continue;
            tri_mask[y * 64 + x] = (u8) ++ncomp;
            tri_stack[sp++] = (short) (y * 64 + x);
            while (sp > 0) {
                static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                const int at = tri_stack[--sp], cx = at % 64, cy = at / 64;
                int n;
                size++;
                if (cx < l) l = cx;
                if (cx > r) r = cx;
                for (n = 0; n < 4; n++) {
                    const int nx = cx + dx[n], ny = cy + dy[n];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H || tri_mask[ny * 64 + nx]) continue;
                    if (tri_ink(fmt, tri_vget(in, fmt, nx, ny)) <= 1) continue;
                    tri_mask[ny * 64 + nx] = (u8) ncomp;
                    tri_stack[sp++] = (short) (ny * 64 + nx);
                }
            }
            if (size >= 12 && l + r > best_lr) {
                best_lr = l + r;
                best = ncomp;
                x0 = l;
                x1 = r;
            }
        }
    }
    if (best < 0 || x1 <= x0) return 0;
    for (i = 0; i < 64 * 64; i++) tri_mask[i] = tri_mask[i] == best;
    {
        int prev = 0;
        for (y = 0; y < H; y++) {
            int n = 0, bar;
            for (x = x0; x <= x1; x++) n += tri_mask[y * 64 + x];
            bar = n * 10 >= (x1 - x0 + 1) * 6;
            if (bar && !prev) {
                if (nbands == 4) return 0;
                lo[nbands] = y;
                hi[nbands++] = y;
            }
            if (bar) hi[nbands - 1] = y;
            prev = bar;
        }
    }
    if (nbands != 3) return 0;
    /* a 3's spine is on its right: between the bars, more of it on the right half than the left */
    for (y = hi[0] + 1; y < lo[2]; y++) {
        for (x = x0; x <= x1; x++) {
            if (!tri_mask[y * 64 + x]) continue;
            if (x * 2 < x0 + x1) left_ink++;
            else right_ink++;
        }
    }
    if (right_ink <= left_ink) return 0;
    *x0p = x0;
    *x1p = x1;
    for (i = 0; i < 3; i++) {
        band_lo[i] = lo[i];
        band_hi[i] = hi[i];
    }
    return 1;
}

void* mu_triples_digit(const void* image_desc, int digit)
{
    const HSD_ImageDesc* src = image_desc;
    const unsigned fmt = src != NULL ? src->format : 99u;
    unsigned bytes;
    const u8* in;
    u8* out;
    HSD_ImageDesc* desc;
    int i, x, y, x0 = -1, x1 = -1, W = 0, H = 0, found = 0;
    int band_lo[3], band_hi[3];
    if (src == NULL || (fmt != 0 && fmt != 1 && fmt != 2) || DP(src->image_ptr) == NULL || (digit != 5 && digit != 6)) {
        return NULL;
    }
    for (i = 0; i < tri_nglyphs; i++) {
        if (tri_glyphs[i].src == src && tri_glyphs[i].digit == digit) return tri_glyphs[i].out;
    }
    tri_w = src->width;
    tri_h = src->height;
    if (tri_w > 64 || tri_h > 64) return NULL;
    in = DP(src->image_ptr);
    for (tri_o = 0; tri_o < 3 && !found; tri_o++) {
        W = tri_o == 0 ? (int) tri_w : (int) tri_h;
        H = tri_o == 0 ? (int) tri_h : (int) tri_w;
        found = tri_find_three(in, fmt, W, H, &x0, &x1, band_lo, band_hi);
        if (found) break;
    }
    if (!found) {
        OSReport("[triples] no 3 to make a %d from (%ux%u)\n", digit, tri_w, tri_h);
        return NULL;
    }
    bytes = fmt == 0 ? ((tri_w + 7) / 8 * 8) * ((tri_h + 7) / 8 * 8) / 2 : ((tri_w + 7) / 8 * 8) * ((tri_h + 3) / 4 * 4);
    out = HSD_MemAlloc(bytes);
    memcpy(out, in, bytes);
    for (y = band_hi[0]; y <= band_lo[1]; y++) {          /* 3 -> 5: the upper side mirrored */
        for (x = x0; x <= x1; x++) {
            const int m = x0 + x1 - x;
            if (!tri_mask[y * 64 + x] && tri_ink(fmt, tri_vget(in, fmt, x, y)) > 1) continue;   /* the P */
            tri_vset(out, fmt, x, y, tri_mask[y * 64 + m] ? tri_vget(in, fmt, m, y) : 0);
        }
    }
    if (digit == 6) {                                     /* 5 -> 6: below the middle bar, left mirrors right */
        const int cx = (x0 + x1) / 2;
        for (y = band_lo[1] + 1; y <= band_lo[2]; y++) {
            for (x = x0; x <= cx; x++) {
                const int m = x0 + x1 - x;
                if (!tri_mask[y * 64 + x] && tri_ink(fmt, tri_vget(in, fmt, x, y)) > 1) continue;
                tri_vset(out, fmt, x, y, tri_mask[y * 64 + m] ? tri_vget(in, fmt, m, y) : 0);
            }
        }
    }
    (void) W;
    (void) H;
    desc = HSD_MemAlloc(sizeof *desc);
    memcpy(desc, src, sizeof *desc);
    desc->image_ptr.raw = (u32) (uintptr_t) out;
    if (tri_nglyphs < (int) (sizeof tri_glyphs / sizeof tri_glyphs[0])) {
        tri_glyphs[tri_nglyphs].src = src;
        tri_glyphs[tri_nglyphs].digit = digit;
        tri_glyphs[tri_nglyphs++].out = desc;
    }
    return desc;
}

/* Tests only: MELEE_TEST_CSS_TEXDUMP=<dir> writes every texture image of a joint tree (each frame of a texture
 * animation too) as raw GameCube texture bytes, <dir>/<tag>_<joint>_<dobj>_<tobj>_<frame>_f<fmt>_<w>x<h>.bin, and
 * its palette beside it. The game's own headers declare no file output: Windows' calls, for this dump only. */
#ifdef _WIN32
__attribute__((stdcall)) void* CreateFileA(const char*, unsigned, unsigned, void*, unsigned, unsigned, void*);
__attribute__((stdcall)) int WriteFile(void*, const void*, unsigned, unsigned*, void*);
__attribute__((stdcall)) int CloseHandle(void*);
#endif
static unsigned tri_tex_bytes(unsigned fmt, unsigned w, unsigned h)
{
    unsigned tw = 4, th = 4, bpp = 16;
    switch (fmt) {
    case 0: case 8: case 14: tw = 8; th = 8; bpp = 4; break;
    case 1: case 2: case 9: tw = 8; th = 4; bpp = 8; break;
    case 6: bpp = 32; break;
    default: break;
    }
    return ((w + tw - 1) / tw * tw) * ((h + th - 1) / th * th) * bpp / 8;
}
static void tri_tex_put(const char* path, const void* p, unsigned n)
{
#ifdef _WIN32
    unsigned done = 0;
    void* f = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0);
    if (f == (void*) (long long) -1 || f == 0) return;
    WriteFile(f, p, n, &done, 0);
    CloseHandle(f);
#else
    (void) path; (void) p; (void) n;   /* the dump is a Windows test tool; the Linux game has none */
#endif
}
static void tri_tex_write(const char* dir, const char* tag, int joint, int d, int t, int frame, HSD_ImageDesc* img,
                          const void* lut, unsigned lut_entries)
{
    char path[512];
    void* bits;
    if (img == NULL || (bits = DP(img->image_ptr)) == NULL) return;
    sprintf(path, "%s/%s_%d_%d_%d_%d_f%u_%ux%u.bin", dir, tag, joint, d, t, frame, (unsigned) img->format,
             (unsigned) img->width, (unsigned) img->height);
    tri_tex_put(path, bits, tri_tex_bytes(img->format, img->width, img->height));
    if (lut != NULL && lut_entries > 0 && lut_entries <= 256) {
        sprintf(path, "%s/%s_%d_%d_%d_%d_pal.bin", dir, tag, joint, d, t, frame);
        tri_tex_put(path, lut, 2 * lut_entries);
    }
}
void mu_triples_tex_dump(void* root_jobj, const char* tag)
{
    const char* dir = mu_test_getenv("MELEE_TEST_CSS_TEXDUMP");
    HSD_JObj* j = root_jobj;
    int idx = 0;
    if (dir == NULL || *dir == '\0') return;
    while (j != NULL) {
        if (!(j->flags & (JOBJ_PTCL | JOBJ_SPLINE))) {
            HSD_DObj* dobj;
            int d = 0;
            for (dobj = j->u.dobj; dobj != NULL; dobj = dobj->next, d++) {
                HSD_TObj* tobj;
                int t = 0;
                if (dobj->mobj == NULL) continue;
                for (tobj = dobj->mobj->tobj; tobj != NULL; tobj = tobj->next, t++) {
                    int k;
                    OSReport("[texdump] %s joint %d dobj %d tobj %d fmt %u %ux%u table %d\n", tag, idx, d, t,
                             tobj->imagedesc ? (unsigned) tobj->imagedesc->format : 99u,
                             tobj->imagedesc ? (unsigned) tobj->imagedesc->width : 0u,
                             tobj->imagedesc ? (unsigned) tobj->imagedesc->height : 0u, tobj->imagetbl != NULL);
                    tri_tex_write(dir, tag, idx, d, t, -1, tobj->imagedesc, tobj->tlut != NULL ? tobj->tlut->lut : NULL,
                                  tobj->tlut != NULL ? tobj->tlut->n_entries : 0);
                    for (k = 0; tobj->imagetbl != NULL && k < 16; k++) {
                        DISC_PTR(HSD_ImageDesc)* tbl = (void*) tobj->imagetbl;
                        DISC_PTR(HSD_TlutDesc)* luts = (void*) tobj->tluttbl;
                        HSD_ImageDesc* img = DP(tbl[k]);
                        HSD_TlutDesc* lt = luts != NULL ? DP(luts[k]) : NULL;
                        const unsigned char* first = (const unsigned char*) DP(tbl[0]);
                        /* a table holds no count: entries in the same file as the first only */
                        if ((const unsigned char*) img < first - 0x400000 || (const unsigned char*) img > first + 0x400000) break;
                        if (lt != NULL && ((const unsigned char*) lt < first - 0x400000 ||
                                           (const unsigned char*) lt > first + 0x400000)) lt = NULL;
                        if (img == NULL || img->width == 0 || img->width > 1024 || img->height == 0 ||
                            img->height > 1024 || img->format > 14) break;
                        tri_tex_write(dir, tag, idx, d, t, k, img, lt != NULL ? DP(lt->lut) : NULL,
                                      lt != NULL ? lt->n_entries : 0);
                    }
                }
            }
        }
        idx++;
        if (!(j->flags & JOBJ_INSTANCE) && j->child != NULL) {
            j = j->child;
        } else {
            while (j != NULL && j->next == NULL) j = j->parent;
            if (j != NULL) j = j->next;
        }
    }
}

/* The match's P5/P6 markers (if/ifnametag.c). The marker's art draws "P" and the digit turned a quarter (they
 * read top to bottom) in a rounded hand that has no 5 or 6. The game's match timer draws its digits in that
 * hand (IA4 too): its own 5 and 6 take the place of the marker's 3, sized to the 3's box, turned as the marker
 * is, and given the marker's black outline. */
typedef struct MuTriMarker { const HSD_ImageDesc* src; int digit; HSD_ImageDesc* out; } MuTriMarker;
static MuTriMarker tri_markers[4];

static void* tri_marker_digit(const HSD_ImageDesc* p3, int digit)
{
    extern const void* mu_iftime_digit_image(int d);
    const HSD_ImageDesc* glyph = mu_iftime_digit_image(digit);
    const u8* in;
    const u8* gl;
    u8* out;
    HSD_ImageDesc* desc;
    int i, x, y, W, H, split = -1, best = 1 << 30, x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
    int gx0 = 1 << 30, gx1 = -1, gy0 = 1 << 30, gy1 = -1;
    unsigned bytes;
    float sc, ox, oy;
    if (p3 == NULL || glyph == NULL || p3->format != 2 || glyph->format != 2 || DP(p3->image_ptr) == NULL ||
        DP(glyph->image_ptr) == NULL || p3->width > 64 || p3->height > 64)
    {
        return NULL;
    }
    for (i = 0; i < tri_nmarkers; i++) {
        if (tri_markers[i].src == p3 && tri_markers[i].digit == digit) return tri_markers[i].out;
    }
    in = DP(p3->image_ptr);
    gl = DP(glyph->image_ptr);
    tri_w = p3->width;
    tri_h = p3->height;
    tri_o = tri_h > tri_w ? 1 : 0;   /* the marker's art runs top to bottom */
    W = tri_o ? (int) tri_h : (int) tri_w;
    H = tri_o ? (int) tri_w : (int) tri_h;
    /* "P" and the digit: split at the emptiest column (of bright texels) in the middle third */
    for (x = W * 35 / 100; x <= W * 65 / 100; x++) {
        int n = 0;
        for (y = 0; y < H; y++) {
            const int v = tri_vget(in, 2, x, y);
            n += (v >> 4) > 1 && (v & 15) > 6;
        }
        if (n < best) {
            best = n;
            split = x;
        }
    }
    /* the digit's box: everything drawn right of the split, its outline included */
    for (y = 0; y < H; y++) {
        for (x = split + 1; x < W; x++) {
            if ((tri_vget(in, 2, x, y) >> 4) > 1) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    for (y = 0; y < (int) glyph->height; y++) {
        for (x = 0; x < (int) glyph->width; x++) {
            if ((tri_px_get(gl, 2, glyph->width, x, y) >> 4) > 1) {
                if (x < gx0) gx0 = x;
                if (x > gx1) gx1 = x;
                if (y < gy0) gy0 = y;
                if (y > gy1) gy1 = y;
            }
        }
    }
    if (x1 <= x0 || y1 <= y0 || gx1 <= gx0 || gy1 <= gy0) return NULL;
    bytes = ((tri_w + 7) / 8 * 8) * ((tri_h + 3) / 4 * 4);
    out = HSD_MemAlloc(bytes);
    memcpy(out, in, bytes);
    /* the 3 out, the timer's digit in: as large as fits the 3's box (less a texel each side for the outline) */
    sc = (float) (x1 - x0 - 1) / (float) (gx1 - gx0 + 1);
    if ((float) (y1 - y0 - 1) / (float) (gy1 - gy0 + 1) < sc) sc = (float) (y1 - y0 - 1) / (float) (gy1 - gy0 + 1);
    ox = 0.5f * ((float) (x0 + x1) - sc * (float) (gx1 - gx0));
    oy = 0.5f * ((float) (y0 + y1) - sc * (float) (gy1 - gy0));
    for (y = y0; y <= y1; y++) {
        for (x = split + 1; x <= x1; x++) {
            const int gx = gx0 + (int) (((float) x - ox) / sc + 0.5f), gy = gy0 + (int) (((float) y - oy) / sc + 0.5f);
            int v = 0;
            if (gx >= gx0 && gx <= gx1 && gy >= gy0 && gy <= gy1) v = tri_px_get(gl, 2, glyph->width, gx, gy);
            tri_vset(out, 2, x, y, v);
        }
    }
    /* the marker's hand is bolder: the digit's white grows by a texel (into empty texels only) */
    {
        static u8 grow[64 * 64];
        memset(grow, 0, sizeof grow);
        for (y = y0; y <= y1; y++) {
            for (x = split + 1; x <= x1; x++) {
                int dx, dy;
                if ((tri_vget(out, 2, x, y) >> 4) > 1) continue;
                for (dy = -1; dy <= 1; dy++) {
                    for (dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx, ny = y + dy;
                        int v;
                        if ((dx != 0) == (dy != 0) || nx <= split || nx >= W || ny < 0 || ny >= H) continue;
                        v = tri_vget(out, 2, nx, ny);
                        if ((v >> 4) > 8 && (v & 15) > 8) grow[y * 64 + x] = 1;
                    }
                }
            }
        }
        for (y = y0; y <= y1; y++) {
            for (x = split + 1; x <= x1; x++) {
                if (grow[y * 64 + x]) tri_vset(out, 2, x, y, 0xFF);
            }
        }
    }
    /* the marker's outline: empty texels next to the digit's white become black */
    for (y = y0; y <= y1; y++) {
        for (x = split + 1; x <= x1; x++) {
            int dx, dy, near = 0;
            if ((tri_vget(out, 2, x, y) >> 4) > 1) continue;
            for (dy = -1; dy <= 1 && !near; dy++) {
                for (dx = -1; dx <= 1 && !near; dx++) {
                    const int nx = x + dx, ny = y + dy;
                    int v;
                    if (nx <= split || nx >= W || ny < 0 || ny >= H) continue;
                    v = tri_vget(out, 2, nx, ny);
                    near = (v >> 4) > 8 && (v & 15) > 8;
                }
            }
            if (near) tri_vset(out, 2, x, y, 0xF0);
        }
    }
    desc = HSD_MemAlloc(sizeof *desc);
    memcpy(desc, p3, sizeof *desc);
    desc->image_ptr.raw = (u32) (uintptr_t) out;
    if (tri_nmarkers < (int) (sizeof tri_markers / sizeof tri_markers[0])) {
        tri_markers[tri_nmarkers].src = p3;
        tri_markers[tri_nmarkers].digit = digit;
        tri_markers[tri_nmarkers++].out = desc;
    }
    return desc;
}

void mu_triples_marker(int slot, void* marker_jobj)
{
    static const GXColor colour[2] = { { 0xFF, 0x98, 0x26, 0xFF }, { 0x98, 0x4C, 0xE5, 0xFF } };
    HSD_JObj* label = NULL;
    HSD_DObj* d;
    HSD_TObj* t;
    HSD_ImageDesc* img;
    if (slot < 4 || slot > 5 || marker_jobj == NULL || Player_GetPlayerSlotType(slot) != Gm_PKind_Human) {
        return;
    }
    /* the label: the marker's joint 1, first part (frames CP, P1-P4); it was given player 3's frame */
    lb_80011E24(marker_jobj, &label, 1, -1);
    if (label == NULL || (label->flags & (JOBJ_PTCL | JOBJ_SPLINE)) || (d = label->u.dobj) == NULL ||
        d->mobj == NULL || (t = d->mobj->tobj) == NULL)
    {
        return;
    }
    if (t->imagedesc != NULL) {
        int i, ours = 0;
        for (i = 0; i < tri_nmarkers; i++) ours |= tri_markers[i].out == t->imagedesc;
        if (!ours) {
            img = tri_marker_digit(t->imagedesc, slot + 1);
            if (img != NULL) t->imagedesc = img;
        }
    }
    if (!gm_8016B168()) {   /* not a team battle: player 5 orange, player 6 purple */
        HSD_DObj* part;   /* the label and its arrow, each its own material (P1 F25959, P3 FFBF3F) */
        for (part = d; part != NULL; part = part->next) {
            if (part->mobj != NULL && part->mobj->mat != NULL) part->mobj->mat->diffuse = colour[slot - 4];
        }
    }
}

/* Shields (and the KO blast) of players 5 and 6 in a free-for-all: the game gives every slot past the
 * fourth the CPU's grey (fn_80160840). They take the colour the rest of their art has (gm_80160968,
 * the Triples patch's orange and purple); a team match keeps the team colours, a CPU its grey. */
int mu_triples_shield_color(int slot, unsigned int* rgb)
{
    u32 index;
    GXColor c;
    if (!mu_triples_active() || slot < 4 || slot > 5 || rgb == NULL || Player_GetUnk45(slot) != 4) {
        return 0;
    }
    index = gm_80160854((u8) Player_GetPadPort(slot), (u8) Player_GetTeam(slot), (u8) gm_8016B168(),
                        (u8) Player_GetPlayerSlotType(slot));
    if (index != 9 && index != 10) {
        return 0;
    }
    c = gm_80160968(index);
    *rgb = (unsigned int) c.r << 16 | (unsigned int) c.g << 8 | c.b;
    return 1;
}

/* "5-6 players: wider camera" (MU_TRIPLES_WIDE_CAMERA): the stage's furthest camera distance
 * (Stage_GetCamMaxDepth) a quarter longer, so six fighters spread over a big stage stay in the picture.
 * Offline only: the camera decides who is off screen, and that must match on every machine online. */
float mu_triples_camera_depth_scale(void)
{
    if (!mu_triples_active() || !(mu_triples_flags() & MU_TRIPLES_WIDE_CAMERA) || mu_online_active() ||
        mu_online_pending() || mu_console_rollback_active())
    {
        return 1.0F;
    }
    if (!tri_camera_logged) {
        tri_camera_logged = 1;
        OSReport("[triples] wider camera: the stage's camera depth limit x1.25\n");
    }
    return 1.25F;
}
