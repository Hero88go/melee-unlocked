/* Kirby's copy abilities for the fighters Akaneia adds: the shared layer.
 *
 * On the console this is a set of m-ex engine patches in the Kirby code plus three small runtime
 * routines (KirbyStateChange at 803D7080, MEX_GetKirbyCpData at 803D7084 and the hat load hook at
 * 800EEE80, which also relocates the hat file's PowerPC). Here it is plain C over the registry
 * (mu_ak_fighters.c), the m-ex data layer (shim/mu_mex.c) and the decomp's own Kirby functions.
 * No PowerPC is run or translated. The console listing of every patch is in
 * run-source/rel09-b1-wolf/kirby/mex_hooks.listing.txt, and LAYER.md there maps each function below
 * to it; APPLIED.md there says where each hook site landed.
 *
 * Kirby's copied kind (fp->u.kb.hat.kind) holds the NATIVE fighter kind of an added fighter
 * (MU_AK_KIND), where m-ex holds its internal id. Every retail table indexed by the copied kind
 * stays untouched: the decomp asks this file first for an added kind.
 *
 * State: the two tables below, filled when a scene loads its fighters and cleared by the game's
 * own hat reset. They are statics of the game library, so a rollback snapshot carries them with
 * the rest of the image. They do not change during a match. The `logged` counters only limit the
 * log.
 *
 * The functions the decomp calls are declared in include/mu_native.h. */
#include <dolphin/os.h>
#include <melee/ef/efasync.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftaction.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lbdvd.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/objalloc.h>

#include <stddef.h>

#include "../mu_ak_fighter.h"
#include "mu_ak_kirby.h"

#ifdef MU_AKANEIA_FIGHTERS

/* The motion id m-ex gives Kirby in state n of an added ability (KirbyStateChange +0B4:
 * addi r3, r30, 0x190). It lies inside the retail range of Kirby's copy states (399 to 543), so
 * the game's own "lose the ability when hit during a copy move" test (ftKb_SpecialN_800F5C34)
 * covers the added abilities too. */
#define AK_KB_MOTION_BASE 400

#define AK_KB_NO_EFFECT_FILE 0xFF

#define AK_KB_LOG 8   /* evidence lines of each kind that go in the log, per run */

/* By slot (kind - MU_AK_KIND_BASE): the loaded hat data and animation table of the scene. */
static KirbyHatStruct* ak_kb_hat[MU_AK_KIND_SLOTS];
static MuAkKirbyCmd* ak_kb_cmd[MU_AK_KIND_SLOTS];

/* The native ability of an added kind, NULL when its fighter has none in this build. */
static const MuAkKirbyCopy* copy_of(int kind)
{
    const MuAkFighter* ft = mu_ak_fighter(kind);
    return ft != NULL ? ft->kirby : NULL;
}

int mu_ak_kirby_has_copy(int kind)
{
    return MU_AK_KIND(kind) && copy_of(kind) != NULL && ak_kb_hat[kind - MU_AK_KIND_BASE] != NULL &&
           ak_kb_cmd[kind - MU_AK_KIND_BASE] != NULL;
}

int mu_ak_kirby_copy_kind(Fighter* fp)
{
    if (fp != NULL && fp->kind == Ft_Kind_Kirby && MU_AK_KIND(fp->u.kb.hat.kind)) {
        return fp->u.kb.hat.kind;
    }
    return Ft_Kind_None;
}

/* ---- the hat file ---- */

/* ftKb_Init_800EE528 (m-ex hook at 800EE528): the scene's hat pointers are dropped. */
void mu_ak_kirby_reset(void)
{
    __builtin_memset(ak_kb_hat, 0, sizeof ak_kb_hat);
    __builtin_memset(ak_kb_cmd, 0, sizeof ak_kb_cmd);
}

/* ftKb_SpecialN_800EEC34: ask for the hat file and its effect file ahead of the scene. m-ex runs
 * the retail body over its own tables (the 04 patches at 800EEC50, 800EECA0, 800EED24); an added
 * kind has no per-costume hat files, so only the first and last steps apply. */
void mu_ak_kirby_preload(int kind)
{
    const int mex = mu_ak_mex_internal(kind);
    const char* file;
    const char* symbol;
    int effect;
    if (!MU_AK_KIND(kind) || copy_of(kind) == NULL || mex < 0) {
        return;
    }
    if (mu_mex_kirby_cap(mex, &file, &symbol)) {
        lbDvd_800178E8(2, file, 4, 4, 0, 1, 3, 1, 0);
    }
    effect = mu_mex_kirby_effect_file(mex);
    if (effect >= 0 && effect != AK_KB_NO_EFFECT_FILE) {
        efAsync_LoadAsync(effect);
    }
}

/* ftKb_SpecialN_800EED50: load the hat file of a fighter kind in the match. m-ex keeps the
 * retail load and adds, at 800EEE80: relocate the file's kbFunction and write its exports into
 * mexData.kirby_function, do the same for its itFunction, and store its ftcmd table per kind. The
 * exports are C here (MuAkKirbyCopy and the fighter's article tables), so what is left is the
 * hat data and the ftcmd table.
 *
 * The retail load (lbArchive_80017040) stops the game when the symbol is missing. Here the file
 * is taken the same way (the preloaded copy when there is one) and both symbols are looked up
 * without that stop: a file without them leaves the kind with no copy, and Kirby keeps what he
 * has. */
void mu_ak_kirby_load(int kind)
{
    static int logged;
    const int slot = kind - MU_AK_KIND_BASE;
    const int mex = mu_ak_mex_internal(kind);
    const char* file;
    const char* symbol;
    int effect;
    if (!MU_AK_KIND(kind) || copy_of(kind) == NULL || mex < 0) {
        return;
    }
    if (ak_kb_hat[slot] == NULL && mu_mex_kirby_cap(mex, &file, &symbol)) {
        HSD_Archive* archive = NULL;
        lbArchive_80016F80(&archive, file);
        if (archive != NULL) {
            ak_kb_hat[slot] = HSD_ArchiveGetPublicAddress(archive, symbol);
            ak_kb_cmd[slot] = HSD_ArchiveGetPublicAddress(archive, "ftcmd");
        }
        if (logged < AK_KB_LOG * 2) {
            logged++;
            OSReport("[ak] Kirby: hat of kind %d from %s (%s): hat data %s, ftcmd %s\n", kind, file,
                     symbol, ak_kb_hat[slot] != NULL ? "loaded" : "MISSING",
                     ak_kb_cmd[slot] != NULL ? "found" : "MISSING");
        }
    }
    effect = mu_mex_kirby_effect_file(mex);
    if (effect >= 0 && effect != AK_KB_NO_EFFECT_FILE) {
        efAsync_LoadSync(effect);
    }
}

/* m-ex MEX_GetKirbyCpData (803D7084). */
KirbyHatStruct* mu_ak_kirby_hat(FighterKind kind)
{
    return MU_AK_KIND(kind) ? ak_kb_hat[kind - MU_AK_KIND_BASE] : NULL;
}

/* The ftcmd table the scene loaded for a fighter kind (m-ex keeps it per kind at r2 + 0x128):
 * for a hat file that carries its own state change routine (Tails, King Dedede). */
MuAkKirbyCmd* mu_ak_kirby_cmd(FighterKind kind)
{
    return MU_AK_KIND(kind) ? ak_kb_cmd[kind - MU_AK_KIND_BASE] : NULL;
}

/* The body every Akaneia hat's swallow export shares with the retail hats (Mario's is
 * ftKb_SpecialN_800EFA40 in ftkirby.c). The m-ex code does not write fp->x2225_b2, which the
 * retail function sets (the hat is only drawn while that bit is set, ftKb_UnkMtxFunc0). It is left
 * alone here too: every state change made without Ft_MF_SkipModelFlags sets it
 * (Fighter_ChangeMotionState). */
void mu_ak_kirby_attach_hat(Fighter_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    KirbyHatStruct* hat;
    if (fp->u.kb.hat.jobj != NULL) {
        return;
    }
    hat = mu_ak_kirby_hat(fp->u.kb.hat.kind);
    if (hat == NULL) {
        /* Not an m-ex case (the console would read through a null pointer). */
        OSReport("[ak] Kirby: no hat data for kind %d; no hat\n", (int) fp->u.kb.hat.kind);
        return;
    }
    fp->u.kb.hat.x14.data = HSD_ObjAlloc(&fighter_x2040_alloc_data);
    ftPartsPObjSetDefaultClass();
    fp->u.kb.hat.jobj = HSD_JObjLoadJoint(DP(hat->hat_joint));
    ftPartsPObjClearDefaultClass();
    ftParts_80075650(gobj, fp->u.kb.hat.jobj, &fp->u.kb.hat.x14);
    ftParts_8007487C(&hat->desc, &fp->u.kb.hat.x24, 0, &fp->u.kb.hat.x14, &fp->u.kb.hat.x14);
}

/* ---- the engine's dispatch (the kirby_function tables of MxDt) ---- */

/* ftKb_SpecialN_800F1BAC (m-ex hook at 800F1BF8, table 0). */
void mu_ak_kirby_gain(HSD_GObj* gobj, int kind)
{
    const MuAkKirbyCopy* copy = copy_of(kind);
    static int logged;
    if (logged < AK_KB_LOG) {
        logged++;
        OSReport("[ak] Kirby copied added fighter kind %d (%s)\n", kind,
                 copy != NULL ? "native ability" : "NO native ability");
    }
    if (copy != NULL && copy->on_swallow != NULL) {
        copy->on_swallow(gobj);
    }
}

/* ftKb_SpecialN_800EEEC4 (m-ex replaces the whole function, table 1). */
void mu_ak_kirby_lose(HSD_GObj* gobj, int kind)
{
    const MuAkKirbyCopy* copy = copy_of(kind);
    static int logged;
    if (logged < AK_KB_LOG) {
        logged++;
        OSReport("[ak] Kirby lost the ability of added fighter kind %d\n", kind);
    }
    if (copy != NULL && copy->on_lose != NULL) {
        copy->on_lose(gobj);
    }
}

/* ftKb_SpecialN_Enter and ftKb_SpecialAirN_Enter (m-ex 04 patches at 800F163C and 800F168C, tables
 * 2 and 3). Returns 1 when the added ability's special ran; 0 sends the caller to its retail
 * path, which for an empty slot is the inhale, as in m-ex. */
int mu_ak_kirby_special_n(HSD_GObj* gobj, int air)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const MuAkKirbyCopy* copy = copy_of(fp->u.kb.hat.kind);
    void (*enter)(HSD_GObj*);
    static int logged;
    if (copy == NULL) {
        return 0;
    }
    enter = air ? copy->special_air_n : copy->special_n;
    if (enter == NULL) {
        return 0;
    }
    if (logged < AK_KB_LOG) {
        logged++;
        OSReport("[ak] Kirby uses the ability of added fighter kind %d (%s)\n",
                 (int) fp->u.kb.hat.kind, air ? "air" : "ground");
    }
    enter(gobj);
    return 1;
}

/* ftKb_SpecialN_800F16D0 (m-ex hook at 800F16EC, table 5): the export gets the copied kind and
 * that kind's hat data, and registers the ability's articles under the copied fighter's item
 * kinds (MEX_IndexFighterItem). */
void mu_ak_kirby_init_items(HSD_GObj* gobj, int kind)
{
    const MuAkKirbyCopy* copy = copy_of(kind);
    KirbyHatStruct* hat = mu_ak_kirby_hat(kind);
    (void) gobj;
    if (copy != NULL && copy->init_items != NULL && hat != NULL) {
        copy->init_items(kind, hat);
    }
}

/* ftKb_SpecialN_800F1A8C (m-ex hook at 800F1AA8, table 4). */
void mu_ak_kirby_hurt(HSD_GObj* gobj, int kind)
{
    const MuAkKirbyCopy* copy = copy_of(kind);
    if (copy != NULL && copy->on_hurt != NULL) {
        copy->on_hurt(gobj);
    }
}

/* ftKb_Init_UnkMotionStates3 (m-ex hook at 800F1B94, table 7). */
void mu_ak_kirby_frame(HSD_GObj* gobj, int kind)
{
    const MuAkKirbyCopy* copy = copy_of(kind);
    if (copy != NULL && copy->on_frame != NULL) {
        copy->on_frame(gobj);
    }
}

/* ---- KirbyStateChange ---- */

/* m-ex KirbyStateChange (803D7080). It does not go through the game's action state tables: it
 * enters the common state ThrownF with the animation step skipped, which runs every reset the
 * game does at a state change, then plays the ability's animation and script straight from the
 * hat file and installs the ability's callbacks.
 *
 * Kept as m-ex has it, where it differs from what Fighter_ChangeMotionState does for a state of
 * a fighter's own table:
 *   - the state's flags and move id (words +4 and +8 of its move_logic entry) are never read, so
 *     the move id and the state flags are ThrownF's;
 *   - the caller's flags are ignored (always Ft_MF_SkipAnim alone);
 *   - the animation starts at anim_start itself (the game passes anim_start - anim_speed when
 *     anim_start is not 0), and anim_blend is used as given (the game maps 0 to the animation's
 *     own blend count and -1 to 0);
 *   - the script timer is always 0 and the script's loop count is not cleared;
 *   - fp->cur_anim_frame stays what the ThrownF change left (anim_start - anim_speed). */
void mu_ak_kirby_state_change(Fighter_GObj* gobj, int state, MotionFlags flags, float anim_start,
                              float anim_speed, float anim_blend)
{
    Fighter* fp = GET_FIGHTER(gobj);
    const MuAkKirbyCopy* copy;
    const MotionState* ms;
    MuAkKirbyCmd* cmd;
    int kind;
    static int logged;
    (void) flags;

    if (fp->kind != Ft_Kind_Kirby) {
        OSReport("[ak] KirbyStateChange: fighter kind %d is not Kirby\n", (int) fp->kind);
        return;
    }
    kind = fp->u.kb.hat.kind;
    copy = copy_of(kind);
    /* m-ex stops the game for each of these (an assert named "m-ex"); here Kirby keeps the state
     * he is in. */
    if (copy == NULL || copy->move_logic == NULL || state < 0 || state >= copy->move_logic_count) {
        OSReport("[ak] KirbyStateChange: kind %d has no state %d\n", kind, state);
        return;
    }
    if (!MU_AK_KIND(kind) || ak_kb_cmd[kind - MU_AK_KIND_BASE] == NULL) {
        OSReport("[ak] KirbyStateChange: kind %d has no ftcmd table\n", kind);
        return;
    }
    ms = &copy->move_logic[state];
    cmd = &ak_kb_cmd[kind - MU_AK_KIND_BASE][ms->anim_id];
    if (DISC_NULL(cmd->anim)) {
        OSReport("[ak] KirbyStateChange: kind %d state %d has no animation\n", kind, state);
        return;
    }
    if (logged < AK_KB_LOG) {
        const char* name = DP(cmd->name);
        logged++;
        OSReport("[ak] Kirby state %d of kind %d: motion id %d, animation %d (%s)\n", state, kind,
                 AK_KB_MOTION_BASE + state, (int) ms->anim_id, name != NULL ? name : "no name");
    }

    Fighter_ChangeMotionState(gobj, ftCo_MS_ThrownF, Ft_MF_SkipAnim, anim_start, anim_speed,
                              anim_blend, NULL);
    fp->anim_id = 0;
    fp->motion_id = AK_KB_MOTION_BASE + state;
    /* m-ex writes -1 here (the console's 32-bit word): the game's animation cache key no longer
     * names what is loaded, so the next state of Kirby's own table reloads its animation
     * (ftData_80085CD8 compares the key with the entry's file offset). */
    fp->x5A4 = (uintptr_t) 0xFFFFFFFFu;

    fp->x590 = DP(cmd->anim);
    fp->x594_s32 = cmd->flags;
    /* The hat file's animations are made for Kirby's skeleton, and m-ex decides that from who owns
     * the animation (its anim_owner field, 0 here), not from the kind in the animation flags,
     * which is 0 (Mario) in every Akaneia hat file. The native game reads the flags for a retail
     * fighter (ft/ftanim.c, MU_ANIM_IS_FOREIGN), so the kind is written into them. On the console
     * these six bits stay as the file has them and nothing reads them. */
    fp->x597_bits = (u32) fp->kind;
    fp->x3E4_fighterCmdScript.u = DP(cmd->script);

    ftAnim_8006EBE8(gobj, anim_start, anim_speed, anim_blend);
    fp->x3E4_fighterCmdScript.timer = 0.0f;
    ftAnim_8006E9B4(gobj);
    if (anim_start == 0.0f) {
        ftCo_800C0408(gobj);
        ftAction_80073240(gobj);
    } else {
        ftAction_80073354(gobj);
    }

    fp->anim_cb = ms->anim_cb;
    fp->input_cb = ms->input_cb;
    fp->phys_cb = ms->phys_cb;
    fp->coll_cb = ms->coll_cb;
    fp->cam_cb = ms->cam_cb;
}

#endif /* MU_AKANEIA_FIGHTERS */
