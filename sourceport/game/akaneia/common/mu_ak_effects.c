/* The added fighters' own effects, native (CREATION_LAYER_PLAN.md step 7).
 *
 * A fighter m-ex adds has one effect file (EfWfData.dat for Wolf), named in the disc's effect file
 * table and loaded by the game's own effect loader under the fighter's effect file index
 * (ftData_UnkBytePerCharacter, filled by mu_ak_fighters.c). Its code and its animation scripts name
 * the effects of that file by a number relative to the fighter:
 *
 *     5000 + n   model effect n of the fighter's file
 *     6000 + n   particle generator n of the fighter's file
 *
 * The game's own id for either one is (effect file index * 1000 + n). How an effect is placed
 * (at a position, on a joint, turned by the facing direction, ...) is not in the caller: the file
 * carries a table, "effBehaviorTable", with one placement type per effect. This file does what the
 * m-ex engine patches do with that table, as plain C: the direct spawn (efSync_Spawn), the spawn
 * from a fighter's or an item's animation script, and the deferred spawn in between.
 *
 * A Kirby who holds the ability of an added fighter is served from that fighter's file (5000 and
 * 6000; m-ex hooks at 8005FF38 and 8006747C). The ranges 7000 and 8000 (which m-ex reads through
 * the asking fighter's copied kind when it is not Kirby) are not served: nothing on Akaneia 1.0.1
 * was seen to use them. */
#include <dolphin/os.h>
#include <melee/ef/efasync.h>
#include <melee/ef/efdata.h>
#include <melee/ef/eflib.h>
#include <melee/ef/efsync.h>
#include <melee/ef/types.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/types.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/generator.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/psstructs.h>

#include <math.h>
#include <stdarg.h>
#include <stddef.h>

#include "mu_disc.h"
#include "../mu_ak_fighter.h"

#ifdef MU_AKANEIA_FIGHTERS

#define AK_EF_MODEL_START 5000
#define AK_EF_PTCL_START 6000
#define AK_EF_COPY_START 7000
#define AK_EF_END 9000
#define AK_EF_FILES EF_DAT_FILE_MAX   /* the game's effect file table holds this many loadable files */
#define AK_EF_ASYNC_KIND 9      /* queued spawn kinds from here on are placement types */

extern EF_DAT_Entry efAsync_DatEntries[EF_DAT_FILE_MAX + 1];

/* "effBehaviorTable" of an effect file: one placement type per model effect and per particle
 * generator. */
typedef struct AkEffBehavior {
    u32 model_count;
    DISC_PTR(u8) model_type;
    u32 ptcl_count;
    DISC_PTR(u8) ptcl_type;
} DISC_STRUCT AkEffBehavior;

/* Placement types of a model effect. */
enum {
    AK_MDL_POS_ROT,            /* at a position; Z rotation from a float */
    AK_MDL_POS,                /* at a position; the owner's scale */
    AK_MDL_POS_GROUND,         /* at a position; Z rotation from the ground angle */
    AK_MDL_JOINT_POS_ROT,      /* at a joint's position, with its Z rotation */
    AK_MDL_POS_FACING,         /* at a position; turned by the facing direction; the owner's scale */
    AK_MDL_FOLLOW,             /* follows a joint */
    AK_MDL_FOLLOW_ROT,         /* follows a joint, as its child */
    AK_MDL_TYPES
};

/* Placement types of a particle generator. */
enum {
    AK_PTCL_POS,               /* at a position */
    AK_PTCL_POS_ROT,           /* at a position, turned by the facing direction */
    AK_PTCL_POS_ROT_GROUND,
    AK_PTCL_POS_FACING,
    AK_PTCL_POS_FACING_GROUND, /* nothing is spawned for this type */
    AK_PTCL_FOLLOW,            /* follows a joint; sized by the owner's scale */
    AK_PTCL_FOLLOW_FACING,     /* follows a joint, turned by the facing direction */
    AK_PTCL_FOLLOW_SCALE,      /* follows a joint with the owner's scale */
    AK_PTCL_TYPES
};

/* The behavior table of each loaded effect file, an added fighter's own or a retail fighter's
 * (ACE's clones use the retail file of the fighter they are built on, with their effects added
 * to it and to its behavior table). An entry is only used while the game holds the file's data
 * (efAsync_DatEntries[index].data), which the game clears at every scene start. */
static AkEffBehavior* ak_behavior[AK_EF_FILES];

/* The fighter that created an added item (m-ex keeps this in the item as its first owner; the
 * item's current owner changes when the item is reflected). */
#define AK_ITEM_OWNERS 48
static struct {
    HSD_GObj* item;
    HSD_GObj* owner;
} ak_item_owner[AK_ITEM_OWNERS];
static int ak_item_owner_next;

/* Set while a deferred spawn hands a position to a type that takes a joint when called
 * directly. */
static int ak_deferred_position;

/* The id of the last efSync_Spawn call (ef/efsync.c), for the log line of
 * mu_ak_effect_file_missing. */
int mu_ak_effect_asked;

/* One log line per effect file that was asked for while not loaded; the last entry stands for
 * every index outside the table. Cleared for a file when it is loaded. */
static u8 ak_missing_logged[AK_EF_FILES + 1];

/* efAsync_LoadSync: an effect file was loaded (or found already loaded) under `index`. */
void mu_ak_effect_file_loaded(int index, HSD_Archive* archive)
{
    if (index < 0 || index >= AK_EF_FILES) {
        return;
    }
    ak_missing_logged[index] = 0;
    ak_behavior[index] =
        archive != NULL ? HSD_ArchiveGetPublicAddress(archive, "effBehaviorTable") : NULL;
}

/* ---- the first owner of an added item ---- */

void mu_ak_item_owner_set(HSD_GObj* item, HSD_GObj* owner)
{
    int i;
    for (i = 0; i < AK_ITEM_OWNERS; i++) {
        if (ak_item_owner[i].item == item) {
            ak_item_owner[i].owner = owner;
            return;
        }
    }
    ak_item_owner[ak_item_owner_next].item = item;
    ak_item_owner[ak_item_owner_next].owner = owner;
    ak_item_owner_next = (ak_item_owner_next + 1) % AK_ITEM_OWNERS;
}

/* it_8026B894: `owner` is going away; no item keeps it as its first owner. */
void mu_ak_item_owner_forget(HSD_GObj* owner)
{
    int i;
    for (i = 0; i < AK_ITEM_OWNERS; i++) {
        if (ak_item_owner[i].owner == owner) {
            ak_item_owner[i].owner = NULL;
        }
    }
}

static HSD_GObj* item_first_owner(HSD_GObj* item_gobj)
{
    Item* ip = item_gobj->user_data;
    int i;
    if (ip == NULL) {
        return NULL;
    }
    if (ip->kind > It_Kind_Kyasarin_Egg) {
        for (i = 0; i < AK_ITEM_OWNERS; i++) {
            if (ak_item_owner[i].item == item_gobj) {
                return ak_item_owner[i].owner;
            }
        }
    }
    return ip->owner;
}

/* efLib_Create: model effect `id` (the game's own id: effect file index * 1000 + n) is asked for
 * by `parent_gobj`. Returns 1 when the effect file is not loaded, so there is no descriptor to
 * read and nothing is made; 0 when the game goes on as usual.
 *
 * This is the retail effect id of borrowed retail code. m-ex leaves ids up to 1298 to the game's
 * own switch (its hook at 8005FF38 starts with that test), which picks the retail fighter's file
 * and entry: Link's spin attack, 1211, is entry 0 of file 6 whoever asks. An added fighter loads
 * the file MxDt names for it (Zero: file 52), so file 6 is only there when a Link is in the
 * match. With it the effect shows, as on the mod; without it the mod reads the descriptor at
 * address 0 plus the entry and makes nothing (an emulator hands such a read 0, so the joint is
 * NULL and efLib_Create gives up; a console has no memory there). Particle generators need no
 * such test: hsd_8039F05C refuses an id past the count of a bank that is not loaded. */
int mu_ak_effect_file_missing(int id, HSD_GObj* parent_gobj)
{
    const int file = id < 0 ? -1 : id / 1000;
    const int slot = file >= 0 && file < AK_EF_FILES ? file : AK_EF_FILES;
    const char* name = "none";
    HSD_GObj* owner = parent_gobj;
    int kind = -1;
    if (slot == file && efAsync_DatEntries[file].data != NULL) {
        return 0;
    }
    if (ak_missing_logged[slot]) {
        return 1;
    }
    ak_missing_logged[slot] = 1;
    if (slot == file && efAsync_DatEntries[file].ef_DAT_file != NULL) {
        name = efAsync_DatEntries[file].ef_DAT_file;
    }
    if (owner != NULL && owner->classifier == HSD_GOBJ_CLASS_ITEM) {
        owner = item_first_owner(owner);
    }
    if (owner != NULL && owner->classifier == HSD_GOBJ_CLASS_FIGHTER && owner->user_data != NULL) {
        kind = ((Fighter*) owner->user_data)->kind;
    }
    OSReport("[ak] effect %d: fighter kind %d asks for model effect %d of effect file %d (%s), which is not loaded; nothing is spawned (logged once per file)\n",
             mu_ak_effect_asked, kind, id, file, name);
    return 1;
}

/* The fighter kind whose effect file serves `fp`: an added fighter's own, or for a Kirby the
 * added fighter he copied (akaneia/common/mu_ak_kirby.c). Ft_Kind_None when neither. */
static int effect_kind(Fighter* fp)
{
    if (MU_AK_KIND(fp->kind)) {
        return fp->kind;
    }
    return mu_ak_kirby_copy_kind(fp);
}

/* The fighter whose spawn is served from an added fighter's effect file, asked for by `gobj` (the
 * fighter itself, or an item it created): an added fighter, or a Kirby who holds the ability of
 * one. NULL when there is none. */
static Fighter* effect_fighter(HSD_GObj* gobj)
{
    Fighter* fp;
    if (gobj == NULL || !mu_mex_active()) {
        return NULL;
    }
    if (gobj->classifier == HSD_GOBJ_CLASS_ITEM) {
        gobj = item_first_owner(gobj);
        if (gobj == NULL || !ftLib_80086960(gobj)) {
            return NULL;
        }
    } else if (gobj->classifier != HSD_GOBJ_CLASS_FIGHTER) {
        return NULL;
    }
    fp = gobj->user_data;
    return fp != NULL && MU_AK_KIND(effect_kind(fp)) ? fp : NULL;
}

/* The same lookup for the sound code (mu_ak_sound.c). */
Fighter* mu_ak_effect_owner(HSD_GObj* gobj)
{
    return effect_fighter(gobj);
}

/* Resolves a fighter-relative effect id. Returns 1 with the game's own id, the kind (0 model,
 * 1 particle) and the placement type; 0 (after a log line) when the fighter has no such effect. */
static int effect_resolve(Fighter* fp, int gfx_id, int* game_id, int* is_ptcl, int* type)
{
    /* The file is the copied fighter's when a Kirby asks. A kind that is not an added one (a
     * Kirby who lost the ability between the request and a deferred spawn) has none. */
    const int kind = effect_kind(fp);
    const int file = MU_AK_KIND(kind) ? ftData_UnkBytePerCharacter[kind] : -1;
    const int ptcl = gfx_id >= AK_EF_PTCL_START;
    const int n = gfx_id - (ptcl ? AK_EF_PTCL_START : AK_EF_MODEL_START);
    AkEffBehavior* table;
    u8* types;
    u32 count;
    if (gfx_id < AK_EF_MODEL_START || gfx_id >= AK_EF_COPY_START) {
        return 0;   /* 7000 and 8000: not served */
    }
    if (file < 0 || file >= AK_EF_FILES || efAsync_DatEntries[file].data == NULL ||
        (table = ak_behavior[file]) == NULL)
    {
        OSReport("[ak] effect %d: fighter kind %d has no loaded effect file with a behavior table (file index %d)\n",
                 gfx_id, kind, file);
        return 0;
    }
    count = ptcl ? table->ptcl_count : table->model_count;
    types = ptcl ? DP(table->ptcl_type) : DP(table->model_type);
    if ((u32) n >= count || types == NULL) {
        OSReport("[ak] effect %d: fighter kind %d does not have %s effect %d (its file has %u)\n",
                 gfx_id, kind, ptcl ? "particle" : "model", n, (unsigned) count);
        return 0;
    }
    *game_id = file * 1000 + n;
    *is_ptcl = ptcl;
    *type = types[n];
    return 1;
}

int mu_ak_effect_id(int gfx_id)
{
    return gfx_id >= AK_EF_MODEL_START && gfx_id < AK_EF_END;
}

static float facing_turn(float facing_dir)
{
    return facing_dir < 0.0f ? (float) -M_PI_2 : (float) M_PI_2;
}

static void copy_owner_scale(HSD_JObj* effect_jobj, HSD_GObj* gobj)
{
    HSD_JObj* owner_jobj = gobj->hsd_obj;
    if (effect_jobj != NULL && owner_jobj != NULL) {
        effect_jobj->scale = owner_jobj->scale;
    }
}

static EF_Effect* spawn_model(int id, int type, HSD_GObj* gobj, va_list args)
{
    EF_Effect* effect = NULL;
    HSD_JObj* jobj;
    Vec3* pos;
    float* value;
    switch (type) {
    case AK_MDL_POS_ROT:
        pos = va_arg(args, Vec3*);
        effect = efLib_Create_Attach_Pos(id, gobj, pos);
        if (effect != NULL) {
            value = va_arg(args, float*);
            jobj = effect->gobj->hsd_obj;
            jobj->rotate.z = *value;
        }
        break;
    case AK_MDL_POS:
        pos = va_arg(args, Vec3*);
        effect = efLib_Create_Attach_Pos(id, gobj, pos);
        if (effect != NULL) {
            copy_owner_scale(effect->gobj->hsd_obj, gobj);
        }
        break;
    case AK_MDL_POS_GROUND:
        /* The arguments are the deferred spawn's: position, facing direction, ground angle. */
        pos = va_arg(args, Vec3*);
        effect = efLib_Create_Attach_Pos(id, gobj, pos);
        if (effect != NULL) {
            (void) va_arg(args, float*);
            value = va_arg(args, float*);
            jobj = effect->gobj->hsd_obj;
            jobj->rotate.z = *value;
        }
        break;
    case AK_MDL_JOINT_POS_ROT:
        if (ak_deferred_position) {
            /* A deferred spawn has only the position left. */
            pos = va_arg(args, Vec3*);
            effect = efLib_Create_Attach_Pos(id, gobj, pos);
            if (effect != NULL) {
                jobj = effect->gobj->hsd_obj;
                jobj->rotate.y = 0.0f;
            }
        } else {
            HSD_JObj* parent = va_arg(args, HSD_JObj*);
            Vec3 world;
            lb_8000B1CC(parent, NULL, &world);
            effect = efLib_Create_Attach_Pos(id, gobj, &world);
            if (effect != NULL) {
                jobj = effect->gobj->hsd_obj;
                jobj->rotate.y = 0.0f;
                jobj->rotate.z = parent->rotate.z;
            }
        }
        break;
    case AK_MDL_POS_FACING:
        pos = va_arg(args, Vec3*);
        effect = efLib_Create_Attach_Pos(id, gobj, pos);
        if (effect != NULL) {
            value = va_arg(args, float*);
            jobj = effect->gobj->hsd_obj;
            jobj->rotate.y = facing_turn(*value);
            copy_owner_scale(jobj, gobj);
        }
        break;
    case AK_MDL_FOLLOW:
        effect = efLib_Create_Attach_Scale(id, gobj, va_arg(args, HSD_JObj*));
        break;
    case AK_MDL_FOLLOW_ROT:
        effect = efLib_Create_AttachChild_Scale(id, gobj, va_arg(args, HSD_JObj*));
        break;
    default:
        break;
    }
    return effect;
}

static void* spawn_ptcl(int id, int type, HSD_GObj* gobj, va_list args)
{
    HSD_Generator* gen = NULL;
    Vec3* pos;
    float* value;
    switch (type) {
    case AK_PTCL_POS:
        pos = va_arg(args, Vec3*);
        gen = hsd_8039F05C(0, id / 1000, id);
        if (gen != NULL) {
            gen->pos = *pos;
        }
        break;
    case AK_PTCL_POS_ROT:
    case AK_PTCL_POS_ROT_GROUND:
    case AK_PTCL_POS_FACING:
        pos = va_arg(args, Vec3*);
        value = va_arg(args, float*);
        gen = efLib_CreateGenerator_Translate_FacingDir(id, pos, *value);
        break;
    case AK_PTCL_FOLLOW:
        gen = hsd_8039EFAC(0, id / 1000, id, va_arg(args, HSD_JObj*));
        if (gen != NULL) {
            HSD_JObj* owner_jobj = gobj->hsd_obj;
            if (owner_jobj != NULL) {
                gen->size *= owner_jobj->scale.x;
            }
        }
        break;
    case AK_PTCL_FOLLOW_FACING:
        gen = efLib_CreateGenerator_AppSRT_SetFacingDir(id, args);
        break;
    case AK_PTCL_FOLLOW_SCALE:
        gen = efLib_CreateGenerator_Attach_Scale(id, args, gobj);
        break;
    default:
        break;
    }
    return gen;
}

/* efSync_Spawn, for an id from 5000 to 8999: the fighter's own effect, placed the way its file
 * says. `args` are the caller's variable arguments. Returns what efSync_Spawn returns (the
 * EF_Effect of a model effect, the generator of a particle effect), NULL when nothing spawned. */
void* mu_ak_effect_sync(int gfx_id, HSD_GObj* gobj, va_list args)
{
    Fighter* fp = effect_fighter(gobj);
    int id, is_ptcl, type;
    void* result;
    if (fp == NULL || !effect_resolve(fp, gfx_id, &id, &is_ptcl, &type)) {
        return NULL;
    }
    if (is_ptcl) {
        return spawn_ptcl(id, type, gobj, args);
    }
    result = spawn_model(id, type, gobj, args);
    /* The fighter's model effects stop and resume with its hitlag. */
    fp->pre_hitlag_cb = efLib_PauseAll;
    fp->post_hitlag_cb = efLib_ResumeAll;
    return result;
}

/* A queued spawn of one of the fighter's own effects: what the animation script gave (the joint,
 * the offset from it, the facing direction, the ground angle), with the placement type as its
 * spawn kind. NULL when the owner has no such effect. */
static EF_QueuedEffect* queue_make(Fighter* owner, int gfx_id, HSD_JObj* joint, Vec3* offset,
                                   float facing_dir, float ground_angle)
{
    int id, is_ptcl, type;
    EF_QueuedEffect* queued;
    if (!effect_resolve(owner, gfx_id, &id, &is_ptcl, &type)) {
        return NULL;
    }
    queued = HSD_ObjAlloc(&efAsync_AllocData);
    if (queued == NULL) {
        return NULL;
    }
    queued->next = NULL;
    queued->spawn_kind = (u8) (AK_EF_ASYNC_KIND + type);
    queued->gfx_id = gfx_id;
    queued->jobj = joint;
    queued->params = *offset;
    queued->extra1 = facing_dir;
    queued->extra2 = ground_angle;
    return queued;
}

static void queue_or_run(HSD_GObj* gobj, EF_QueuedEffect** head, EF_QueuedEffect* queued)
{
    if (HSD_GObj_CurrentInvokedProc != NULL && HSD_GObj_CurrentInvokedProc->s_link < 9U) {
        queued->next = *head;
        *head = queued;
        return;
    }
    efAsync_QueueProcessDeferred(gobj, queued);
}

/* ftCo_8009F834 (a fighter's animation script asks for effect `gfx_id` on `joint` at `offset`).
 * Returns 1 when the id is one of the fighter's own effects (spawned or not). */
int mu_ak_effect_script(HSD_GObj* gobj, int gfx_id, HSD_JObj* joint, Vec3* offset)
{
    Fighter* fp;
    EF_QueuedEffect* queued;
    float ground_angle = 0.0f;
    if (!mu_ak_effect_id(gfx_id) || (fp = effect_fighter(gobj)) == NULL) {
        return 0;
    }
    if (fp->ground_or_air == GA_Ground) {
        ground_angle = atan2f(-fp->coll_data.floor.normal.x, fp->coll_data.floor.normal.y);
    }
    queued = queue_make(fp, gfx_id, joint, offset, fp->facing_dir, ground_angle);
    if (queued != NULL) {
        queue_or_run(gobj, &fp->x60C, queued);
    }
    return 1;
}

/* it_80278800 (an item's animation script asks for effect `gfx_id`). The effect comes from the
 * file of the fighter that created the item. Returns 1 when the id is in the fighters' range. */
int mu_ak_effect_item_script(HSD_GObj* item_gobj, int gfx_id, HSD_JObj* joint, Vec3* offset)
{
    Item* ip;
    Fighter* owner;
    EF_QueuedEffect* queued;
    float ground_angle = 0.0f;
    if (!mu_ak_effect_id(gfx_id) || !mu_mex_active()) {
        return 0;
    }
    ip = item_gobj->user_data;
    owner = effect_fighter(item_gobj);
    if (owner == NULL) {
        return 1;   /* an item with no first owner spawns nothing */
    }
    if (ip->ground_or_air == GA_Ground) {
        ground_angle =
            atan2f(-ip->x378_itemColl.floor.normal.x, ip->x378_itemColl.floor.normal.y);
    }
    queued = queue_make(owner, gfx_id, joint, offset, ip->facing_dir, ground_angle);
    if (queued != NULL) {
        queue_or_run(item_gobj, &ip->xBC0, queued);
    }
    return 1;
}

/* efAsync_QueueProcessDeferred: a queued spawn whose id is one of the fighters' own effects goes
 * to efSync_Spawn with the arguments its placement type takes, whatever spawn kind queued it.
 * Returns 1 when it was handled (the caller frees the entry). */
int mu_ak_effect_deferred(HSD_GObj* gobj, EF_QueuedEffect* queued)
{
    const int gfx_id = queued->gfx_id;
    Fighter* fp;
    int id, is_ptcl, type;
    Vec3 pos;
    if (!mu_ak_effect_id(gfx_id) || (fp = effect_fighter(gobj)) == NULL ||
        !effect_resolve(fp, gfx_id, &id, &is_ptcl, &type))
    {
        /* One of this file's own spawn kinds must not reach the game's switch. */
        return queued->spawn_kind >= AK_EF_ASYNC_KIND;
    }
    if (!is_ptcl) {
        switch (type) {
        case AK_MDL_POS_ROT:
        case AK_MDL_POS:
        case AK_MDL_POS_GROUND:
        case AK_MDL_JOINT_POS_ROT:
        case AK_MDL_POS_FACING:
            lb_8000B1CC(queued->jobj, &queued->params, &pos);
            ak_deferred_position = 1;
            efSync_Spawn(gfx_id, gobj, &pos, &queued->extra1, &queued->extra2);
            ak_deferred_position = 0;
            break;
        case AK_MDL_FOLLOW:
        case AK_MDL_FOLLOW_ROT:
            efSync_Spawn(gfx_id, gobj, queued->jobj);
            break;
        default:
            break;
        }
        return 1;
    }
    switch (type) {
    case AK_PTCL_POS:
        lb_8000B1CC(queued->jobj, &queued->params, &pos);
        efSync_Spawn(gfx_id, gobj, &pos, &queued->extra1, &queued->extra2);
        break;
    case AK_PTCL_POS_FACING:
        lb_8000B1CC(queued->jobj, &queued->params, &pos);
        efSync_Spawn(gfx_id, gobj, &pos, &queued->extra1);
        break;
    case AK_PTCL_FOLLOW:
    case AK_PTCL_FOLLOW_SCALE:
        efSync_Spawn(gfx_id, gobj, queued->jobj);
        break;
    case AK_PTCL_FOLLOW_FACING:
        efSync_Spawn(gfx_id, gobj, queued->jobj, &queued->extra1);
        break;
    default:
        /* The two position-and-rotation types and the ground type spawn nothing from a
         * script. */
        break;
    }
    return 1;
}

#endif /* MU_AKANEIA_FIGHTERS */
