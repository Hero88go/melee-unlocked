/* Akaneia's new fighters, native.
 *
 * Akaneia (an m-ex build) ships each new fighter's behavior as PowerPC code inside its Pl<Xx>.dat,
 * exported as the m-ex "ftFunction" table (the names in sourceport/game/tmce/MexTK/ftFunction.txt)
 * plus "move_logic", its table of special action states. The Source Port never runs that PowerPC:
 * each fighter's behavior is rewritten here as C against the decomp's own types, one file set per
 * fighter under sourceport/game/akaneia/<fighter>/, and described by one MuAkFighter.
 *
 * The per-kind dispatch (the decomp's ftData_* tables, ft/ftdata.h) is widened for Akaneia's extra
 * internal kinds by the integration layer (mu_ak_fighters.c); a NULL callback keeps the game's
 * default, exactly as an empty m-ex slot does. Data (models, animations, attributes, hitboxes) still
 * loads from the player's own Akaneia disc through the existing m-ex data layer (shim/mu_mex.c). */
#ifndef MU_AK_FIGHTER_H
#define MU_AK_FIGHTER_H

#include <melee/ft/forward.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/forward.h>
#include <melee/mp/forward.h>
#include <sysdolphin/baselib/forward.h>

typedef void (*MuAkEvent)(HSD_GObj* gobj);

typedef struct MuAkFighter {
    const char* name;          /* "Wolf" */
    const char* file;          /* the fighter file on the Akaneia disc, "PlWf.dat" */

    /* The m-ex ftFunction exports, same order and meaning (MexTK/ftFunction.txt). NULL = default.
     * Several MexTK names do not say what the slot is, and some slots are not (HSD_GObj*) events:
     * sourceport/game/akaneia/INTEGRATION.md maps each one to its decomp table, call site and real
     * signature. Store a function of the real signature, cast to MuAkEvent. */
    MuAkEvent onload;
    MuAkEvent ondeath;
    MuAkEvent onunknown;
    MuAkEvent specialn, specialairn;
    MuAkEvent specials, specialairs;
    MuAkEvent specialhi, specialairhi;
    MuAkEvent speciallw, specialairlw;
    MuAkEvent onabsorb;
    MuAkEvent onitempickup;
    MuAkEvent onmakeiteminvisible, onmakeitemvisible;
    MuAkEvent onitemdrop, onitemcatch;
    MuAkEvent onunknownitemrelated;
    MuAkEvent onunknowncharactermodelflags1, onunknowncharactermodelflags2;
    MuAkEvent onhit;
    MuAkEvent onunknowneyetexturerelated;
    MuAkEvent onframe;
    MuAkEvent onactionstatechange;
    MuAkEvent onrespawn;
    MuAkEvent onmodelrender, onshadowrender;
    MuAkEvent onunknownmultijump;
    MuAkEvent onactionstatechangewhileeyetextureischanged;
    MuAkEvent ontwoentrytable;
    MuAkEvent enterfloat, enterdoublejump, entertether;
    MuAkEvent onlanding;
    MuAkEvent onsmashf, onsmashhi, onsmashlw;

    /* move_logic: the fighter's own action states, numbered from ftCo_MS_Count (341) as in m-ex.
     * Each entry is a decomp MotionState (anim id, flags, move id, the four callbacks). */
    const MotionState* move_logic;
    int move_logic_count;

    /* ---- added by the integration layer (mu_ak_fighters.c); zero is always a valid value ---- */
    unsigned int flags;   /* MU_AK_READY once the fighter is complete enough to be played */
    /* The fighter's articles (projectiles and items it creates), in the order of its m-ex item
     * lookup (MxDt MEXItemLookup): entry i is the m-ex itFunction table of article i, which has the
     * decomp's ItemLogicTable fields in the same order. The game's item kind for article i is
     * mu_ak_item_kind(fp->kind, i). */
    const ItemLogicTable* articles;
    int article_count;
    /* The same list as pointers, for a fighter that keeps one table per article and has
     * articles with no code (a NULL entry). Used instead of `articles` when set. */
    ItemLogicTable* const* article_tables;
} MuAkFighter;

#define MU_AK_READY 0x1u

/* One per fighter (sourceport/game/akaneia/<fighter>/<fighter>.c). The build links a fighter in
 * when that file exists (CMakeLists.txt, MU_AK_HAVE_<FIGHTER>); until then the fighter stays locked
 * on the character select screen. */
extern const MuAkFighter mu_ak_wolf, mu_ak_diddy, mu_ak_charizard, mu_ak_lucas, mu_ak_sonic,
    mu_ak_dedede, mu_ak_tails;

/* ---- the integration layer's services to fighter code (mu_ak_fighters.c) ---- */
/* The registered fighter behind a native fighter kind (MU_AK_KIND(kind)), NULL for any other. */
const MuAkFighter* mu_ak_fighter(int kind);
/* The m-ex internal id of a native fighter kind (retail kinds are their own ids), -1 when none. */
int mu_ak_mex_internal(int kind);
/* The game's item kind for the fighter's article `local`, -1 when it has none. */
int mu_ak_item_kind(int kind, int local);
/* The article data (from the fighter's own file) for one of its item kinds. Done from the fighter's
 * onload, as retail fighters call it_8026B3F8, which also accepts these kinds. */
void mu_ak_article_set(int item_kind, Article* article);

#ifdef MU_AKANEIA_FIGHTERS
/* ---- the m-ex runtime services fighter code calls (common/mu_ak_services.c) ---- */
/* MEX_IndexFighterItem: article `index` of the fighter's own item list, from its onload. */
void mu_ak_register_article(FighterKind kind, void* article, int index);
/* MEX_GetFtItemID: the game's item kind of the fighter's article `index`, -1 when none. */
ItemKind mu_ak_article_kind(HSD_GObj* fighter_gobj, int index);
/* The loaded costume file of a fighter kind, and a public symbol of the fighter's own costume. */
HSD_Archive* mu_ak_costume_archive(FighterKind kind, int costume);
void* mu_ak_costume_symbol(Fighter* fp, const char* symbol);
/* The disc's display name of a native character kind. */
const char* mu_ak_fighter_name(int ckind);
/* The retail item creator as m-ex code calls it (it/item.c Item_8026862C): the caller's hold kind
 * is kept. */
#define MU_AK_HAVE_ITEM_CREATE 1
Item_GObj* mu_ak_item_create(SpawnItem* spawn);
/* The stage's collision joint list (mp/mplib.c). */
CollJoint* mu_ak_mp_joint_list(void);
/* m-ex MexCPU_Process: the game's CPU step with the fighter's own decision code in the middle
 * (ft/kinds/ftCommon/ftCo_0A01.c). */
void mu_ak_cpu_process(Fighter_GObj* gobj, void (*custom)(Fighter* fp));
/* Fills or clears the service tables some fighters keep (at each view change). */
void mu_ak_services_apply(int mod_view);
#endif

#endif
