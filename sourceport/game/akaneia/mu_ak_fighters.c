/* Akaneia's added fighters: the registry between the m-ex data (shim/mu_mex.c) and the fighters'
 * C (sourceport/game/akaneia/<fighter>/).
 *
 * m-ex numbers the fighters it adds right after Roy (internal 27 on) and moves the special fighters
 * (Master Hand to Sandbag) behind them. The native game keeps every retail kind where it is and gives
 * each added fighter the kind MU_AK_KIND_BASE + (its m-ex internal id - Ft_Kind_MasterH), past
 * Ft_Kind_None. The per-kind tables are sized FT_KIND_TABLE_MAX (ft/forward.h), and in the mod view
 * this file writes each added fighter's slots the way m-ex's loader overloads its tables: the
 * fighter's own C function, or else the m-ex default (the retail function MxDt.dat names, found by
 * comparing it with MxDt's entries for the retail fighters). In the retail view the slots go back to
 * NULL. Retail kinds read exactly the entries they always did, so retail play, online and replays
 * are untouched.
 *
 * m-ex hooks with no retail table (float, double jump, tether, landing, the three smashes) are kept
 * here and read through mu_ak_hook() at their call sites. The added fighters' articles (item kinds
 * past the retail ones) are served to the item code from the same registry.
 *
 * INTEGRATION.md (this folder) maps every field to its decomp site. */
#include <dolphin/os.h>
#include <melee/ft/forward.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftdemo.h>
#include <melee/ft/kinds/ftCommon/ftCo_JumpAerial.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/kinds/types.h>

#include <stddef.h>

#include "mu_ak_fighter.h"

_Static_assert(MU_AK_KIND_BASE == Ft_Kind_Max + 1, "added fighter kinds start right after Ft_Kind_None");
_Static_assert(FT_KIND_TABLE_MAX == MU_FT_KIND_CAP, "the per-kind tables are widened for the added fighters");

/* The retail per-kind tables ftdata.h does not declare (defined in ft/ftdata.c). */
extern MotionState* ftData_CharacterStateTables[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_OnItemInvisible[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_OnItemVisible[FT_KIND_TABLE_MAX];
extern Fighter_ItemEvent ftData_OnItemDropExt[FT_KIND_TABLE_MAX];
extern Fighter_ItemEvent ftData_OnItemPickup[FT_KIND_TABLE_MAX];
extern Fighter_ItemEvent ftData_OnItemDrop[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_UnkMotionStates1[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_UnkMotionStates2[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_OnKnockbackEnter[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftData_OnKnockbackExit[FT_KIND_TABLE_MAX];
extern HSD_GObjEvent ftKindCalcIndiviParamTable[FT_KIND_TABLE_MAX];
struct MuAkCallbackPair {
    HSD_GObjEvent x0;
    void (*x4)(HSD_GObj*, int, float);
};
extern struct MuAkCallbackPair ftData_UnkCallbackPairs0[FT_KIND_TABLE_MAX];

/* ---- the fighters this build has (CMakeLists.txt defines MU_AK_HAVE_<FIGHTER> per file present) */
static const MuAkFighter* const mu_ak_registry[] = {
#ifdef MU_AK_HAVE_WOLF
    &mu_ak_wolf,
#endif
#ifdef MU_AK_HAVE_DIDDY
    &mu_ak_diddy,
#endif
#ifdef MU_AK_HAVE_CHARIZARD
    &mu_ak_charizard,
#endif
#ifdef MU_AK_HAVE_LUCAS
    &mu_ak_lucas,
#endif
#ifdef MU_AK_HAVE_SONIC
    &mu_ak_sonic,
#endif
#ifdef MU_AK_HAVE_DEDEDE
    &mu_ak_dedede,
#endif
#ifdef MU_AK_HAVE_TAILS
    &mu_ak_tails,
#endif
    NULL,
};

/* ---- the table slots the registry writes: m-ex ftFunction index -> decomp table ---- */
#define NO_FIELD -1
#define FIELD(f) ((short) offsetof(MuAkFighter, f))
#define TABLE(t) (void*) &(t)[0], (unsigned short) sizeof((t)[0])

enum {
    MU_AK_SLOT_NO_DEFAULT = 1,   /* m-ex's own table does not line up with the decomp one */
    MU_AK_SLOT_NOOP = 2,         /* the call site asserts the entry: an empty slot gets a no-op */
};

typedef struct MuAkSlotSpec {
    unsigned char mex;       /* m-ex ftFunction index (MexTK/ftFunction.txt, MxDt fighter_function) */
    short field;             /* offsetof(MuAkFighter, ...), NO_FIELD when a fighter cannot set it */
    void* base;              /* &table[0] */
    unsigned short stride;
    unsigned char flags;
} MuAkSlotSpec;

static const MuAkSlotSpec mu_ak_slots[] = {
    { 0, FIELD(onload), TABLE(ftData_OnLoad), 0 },
    { 1, FIELD(ondeath), TABLE(ftData_OnDeath), 0 },
    { 2, FIELD(onunknown), TABLE(ftData_OnUserDataRemove), 0 },
    { 3, FIELD(move_logic), TABLE(ftData_CharacterStateTables), 0 },
    { 4, FIELD(specialn), TABLE(ftData_SpecialN), 0 },
    { 5, FIELD(specialairn), TABLE(ftData_SpecialAirN), 0 },
    { 6, FIELD(specials), TABLE(ftData_SpecialS), 0 },
    { 7, FIELD(specialairs), TABLE(ftData_SpecialAirS), 0 },
    { 8, FIELD(specialhi), TABLE(ftData_SpecialHi), 0 },
    { 9, FIELD(specialairhi), TABLE(ftData_SpecialAirHi), 0 },
    { 10, FIELD(speciallw), TABLE(ftData_SpecialLw), 0 },
    { 11, FIELD(specialairlw), TABLE(ftData_SpecialAirLw), 0 },
    { 12, FIELD(onabsorb), TABLE(ftData_OnAbsorb), 0 },
    { 13, FIELD(onitempickup), TABLE(ftData_OnItemPickupExt), 0 },
    { 14, FIELD(onmakeiteminvisible), TABLE(ftData_OnItemInvisible), 0 },
    { 15, FIELD(onmakeitemvisible), TABLE(ftData_OnItemVisible), 0 },
    { 16, FIELD(onitemdrop), TABLE(ftData_OnItemDropExt), 0 },
    { 17, FIELD(onitemcatch), TABLE(ftData_OnItemPickup), 0 },
    { 18, FIELD(onunknownitemrelated), TABLE(ftData_OnItemDrop), 0 },
    { 19, FIELD(onunknowncharactermodelflags1), TABLE(ftData_UnkMotionStates1), 0 },
    { 20, FIELD(onunknowncharactermodelflags2), TABLE(ftData_UnkMotionStates2), 0 },
    { 21, FIELD(onhit), TABLE(ftData_OnKnockbackEnter), 0 },
    { 22, FIELD(onunknowneyetexturerelated), TABLE(ftData_OnKnockbackExit), 0 },
    { 23, FIELD(onframe), TABLE(ftData_UnkMotionStates3), 0 },
    { 24, FIELD(onactionstatechange), TABLE(ftData_UnkMotionStates4), 0 },
    { 25, FIELD(onrespawn), TABLE(ftKindCalcIndiviParamTable), MU_AK_SLOT_NOOP },
    { 26, FIELD(onmodelrender), TABLE(ftData_UnkMtxFunc0), 0 },
    { 27, FIELD(onshadowrender), TABLE(ftData_UnkIntBoolFunc0.model_events), 0 },
    { 28, FIELD(onunknownmultijump), TABLE(ftData_UnkIntBoolFunc0.getter), 0 },
    /* m-ex writes this one into the pair table as if it were flat, so its defaults are not per
     * fighter; a fighter's own function goes to the first callback of its pair (ftAnim_80070654). */
    { 29, FIELD(onactionstatechangewhileeyetextureischanged), (void*) &ftData_UnkCallbackPairs0[0].x0,
      (unsigned short) sizeof(ftData_UnkCallbackPairs0[0]), MU_AK_SLOT_NO_DEFAULT },
    /* MoveLogicDemo: not a fighter export, only the MxDt default (Tails uses Mario's). */
    { 40, NO_FIELD, TABLE(ftData_UnkMotionStates0), 0 },
};

/* The m-ex hooks with no retail table (mu_native.h MU_AK_HOOK_*), by field. */
static const short mu_ak_hook_fields[] = {
    FIELD(enterfloat), FIELD(enterdoublejump), FIELD(entertether), FIELD(onlanding),
    FIELD(onsmashf), FIELD(onsmashhi), FIELD(onsmashlw),
};
#define MU_AK_HOOK_FIRST MU_AK_HOOK_FLOAT
#define MU_AK_HOOK_COUNT ((int) (sizeof mu_ak_hook_fields / sizeof mu_ak_hook_fields[0]))

/* ---- state, rebuilt at each content view change ---- */
static const MuAkFighter* ak_fighter[MU_AK_KIND_SLOTS];   /* by slot (kind - MU_AK_KIND_BASE) */
static signed char ak_mex[MU_AK_KIND_SLOTS];               /* m-ex internal id, -1 = no slot */
static void* ak_hooks[MU_AK_KIND_SLOTS][7];
static int ak_shift;                                      /* m-ex special fighter shift, 0 = off */

#define MU_AK_MAX_ARTICLES 128
typedef struct MuAkArticle {
    short item_kind;
    signed char slot, local;
    Article* data;   /* set by the fighter's onload (it_8026B3F8 / mu_ak_article_set) */
} MuAkArticle;
static MuAkArticle ak_articles[MU_AK_MAX_ARTICLES];
static int ak_article_count;

static void mu_ak_noop(HSD_GObj* gobj)
{
    (void) gobj;
}

static void* fighter_field(const MuAkFighter* ft, short field)
{
    return field == NO_FIELD ? NULL : *(void* const*) ((const char*) ft + field);
}

static void** table_slot(const MuAkSlotSpec* spec, int kind)
{
    return (void**) ((char*) spec->base + (size_t) kind * spec->stride);
}

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

static int same_file(const char* a, const char* b)
{
    while (*a != '\0' && ascii_lower(*a) == ascii_lower(*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* ---- kinds ---- */

/* The native kind of an m-ex internal fighter id: retail fighters keep their kind, the special
 * fighters move back to theirs, added fighters get their slot. -1 when out of range. */
int mu_ak_kind_from_mex(int mex_internal)
{
    if (mex_internal < 0) {
        return -1;
    }
    if (mex_internal < Ft_Kind_MasterH) {
        return mex_internal;
    }
    if (mex_internal < Ft_Kind_MasterH + ak_shift) {
        const int slot = mex_internal - Ft_Kind_MasterH;
        return slot < MU_AK_KIND_SLOTS ? MU_AK_KIND_BASE + slot : -1;
    }
    return mex_internal - ak_shift < Ft_Kind_Max ? mex_internal - ak_shift : -1;
}

int mu_ak_mex_internal(int kind)
{
    if (kind >= 0 && kind < Ft_Kind_MasterH) {
        return kind;
    }
    if (kind >= Ft_Kind_MasterH && kind < Ft_Kind_Max) {
        return kind + ak_shift;
    }
    if (MU_AK_KIND(kind)) {
        return ak_mex[kind - MU_AK_KIND_BASE];
    }
    return -1;
}

const MuAkFighter* mu_ak_fighter(int kind)
{
    return MU_AK_KIND(kind) ? ak_fighter[kind - MU_AK_KIND_BASE] : NULL;
}

/* ---- m-ex defaults ---- */

/* The retail kind whose MxDt entry in ftFunction table `table` is the same function as the added
 * fighter's, -1 when the entry is empty or matches no retail fighter. */
static int default_kind(int table, int mex_internal)
{
    const unsigned int want = mu_mex_fighter_function(table, mex_internal);
    const int count = mu_mex_fighter_internal_count();
    int m;
    if (want == 0) {
        return -1;
    }
    for (m = 0; m < count; m++) {
        if (m >= Ft_Kind_MasterH && m < Ft_Kind_MasterH + ak_shift) {
            continue;   /* another added fighter */
        }
        if (mu_mex_fighter_function(table, m) == want) {
            return mu_ak_kind_from_mex(m);
        }
    }
    OSReport("[ak] m-ex default %08X (ftFunction %d) of fighter %d is no retail fighter's; left empty\n",
             want, table, mex_internal);
    return -1;
}

/* The retail double jump the site's switch would pick for a retail kind. */
static void* retail_double_jump(int kind)
{
    switch (kind) {
    case Ft_Kind_Ness: return (void*) ftNs_JumpAerial_Enter;
    case Ft_Kind_Yoshi: return (void*) ftYs_JumpAerial_Enter;
    case Ft_Kind_Peach: return (void*) ftPe_JumpAerial_Enter;
    case Ft_Kind_Mewtwo: return (void*) ftMt_JumpAerial_Enter;
    default: return (void*) ftCo_JumpAerial_Enter_Basic;
    }
}

/* ---- articles ---- */

static void build_articles(void)
{
    int slot, local;
    ak_article_count = 0;
    for (slot = 0; slot < MU_AK_KIND_SLOTS; slot++) {
        if (ak_mex[slot] < 0) {
            continue;
        }
        for (local = 0; local < 127; local++) {
            const int item = mu_mex_fighter_item(ak_mex[slot], local);
            if (item < 0) {
                break;
            }
            if (ak_article_count == MU_AK_MAX_ARTICLES) {
                OSReport("[ak] more than %d added articles; the rest are not served\n", MU_AK_MAX_ARTICLES);
                return;
            }
            ak_articles[ak_article_count].item_kind = (short) item;
            ak_articles[ak_article_count].slot = (signed char) slot;
            ak_articles[ak_article_count].local = (signed char) local;
            ak_articles[ak_article_count].data = NULL;
            ak_article_count++;
        }
    }
}

static MuAkArticle* find_article(int item_kind)
{
    int i;
    for (i = 0; i < ak_article_count; i++) {
        if (ak_articles[i].item_kind == item_kind) {
            return &ak_articles[i];
        }
    }
    return NULL;
}

int mu_ak_item_kind(int kind, int local)
{
    const int mex = MU_AK_KIND(kind) ? ak_mex[kind - MU_AK_KIND_BASE] : -1;
    return mex >= 0 ? mu_mex_fighter_item(mex, local) : -1;
}

void mu_ak_article_store(int item_kind, void* article)
{
    MuAkArticle* a = find_article(item_kind);
    if (a == NULL) {
        OSReport("[ak] article data for item kind %d, which no added fighter owns; ignored\n", item_kind);
        return;
    }
    a->data = article;
}

void mu_ak_article_set(int item_kind, Article* article)
{
    mu_ak_article_store(item_kind, article);
}

/* Item_80267978: the article data and logic table of an added fighter's item kind. */
int mu_ak_article(int item_kind, void** article, void** logic)
{
    MuAkArticle* a = find_article(item_kind);
    const MuAkFighter* ft = a != NULL ? ak_fighter[a->slot] : NULL;
    if (ft == NULL || ft->articles == NULL || a->local >= ft->article_count || a->data == NULL) {
        if (a != NULL) {
            OSReport("[ak] item kind %d (article %d of fighter slot %d) has no %s\n", item_kind,
                     a->local, a->slot, ft == NULL ? "native fighter" : a->data == NULL ? "article data" : "logic table");
        }
        return 0;
    }
    *article = a->data;
    *logic = (void*) &ft->articles[a->local];
    return 1;
}

/* ---- the m-ex hooks with no retail table ---- */

void* mu_ak_hook(int kind, int hook)
{
    if (!MU_AK_KIND(kind) || hook < MU_AK_HOOK_FIRST || hook >= MU_AK_HOOK_FIRST + MU_AK_HOOK_COUNT) {
        return NULL;
    }
    return ak_hooks[kind - MU_AK_KIND_BASE][hook - MU_AK_HOOK_FIRST];
}

int mu_ak_call(int kind, int hook, struct HSD_GObj* gobj)
{
    HSD_GObjEvent fn = (HSD_GObjEvent) mu_ak_hook(kind, hook);
    if (fn == NULL) {
        return 0;
    }
    fn(gobj);
    return 1;
}

/* ---- the character select screen ---- */

int mu_ak_css_selectable(int ext)
{
    (void) ext;
    /* Not yet for any fighter: an added fighter also needs the creation layer described in
     * INTEGRATION.md ("What still blocks a fighter"): its character kind, fighter and animation
     * files, costumes and common data slots. Until that lands every added fighter stays locked,
     * whatever its registry state (MU_AK_READY). */
    return 0;
}

/* ---- the view change ---- */

static void clear_slots(void)
{
    size_t i;
    int slot;
    for (i = 0; i < sizeof mu_ak_slots / sizeof mu_ak_slots[0]; i++) {
        for (slot = 0; slot < MU_AK_KIND_SLOTS; slot++) {
            *table_slot(&mu_ak_slots[i], MU_AK_KIND_BASE + slot) = NULL;
        }
    }
    for (slot = 0; slot < MU_AK_KIND_SLOTS; slot++) {
        ak_fighter[slot] = NULL;
        ak_mex[slot] = -1;
        __builtin_memset(ak_hooks[slot], 0, sizeof ak_hooks[slot]);
    }
    ak_article_count = 0;
}

static const MuAkFighter* find_fighter(const char* file)
{
    int i;
    for (i = 0; mu_ak_registry[i] != NULL; i++) {
        if (mu_ak_registry[i]->file != NULL && same_file(mu_ak_registry[i]->file, file)) {
            return mu_ak_registry[i];
        }
    }
    return NULL;
}

static void fill_fighter(int slot, const MuAkFighter* ft)
{
    const int kind = MU_AK_KIND_BASE + slot;
    const int mex = ak_mex[slot];
    size_t i;
    int h;
    for (i = 0; i < sizeof mu_ak_slots / sizeof mu_ak_slots[0]; i++) {
        const MuAkSlotSpec* spec = &mu_ak_slots[i];
        void* fn = ft != NULL ? fighter_field(ft, spec->field) : NULL;
        if (fn == NULL && !(spec->flags & MU_AK_SLOT_NO_DEFAULT)) {
            const int as = default_kind(spec->mex, mex);
            if (as >= 0) {
                fn = *table_slot(spec, as);
            }
        }
        if (fn == NULL && (spec->flags & MU_AK_SLOT_NOOP)) {
            fn = (void*) mu_ak_noop;
        }
        *table_slot(spec, kind) = fn;
    }
    for (h = 0; h < MU_AK_HOOK_COUNT; h++) {
        void* fn = ft != NULL ? fighter_field(ft, mu_ak_hook_fields[h]) : NULL;
        if (fn == NULL && MU_AK_HOOK_FIRST + h == MU_AK_HOOK_DOUBLEJUMP) {
            const int as = default_kind(MU_AK_HOOK_DOUBLEJUMP, mex);
            fn = as >= 0 ? retail_double_jump(as) : NULL;
        }
        ak_hooks[slot][h] = fn;
    }
}

/* At each content view change (shim/mu_mex.c): the mod view fills the added fighters' slots, the
 * retail view (and a retail disc) leaves them empty. */
void mu_ak_apply(void)
{
    int slot, slots;
    clear_slots();
    ak_shift = mu_mex_special_kind_shift();
    if (!mu_mex_active() || ak_shift <= 0) {
        ak_shift = 0;
        return;
    }
    slots = ak_shift < MU_AK_KIND_SLOTS ? ak_shift : MU_AK_KIND_SLOTS;
    if (ak_shift > MU_AK_KIND_SLOTS) {
        OSReport("[ak] m-ex adds %d fighters; the native game has %d slots\n", ak_shift, MU_AK_KIND_SLOTS);
    }
    for (slot = 0; slot < slots; slot++) {
        const int mex = Ft_Kind_MasterH + slot;
        const char* file = mu_mex_fighter_file(mex);
        const MuAkFighter* ft;
        if (file == NULL) {
            continue;   /* an empty m-ex slot */
        }
        ak_mex[slot] = (signed char) mex;
        ft = find_fighter(file);
        ak_fighter[slot] = ft;
        if (ft == NULL) {
            OSReport("[ak] %s: kind %d, no native code in this build (locked)\n", file,
                     MU_AK_KIND_BASE + slot);
            continue;
        }
        fill_fighter(slot, ft);
        OSReport("[ak] %s (%s): kind %d, %s\n", ft->name != NULL ? ft->name : "?", file,
                 MU_AK_KIND_BASE + slot, (ft->flags & MU_AK_READY) ? "ready" : "in progress");
    }
    build_articles();
}
