/* ACE 2.0.0's Daisy: the ability Kirby copies from her (m-ex "kbFunction" of PlKbCpDa.dat).
 *
 * Kirby's Daisy ability is her Toad counter (ace/daisy/daisy_specialn.c), which is Peach's, and
 * the hat file's code is built on Kirby's own Peach copy (ft/kinds/ftKirby/ftkirbyspecialpeach.c):
 * the Toad Kirby holds is fp->u.kb.xD0 (console fp+22FC) and the "air version used" flag is
 * fp->u.kb.xCC (fp+22F8), the facing kept for the hit state is fp->mv.kb.specialn_pe.facing_dir
 * (an int, fp+2340), the death and damage callbacks are the calls of Kirby's own
 * ftKb_Init_800EE74C and ftKb_Init_800EE7B8, and the Toad and the spores leave from Kirby's part
 * 44 (FtPart_R3rdNa, as in the retail Kirby copy). Against Daisy's own code the differences are:
 *   - every state change goes through KirbyStateChange (four states, motion ids 400 to 403);
 *   - every value the move reads from Daisy's attributes is a constant of the code: the air
 *     entry halves the x speed (Daisy divides by +94), the air physics uses 0.7 (first upward
 *     speed and terminal speed), 0.025 (fall acceleration) and 0.0025 (drift), and the two
 *     shield numbers are 5.0 in the air as on the ground;
 *   - the articles are Daisy's own two, 3 (the Toad, item kind 290) and 4 (the spores, 291),
 *     with the hat file's article data (hat data +0xC and +0x10).
 * The callbacks that are word for word Daisy's own are named in the state table; the spawns and
 * the Toad's second state are the retail Peach item functions Daisy's port uses for the same
 * words. See run-source/rel09-ace-native/daisy/kirby/KIRBY.md.
 *
 * Articles: the hat file's itFunction 3 and 4 are Daisy's own blocks 3 and 4: block 4 word for
 * word, block 3 with four debug OSReport calls added. Both are registered under Daisy's own item
 * kinds 290 and 291, so natively their logic tables are Daisy's (ace/daisy/daisy_items.c), which
 * already handle a Kirby owner. No article code here.
 *
 * The hat file has no debug symbols. Each function names the offset of its routine in the hat
 * file's code block (run-source/rel09-ace-native/daisy/kirby/PlKbCpDa.listing.txt shows it at
 * 0x81800000 plus the offset) and its size in words. The code has no fixed console address:
 * m-ex loads it with the hat file.
 *
 * Nothing below keeps state outside Kirby's Fighter. */
#include "../../ace/daisy/daisy.h"

#include <melee/ft/fighter.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ftanim.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/kinds/ftKirby/ftkirbyspecialhi.h>
#include <melee/ft/kinds/ftKirby/types.h>
#include <melee/ft/kinds/ftPeach/ftpeachspecialn.h>
#include <melee/ft/types.h>
#include <melee/it/kinds/itpeachtoad.h>
#include <melee/it/kinds/itpeachtoadspore.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/types.h>
#include <sysdolphin/baselib/gobj.h>

#include "daisy_kirby.h"

/* Kirby's states of this ability (KirbyStateChange state numbers; motion ids 400 to 403). They
 * mirror Daisy's (Peach's) 365 to 368. */
enum {
    ftKbDa_State_SpecialN,       /* 0: the counter on the ground */
    ftKbDa_State_SpecialNHit,    /* 1: the counter was hit, on the ground */
    ftKbDa_State_SpecialAirN,    /* 2 */
    ftKbDa_State_SpecialAirNHit, /* 3 */
    ftKbDa_State_Count
};

/* The script variables, as in Daisy's and Peach's: 0 physics step, 1 bubble step, 2 Toad made,
 * 3 "scatter the spores". */
enum {
    ftKbDa_Cmd_Phys = 0,
    ftKbDa_Cmd_Anim = 1,
    ftKbDa_Cmd_Toad = 2,
    ftKbDa_Cmd_Hit = 3,
};

/* Daisy's articles the copy uses, by index into her MxDt item list (InitCopyItems +0x254). */
enum {
    ftKbDa_Article_Toad = 3,  /* item kind 290 */
    ftKbDa_Article_Spore = 4, /* item kind 291 */
};

/* Kirby's joint the Toad hangs from and the spores leave from: fp->parts + 0x2C0, part 44. */
#define FTKBDA_PART FtPart_R3rdNa

/* The transition flags (0x0C4C508C), not read by KirbyStateChange. */
#define FTKBDA_COLL_MF FTDA_SPECIALN_COLL_MF

static void ftKbDa_OnShieldHit(HSD_GObj* gobj);
static void ftKbDa_SpecialN_OnAccessory4(HSD_GObj* gobj);

/* The counter bubble, built on the stack at each use (+0x428 on: the words 3, 0.0, 1.0, 3.5,
 * 6.0; Daisy's constants), handed to ftColl_8007B1B8 with +0xC70 as its callback. The address of
 * ftColl_8007B1B8 is kept at +0xC6C and the call goes through it. */
static void ftKbDa_SetBubble(HSD_GObj* gobj)
{
    ShieldDesc bubble = { 0 };

    bubble.bone = 3;
    bubble.pos.x = 0.0f;
    bubble.pos.y = 1.0f;
    bubble.pos.z = 3.5f;
    bubble.radius = 6.0f;
    ftColl_8007B1B8(gobj, &bubble, ftKbDa_OnShieldHit);
}

/* ---- the exports --------------------------------------------------------------------------- */

/* [OnKirbySwallow] +0x000 (48 words): put the hat on. The retail hat attach without the x2225_b2
 * write: word for word Wario's hat +0x000 (two epilogue pairs in another order than Akaneia's
 * Lucas and Wolf hats). */
static void ftKbDa_OnSwallow(HSD_GObj* gobj)
{
    mu_ak_kirby_attach_hat(gobj);
}

/* [OnKirbyLoseAbility] +0x0C0 (23 words): take the hat off. Word for word the Akaneia Lucas
 * hat's +0x0C0, the retail ftKb_SpecialN_800EFAF0. A Toad that is out is not touched here. */
static void ftKbDa_OnLose(HSD_GObj* gobj)
{
    ftKb_SpecialN_800EFAF0(gobj);
}

/* [InitCopyItems] +0x254 (19 words): MEX_IndexFighterItem(kind, hat +0xC, 3) and
 * (kind, hat +0x10, 4). Daisy's own articles 3 and 4 take the hat file's data (mod behavior,
 * KIRBY.md). */
static void ftKbDa_InitCopyItems(FighterKind kind, KirbyHatStruct* hat)
{
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[0]), ftKbDa_Article_Toad);
    mu_ak_register_article(kind, DISC_GET(Article, hat->hat_dynamics[1]), ftKbDa_Article_Spore);
}

/* The tail both entries share (+0x168 to +0x1A4 and +0x20C to +0x248): the facing kept as an
 * int (fctiwz: truncated), the Toad callback, the four script variables cleared. Kirby's Peach
 * inlineA0. */
static void ftKbDa_SpecialN_Reset(Fighter* fp)
{
    fp->accessory4_cb = ftKbDa_SpecialN_OnAccessory4;
    fp->cmd_vars[ftKbDa_Cmd_Hit] = 0;
    fp->cmd_vars[ftKbDa_Cmd_Toad] = 0;
    fp->mv.kb.specialn_pe.facing_dir = (int) fp->facing_dir;
    fp->cmd_vars[ftKbDa_Cmd_Anim] = 0;
    fp->cmd_vars[ftKbDa_Cmd_Phys] = 0;
}

/* [SpecialN] +0x11C (37 words): self_vel.y = 0.0, KirbyStateChange(gobj, 0, 0, 0, 0.0, 1.0,
 * 0.0), ftAnim_8006EBA4, then the shared tail. */
static void ftKbDa_SpecialN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.y = 0.0f;
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialN, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    ftKbDa_SpecialN_Reset(fp);
}

/* [SpecialAirN] +0x1B0 (41 words): self_vel.x = self_vel.x * 0.5 (Daisy divides by her +94),
 * state 2, then as the ground entry. */
static void ftKbDa_SpecialAirN_Enter(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->self_vel.x = fp->self_vel.x * 0.5f;
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialAirN, Ft_MF_None, 0.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    ftKbDa_SpecialN_Reset(fp);
}

/* ---- the callbacks the move installs -------------------------------------------------------- */

/* +0xADC (45 words): the death callback the Toad installs. The six calls of Kirby's own
 * ftKb_Init_800EE74C, through the address words at +0x11CC to +0x11E0, and death3_cb cleared.
 * None of them removes this Toad (ftKb_SpecialN_800F19AC does only for a retail Peach copy). */
static void ftKbDa_OnDeath2(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKb_SpecialAirLw_800F5524(gobj);
    ftKb_AttackDashAir_800F22D4(gobj);
    ftKb_SpecialAirLw_800F5318(gobj);
    ftKb_SpecialN_800F9090(gobj);
    ftKb_SpecialN_800F19AC(gobj);
    ftKb_SpecialN_800F5D04(gobj, false);
    fp->death3_cb = NULL;
}

/* +0xB90 (39 words): the damage callback the Toad installs. The five calls of Kirby's own
 * ftKb_Init_800EE7B8 (the last, ftKb_SpecialN_800F1A8C, is the layer's on_hurt slot for an added
 * kind, empty here), and death1_cb cleared. */
static void ftKbDa_OnTakeDamage(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKb_SpecialAirLw_800F5524(gobj);
    ftKb_AttackDashAir_800F22D4(gobj);
    ftKb_SpecialAirLw_800F5318(gobj);
    ftKb_SpecialN_800F9090(gobj);
    ftKb_SpecialN_800F1A8C(gobj);
    fp->death1_cb = NULL;
}

/* +0xC2C (8 words): the hitlag callback, the Toad freezes with Kirby. Daisy's +0x1C50 with
 * fp+22FC for fp+2244 (Kirby's Peach fn_8010C44C). */
static void ftKbDa_OnEnterHitlag(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.kb.xD0 != NULL) {
        it_802BDFA0(fp->u.kb.xD0);
    }
}

/* +0xC4C (8 words): the end of hitlag. Daisy's +0x1C70 with fp+22FC (Kirby's Peach
 * ftKb_SpecialNPe_8010C47C). */
static void ftKbDa_OnExitHitlag(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->u.kb.xD0 != NULL) {
        it_802BDFC0(fp->u.kb.xD0);
    }
}

/* With a Toad out, the death and damage callbacks (the head of +0xE0C, +0xF90, +0x1078,
 * +0x1174, +0xD1C). */
static void ftKbDa_SetToadCallbacks(Fighter* fp)
{
    if (fp->u.kb.xD0 != NULL) {
        fp->death2_cb = ftKbDa_OnDeath2;
        fp->take_dmg_cb = ftKbDa_OnTakeDamage;
    }
}

/* +0x9F0 (59 words): the Toad spawn. It asks MEX_GetFtItemID(gobj, 3) itself, then builds the
 * request of retail it_802BDE18 (the parents, the kind, the position twice, no speed, the facing,
 * damage 0, word +40 0, the collision bit set), Item_80268B18, Item_8026AB54(item, gobj, part)
 * and db_80225DD8 when made. */
static Item_GObj* ftKbDa_SpawnToad(HSD_GObj* gobj, Vec3* pos, Fighter_Part part, float facing)
{
    ItemKind kind = mu_ak_article_kind(gobj, ftKbDa_Article_Toad);

    if ((int) kind < 0) {
        /* Not an m-ex case: no item kind for the article (m-ex stops the game there). */
        return NULL;
    }
    return it_802BDE18(gobj, pos, part, kind, facing);
}

/* +0x358 (52 words): the accessory callback of states 0 and 2, make the Toad once. Daisy's
 * +0x1260 with part 44 direct (Daisy looks part 49 up), the Toad into fp->u.kb.xD0, Kirby's
 * callbacks, and no fp->x1984_heldItemSpec store. The code also asks MEX_GetFtItemID(gobj, 3)
 * before the spawn and leaves the answer in a register the spawn does not read. */
static void ftKbDa_SpecialN_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;

    if (fp->cmd_vars[ftKbDa_Cmd_Toad] != 0) {
        return;
    }
    fp->cmd_vars[ftKbDa_Cmd_Toad] = 1;
    lb_8000B1CC(fp->parts[FTKBDA_PART].joint, NULL, &pos);
    (void) mu_ak_article_kind(gobj, ftKbDa_Article_Toad);
    fp->u.kb.xD0 = ftKbDa_SpawnToad(gobj, &pos, FTKBDA_PART, fp->facing_dir);
    if (fp->u.kb.xD0 != NULL) {
        fp->death2_cb = ftKbDa_OnDeath2;
        fp->take_dmg_cb = ftKbDa_OnTakeDamage;
    }
    fp->pre_hitlag_cb = ftKbDa_OnEnterHitlag;
    fp->post_hitlag_cb = ftKbDa_OnExitHitlag;
    fp->accessory4_cb = NULL;
}

/* +0xEB0 (36 words): the accessory callback of the two hit states, one spore from part 44,
 * 2.5 up (fadds), on the plane. Daisy's +0x17D0 with part 44 for 109. The spawn (+0x1284, 51
 * words) is word for word Daisy's +0x1E3C, which her port proves is retail it_802BE214. */
static void ftKbDa_SpecialNHit_OnAccessory4(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    Vec3 pos;
    ItemKind kind;

    lb_8000B1CC(fp->parts[FTKBDA_PART].joint, NULL, &pos);
    pos.y = pos.y + 2.5f;
    pos.z = 0.0f;
    kind = mu_ak_article_kind(gobj, ftKbDa_Article_Spore);
    if ((int) kind >= 0) {
        it_802BE214(gobj, &pos, kind, fp->facing_dir);
    }
    fp->accessory4_cb = NULL;
}

/* +0xC70 (64 words): the bubble was hit. The facing kept (fp->specialn_facing_dir as an int,
 * fctiwz), state 1 on the ground or 3 in the air from frame 9, ftAnim_8006EBA4, the Toad to its
 * second state (+0x11F0, word for word Daisy's +0x1DA8, retail it_802BE100), the callbacks.
 * Kirby's Peach fn_8010CD88 with the state change swapped. */
static void ftKbDa_OnShieldHit(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->mv.kb.specialn_pe.facing_dir = (int) fp->specialn_facing_dir;
    mu_ak_kirby_state_change(gobj,
                             fp->ground_or_air == GA_Ground ? ftKbDa_State_SpecialNHit
                                                            : ftKbDa_State_SpecialAirNHit,
                             Ft_MF_None, 9.0f, 1.0f, 0.0f);
    ftAnim_8006EBA4(gobj);
    if (fp->u.kb.xD0 != NULL) {
        it_802BE100(fp->u.kb.xD0);
        ftKbDa_SetToadCallbacks(fp);
    }
    fp->pre_hitlag_cb = ftKbDa_OnEnterHitlag;
    fp->post_hitlag_cb = ftKbDa_OnExitHitlag;
}

/* What the transitions of states 0 and 2 put back (the tail of +0xD70 and +0xFE8): Daisy's
 * SetupColl with Kirby's fields. */
static void ftKbDa_SetupColl(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbDa_SetToadCallbacks(fp);
    fp->accessory4_cb = ftKbDa_SpecialN_OnAccessory4;
    fp->pre_hitlag_cb = ftKbDa_OnEnterHitlag;
    fp->post_hitlag_cb = ftKbDa_OnExitHitlag;
    if (fp->cmd_vars[ftKbDa_Cmd_Anim] == 2) {
        ftKbDa_SetBubble(gobj);
        fp->x221B_b3 = true;
    }
}

/* What the transitions of the two hit states put back (the tail of +0xF40 and +0x111C). */
static void ftKbDa_SetupHitColl(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftKbDa_SetToadCallbacks(fp);
    fp->pre_hitlag_cb = ftKbDa_OnEnterHitlag;
    fp->post_hitlag_cb = ftKbDa_OnExitHitlag;
}

/* +0xD70 (80 words): off the ground during state 0. ftCommon_8007D5D4, state 2 at the current
 * frame, physics step 1 becomes 2, then the setup. */
static void ftKbDa_SpecialN_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialAirN, FTKBDA_COLL_MF, fp->cur_anim_frame,
                             1.0f, 0.0f);
    if (fp->cmd_vars[ftKbDa_Cmd_Phys] == 1) {
        fp->cmd_vars[ftKbDa_Cmd_Phys] = 2;
    }
    ftKbDa_SetupColl(gobj);
}

/* +0xFE8 (77 words): landed during state 2. xCC cleared first, ftCommon_8007D7FC, state 0 at the
 * current frame, then the setup. */
static void ftKbDa_SpecialN_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->u.kb.xCC = false;
    ftCommon_8007D7FC(fp);
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialN, FTKBDA_COLL_MF, fp->cur_anim_frame,
                             1.0f, 0.0f);
    ftKbDa_SetupColl(gobj);
}

/* +0xF40 (42 words): off the ground during state 1. */
static void ftKbDa_SpecialNHit_GroundToAir(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_8007D5D4(fp);
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialAirNHit, FTKBDA_COLL_MF,
                             fp->cur_anim_frame, 1.0f, 0.0f);
    ftKbDa_SetupHitColl(gobj);
}

/* +0x111C (44 words): landed during state 3. */
static void ftKbDa_SpecialNHit_AirToGround(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    fp->u.kb.xCC = false;
    ftCommon_8007D7FC(fp);
    mu_ak_kirby_state_change(gobj, ftKbDa_State_SpecialNHit, FTKBDA_COLL_MF, fp->cur_anim_frame,
                             1.0f, 0.0f);
    ftKbDa_SetupHitColl(gobj);
}

/* ---- the states ---------------------------------------------------------------------------- */

/* The animation callbacks of states 0 and 2 (+0x428 and +0x68C, 68 words each; +0x428 is word
 * for word Daisy's +0xC60 but the callback it installs): the bubble comes up when the script
 * asks, the two shield numbers are 5.0 (+0x330) in both. */
static void ftKbDa_SpecialN_AnimShared(HSD_GObj* gobj, HSD_GObjEvent end)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 step = fp->cmd_vars[ftKbDa_Cmd_Anim];

    if (step == 1) {
        fp->cmd_vars[ftKbDa_Cmd_Anim] = 2;
        ftKbDa_SetBubble(gobj);
        fp->x221B_b3 = true;
        fp->shield_unk0 = 5.0f;
        fp->shield_unk1 = 5.0f;
    } else if (step == 0) {
        fp->x221B_b0 = false;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        end(gobj);
    }
}

/* [state 0 Anim] +0x428 */
static void ftKbDa_SpecialN_Anim(HSD_GObj* gobj)
{
    ftKbDa_SpecialN_AnimShared(gobj, ft_8008A2BC);
}

/* [state 0 Coll] +0x578 (19 words) */
static void ftKbDa_SpecialN_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftKbDa_SpecialN_GroundToAir(gobj);
    }
}

/* The animation callbacks of the hit states (+0x5C4 and +0x8E0, 29 words each, word for word
 * Daisy's +0xDFC and +0x1124 but the callback they install). */
static void ftKbDa_SpecialNHit_AnimShared(HSD_GObj* gobj, HSD_GObjEvent end)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->cmd_vars[ftKbDa_Cmd_Hit] != 0) {
        fp->cmd_vars[ftKbDa_Cmd_Hit] = 0;
        fp->accessory4_cb = ftKbDa_SpecialNHit_OnAccessory4;
    }
    if (!ftAnim_IsFramesRemaining(gobj)) {
        end(gobj);
    }
}

/* [state 1 Anim] +0x5C4 */
static void ftKbDa_SpecialNHit_Anim(HSD_GObj* gobj)
{
    ftKbDa_SpecialNHit_AnimShared(gobj, ft_8008A2BC);
}

/* [state 1 Coll] +0x640 (19 words) */
static void ftKbDa_SpecialNHit_Coll(HSD_GObj* gobj)
{
    if (!ft_800827A0(gobj)) {
        ftKbDa_SpecialNHit_GroundToAir(gobj);
    }
}

/* [state 2 Anim] +0x68C */
static void ftKbDa_SpecialAirN_Anim(HSD_GObj* gobj)
{
    ftKbDa_SpecialN_AnimShared(gobj, ftCo_Fall_Enter);
}

/* [state 2 Phys] +0x7A0 (61 words): Daisy's (Peach's) air physics with constants. Physics step
 * 0: the basic fall; step 1: step 2, the first use of this airtime (xCC) gets 0.7 upward, a later
 * one 0; from step 1 on the fall with 0.025 and 0.7. Then the drift with 0.0025, and the bubble's
 * hit position update allowed again (ftColl_8007AEE0, inlined in the code). */
static void ftKbDa_SpecialAirN_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    u32 step = fp->cmd_vars[ftKbDa_Cmd_Phys];

    if (step == 0) {
        ftCommon_FallBasic(fp);
    } else {
        if (step == 1) {
            fp->cmd_vars[ftKbDa_Cmd_Phys] = 2;
            if (!fp->u.kb.xCC) {
                fp->u.kb.xCC = true;
                fp->self_vel.y = 0.7f;
            } else {
                fp->self_vel.y = 0.0f;
            }
        }
        ftCommon_Fall(fp, 0.025f, 0.7f);
    }
    ftCommon_CalcSelfAccel_Deaccel(fp, 0.0025f);
    ftColl_8007AEE0(gobj);
}

/* [state 2 Coll] +0x894 (19 words) */
static void ftKbDa_SpecialAirN_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDa_SpecialN_AirToGround(gobj);
    }
}

/* [state 3 Anim] +0x8E0 */
static void ftKbDa_SpecialAirNHit_Anim(HSD_GObj* gobj)
{
    ftKbDa_SpecialNHit_AnimShared(gobj, ftCo_Fall_Enter);
}

/* [state 3 Phys] +0x958 (19 words): the fall with 0.025 and 0.7, the drift with 0.0025 (Daisy's
 * +0x119C with constants). */
static void ftKbDa_SpecialAirNHit_Phys(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    ftCommon_Fall(fp, 0.025f, 0.7f);
    ftCommon_CalcSelfAccel_Deaccel(fp, 0.0025f);
}

/* [state 3 Coll] +0x9A4 (19 words) */
static void ftKbDa_SpecialAirNHit_Coll(HSD_GObj* gobj)
{
    if (ft_80081D0C(gobj)) {
        ftKbDa_SpecialNHit_AirToGround(gobj);
    }
}

/* [move_logic] +0x2A0 (four entries of 0x20 bytes). Words +4 and +8 are Peach's (0x0034001D,
 * 0x0034041D, move id 0x12); KirbyStateChange does not read them. The camera word is the literal
 * address of ftCamera_UpdateCameraBox. The callbacks that are word for word Daisy's own are the
 * functions her table names:
 *   [state 0 IASA] +0x538, [state 1 IASA] +0x638, [state 2 IASA] +0x79C, [state 3 IASA] +0x954:
 *     a lone return (Peach's four IASA callbacks);
 *   [state 0 Phys] +0x53C: Daisy's +0xD74, ftPe_SpecialN_Phys;
 *   [state 1 Phys] +0x63C: a branch to ft_80084F3C, as Daisy's +0xE74, ftPe_SpecialNHit_Phys. */
static const MotionState ftKbDa_MotionStateTable[ftKbDa_State_Count] = {
    {
        /* state 0, ftcmd 0 "DaSpecialN" */
        0,
        0x0034001D,
        0x12 << 24,
        ftKbDa_SpecialN_Anim,
        ftPe_SpecialN_IASA,
        ftPe_SpecialN_Phys,
        ftKbDa_SpecialN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 1, ftcmd 1 "DaSpecialNHit" */
        1,
        0x0034001D,
        0x12 << 24,
        ftKbDa_SpecialNHit_Anim,
        ftPe_SpecialNHit_IASA,
        ftPe_SpecialNHit_Phys,
        ftKbDa_SpecialNHit_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 2, ftcmd 2 "DaSpecialAirN" */
        2,
        0x0034041D,
        0x12 << 24,
        ftKbDa_SpecialAirN_Anim,
        ftPe_SpecialAirN_IASA,
        ftKbDa_SpecialAirN_Phys,
        ftKbDa_SpecialAirN_Coll,
        ftCamera_UpdateCameraBox,
    },
    {
        /* state 3, ftcmd 3 "DaSpecialAirNHit" */
        3,
        0x0034041D,
        0x12 << 24,
        ftKbDa_SpecialAirNHit_Anim,
        ftPe_SpecialAirNHit_IASA,
        ftKbDa_SpecialAirNHit_Phys,
        ftKbDa_SpecialAirNHit_Coll,
        ftCamera_UpdateCameraBox,
    },
};

/* The kbFunction exports of PlKbCpDa.dat: 0, 1, 2, 3, 5 and 6. Slots 4 (OnKirbyHurt) and 7 are
 * empty m-ex slots. */
const MuAkKirbyCopy ftKbDa_Copy = {
    .on_swallow = ftKbDa_OnSwallow,
    .on_lose = ftKbDa_OnLose,
    .special_n = ftKbDa_SpecialN_Enter,
    .special_air_n = ftKbDa_SpecialAirN_Enter,
    .on_hurt = NULL,
    .init_items = ftKbDa_InitCopyItems,
    .move_logic = ftKbDa_MotionStateTable,
    .move_logic_count = ftKbDa_State_Count,
    .on_frame = NULL,
};
