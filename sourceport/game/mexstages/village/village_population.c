/* Background residents, GrAc +1498..16D0, +23B4..298C, +2E54..366C,
 * +3958..403C and +43FC..4624. Duplicate compiler copies at +4860, +49C0,
 * +4E60, +5324 share these native implementations. */
#include "village.h"
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmvs.h>
#include <melee/gr/granime.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbarchive.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/tobj.h>
#include <math.h>

static void ac_pick_target(HSD_GObj* gobj);

static struct UnkStageDat_x8_t* ac_map_data(int id)
{
    return &DP(grDatFiles_801C6330(id)->unk4->unk8)[id];
}

/* +2328: index is along the DObj sibling chain of the supplied joint. */
static HSD_DObj* ac_dobj(HSD_JObj* joint, int index)
{
    HSD_DObj* dobj = joint->u.dobj;
    while (index-- > 0) {
        if (dobj->next == NULL) { __assert("village.c", 0x1D8, "dobj not found"); }
        dobj = dobj->next;
    }
    return dobj;
}

void ac_material_frame(HSD_GObj* gobj, int index, int frame)
{
    HSD_DObj* dobj = ac_dobj(Ground_801C3FA4(gobj, 0), index);
    HSD_MObj* material;
    if (dobj == NULL || (material = dobj->mobj) == NULL || material->tobj == NULL ||
        material->tobj->aobj == NULL) { return; }
    HSD_AObjSetRate(material->tobj->aobj, 0.0f);
    HSD_MObjReqAnim(material, (float) frame);
    HSD_MObjAnim(material);
}

static void ac_eye(HSD_GObj* gobj, int frame)
{
    AC_GET(AcVillager, gobj)->eye = frame;
    ac_material_frame(gobj, 0, frame);
}

static void ac_mouth(HSD_GObj* gobj, int frame)
{
    AC_GET(AcVillager, gobj)->mouth = frame;
    ac_material_frame(gobj, 1, frame);
}

/* +2610: replace only joint animations. Material animations remain attached. */
void ac_add_animation(HSD_GObj* gobj, int index)
{
    Ground* gp = HSD_GObjGetUserData(gobj);
    struct UnkStageDat_x8_t* data = ac_map_data(gp->map_id);
    HSD_JObj* child = GET_JOBJ(gobj)->child;
    if (child == NULL) { return; }
    lb_8000B804(child->child, DP(DP(data->unk0)->child));
    HSD_JObjRemoveAnimByFlags(child, JOBJ_MASK);
    HSD_JObjAddAnimAll(child, DP(DP(data->unk4)[index]), NULL, NULL);
    HSD_JObjReqAnimAllByFlags(child, JOBJ_MASK, 0.0f);
    HSD_JObjAnimAll(child);
}

/* +3AD0: resident skeletons share animation slots from map gobj 12. */
static void ac_villager_animation(HSD_GObj* gobj, int index)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    HSD_JObj* child = GET_JOBJ(gobj)->child;
    HSD_JObj* head;
    float rotation;
    if (child == NULL) { return; }
    head = Ground_801C3FA4(gobj, 11);
    rotation = head->rotate.x;
    lb_8000B804(child, DP(v->data->joint));
    head->rotate.x = rotation;
    HSD_JObjRemoveAnimByFlags(child, JOBJ_MASK);
    HSD_JObjAddAnimAll(child, DP(DP(ac_map_data(12)->unk4)[index]), NULL, NULL);
    HSD_JObjReqAnimAllByFlags(child, JOBJ_MASK, 0.0f);
    HSD_JObjAnimAll(child);
    Ground_801C3FA4(gobj, 0)->translate = v->pos;
    Ground_801C3FA4(gobj, 0)->rotate.y = v->rotation;
}

void ac_set_action(HSD_GObj* gobj, int action, int duration)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    unsigned flags = v->data->flags;
    if (flags & 8) {
        if (action != 0) { return; }
        if (v->action != 0) { ac_villager_animation(gobj, flags & 0x10 ? 5 : 0); }
    } else if (v->action != action) {
        switch ((unsigned) action) {
        case 0: ac_villager_animation(gobj, flags & 0x10 ? 5 : 0); break;
        case 1: ac_villager_animation(gobj, flags & 0x10 ? 6 : 1); break;
        case 2: ac_villager_animation(gobj, 3); break;
        case 3: ac_villager_animation(gobj, 2); break;
        case 4:
            ac_villager_animation(gobj, 4);
            ac_eye(gobj, 2);
            v->following = 0;
            break;
        }
    }
    v->action = action;
    v->action_timer = duration;
}

void ac_set_mood(HSD_GObj* gobj, int mood, int duration)
{
    static const u8 eyes[5] = { 0, 4, 3, 5, 6 };
    static const u8 mouths[5] = { 0, 2, 3, 3, 3 };
    AcVillager* v = AC_GET(AcVillager, gobj);
    if (v->data->flags & 0xC) { return; }
    v->mood = mood;
    v->mood_timer = duration;
    if ((unsigned) mood <= 4) {
        ac_eye(gobj, eyes[mood]);
        ac_mouth(gobj, mouths[mood]);
    }
}

/* +4310: the inclusive comparison (draw <= cumulative) is retained. This biases the first
 * entry by one integer outcome, unlike HSD_Randi or a conventional weighted choice. */
static int ac_chance(const u8* chances)
{
    int total = 0, cumulative = 0, draw;
    const u8* row;
    for (row = chances; row[1] != 0xFF; row += 2) { total += row[1]; }
    draw = (int) ((float) total * HSD_Randf());
    for (row = chances; row[1] != 0xFF; row += 2) {
        cumulative += row[1];
        if (draw <= cumulative) { return row[0]; }
    }
    return 0;
}

MU_NO_CONTRACT static void ac_select_action(HSD_GObj* gobj, const u8* chances)
{
    int action = ac_chance(chances);
    float draw = HSD_Randf();
    float product = (float) (ac_param->action_max - ac_param->action_min) * draw;
    int duration = action == 3 ? 200 : (int) (product + (float) ac_param->action_min);
    ac_set_action(gobj, action, duration);
}

static void ac_action_from_mood(HSD_GObj* gobj)
{
    static const u8 human_happy[] = { 0, 50, 1, 25, 2, 20, 3, 5, 0, 255 };
    static const u8 animal_happy[] = { 0, 50, 1, 50, 0, 255 };
    static const u8 human_surprise[] = { 1, 75, 2, 25, 0, 255 };
    AcVillager* v = AC_GET(AcVillager, gobj);
    if ((unsigned) v->mood > 4) { return; }
    switch (v->mood) {
    case 1: ac_select_action(gobj, v->data->flags & 1 ? human_happy : animal_happy); break;
    case 3:
        if (v->data->flags & 1) { ac_select_action(gobj, human_surprise); }
        else if (v->action != 1) { ac_set_action(gobj, 1, 0); }
        break;
    default:
        if (v->action != 0) { ac_set_action(gobj, 0, 0); }
        break;
    }
}

static void ac_statistics(HSD_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    StaticPlayer* p = Player_GetPtrForSlot(v->player);
    int i;
    v->stocks = Player_GetStocks(v->player);
    /* +3340 starts at player +6C, so the six words are the second falls count and only the
     * first five KO counts. +3350 also compares the stamina/unknown packed halfword pair. */
    v->kos[0] = p->falls[1];
    for (i = 0; i < 5; i++) { v->kos[i + 1] = p->kos_by_player[i]; }
    v->stamina_word = ((u32) (u16) p->staminas.byName.stamina << 16) |
                       (u16) p->staminas.byName.unk66;
    v->falls = p->falls[0];
}

static bool ac_player_active(unsigned player)
{
    if (player > 6 || Player_GetEntity(player) == NULL) { return false; }
    return !gm_8016B094() || Player_GetStocks(player) > 0;
}

MU_NO_CONTRACT static void ac_look_at(HSD_GObj* gobj, float x)
{
    HSD_JObj* head = Ground_801C3FA4(gobj, 11);
    Vec3 pos;
    float angle, difference;
    lb_8000B1CC(head, NULL, &pos);
    angle = pos.z == 0.0f ? 0.0f : -atanf((x - pos.x) / pos.z);
    if (ac_param->look_limit < angle) { angle = ac_param->look_limit; }
    if (-ac_param->look_limit > angle) { angle = -ac_param->look_limit; }
    difference = head->rotate.x - angle;
    if (difference < 0.0f) { difference = -difference; }
    if (!(difference < 0.05)) {
        float delta = angle - head->rotate.x;
        delta = (float) ((double) delta * 0.1);
        angle = delta + head->rotate.x;
    }
    head->rotate.x = angle;
    HSD_JObjSetMtxDirtySub(head);
}

static void ac_following(HSD_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    HSD_GObj* fighter_gobj = Player_GetEntity(v->player);
    Fighter* fp;
    StaticPlayer* p;
    bool ko_changed;
    u32 stamina;
    int i;
    if (fighter_gobj == NULL) { v->player = 255; return; }
    fp = HSD_GObjGetUserData(fighter_gobj);
    if (v->mood == 2 && v->source <= 5) {
        Fighter* source = HSD_GObjGetUserData(Player_GetEntity(v->source));
        ac_look_at(gobj, source->cur_pos.x);
    } else { ac_look_at(gobj, fp->cur_pos.x); }
    p = Player_GetPtrForSlot(v->player);
    ko_changed = v->kos[0] != (u32) p->falls[1];
    for (i = 0; i < 5; i++) { ko_changed |= v->kos[i + 1] != p->kos_by_player[i]; }
    if (ko_changed) {
        ac_set_mood(gobj, 1, 200);
        ac_set_action(gobj, 3, 200);
    }
    stamina = ((u32) (u16) p->staminas.byName.stamina << 16) | (u16) p->staminas.byName.unk66;
    if (stamina != v->stamina_word || p->falls[0] != v->falls) {
        float draw = HSD_Randf();
        if ((double) draw < 0.3 && fp->dmg.x18c4_source_ply != fp->player_idx &&
            fp->dmg.x18c4_source_ply != -1) {
            v->source = fp->dmg.x18c4_source_ply;
            ac_set_mood(gobj, 2, 200);
        } else if ((double) draw >= 0.6) { ac_set_mood(gobj, 4, 200); }
        else { ac_set_mood(gobj, 3, 200); }
        ac_action_from_mood(gobj);
    }
}

/* +2E54, duplicate +4E60. Blink sequence uses the same mood-dependent eye frames as SetMood. */
static void ac_mood_think(HSD_GObj* gobj)
{
    static const u8 panic[] = { 1, 65, 0, 15, 4, 10, 3, 5, 2, 5, 0, 255 };
    static const u8 normal[] = { 1, 70, 0, 20, 3, 5, 2, 5, 0, 255 };
    static const u8 eyes[] = { 0, 4, 3, 5, 6 };
    AcVillager* v = AC_GET(AcVillager, gobj);
    if (v->data->flags & 4) { return; }
    if (v->mood_timer > 0) { v->mood_timer--; }
    else if (v->following) {
        HSD_GObj* fighter_gobj;
        Fighter* fp;
        int mood;
        v->source = 255;
        fighter_gobj = Player_GetEntity(v->player);
        fp = fighter_gobj != NULL ? HSD_GObjGetUserData(fighter_gobj) : NULL;
        mood = ac_chance(fp != NULL && fp->dmg.x1830_percent > 100.0f ? panic : normal);
        ac_set_mood(gobj, mood, ac_random_range(ac_param->mood_min, ac_param->mood_max));
        ac_action_from_mood(gobj);
    }
    if (v->blink_timer > 0) { v->blink_timer--; return; }
    if (v->blink == 0) {
        if (v->mood == 0 || v->mood == 2 || v->mood == 3) { v->blink = 1; }
        else { v->blink_timer = ac_random_range(ac_param->blink_min, ac_param->blink_max); }
        return;
    }
    switch (v->blink) {
    case 1:
        if (v->mood == 3) { v->blink = 3; ac_eye(gobj, 2); }
        else { ac_eye(gobj, 1); }
        break;
    case 2: ac_eye(gobj, 2); break;
    case 3: ac_eye(gobj, 1); break;
    case 4:
        if ((unsigned) v->mood <= 4) { ac_eye(gobj, eyes[v->mood]); }
        break;
    default: break;
    }
    if (v->blink > 3) {
        v->blink = 0;
        v->blink_timer = ac_random_range(ac_param->blink_min, ac_param->blink_max);
    } else { v->blink++; v->blink_timer = ac_param->blink_step; }
}

static void ac_action_think(HSD_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    if (v->action_timer > 0) { v->action_timer--; return; }
    if (v->following) { ac_action_from_mood(gobj); }
    else if (v->action != 0) { ac_set_action(gobj, 0, 0); }
}

static void ac_pick_target(HSD_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    int players[6], count = 0, i;
    for (i = 0; i < 6; i++) {
        if (Player_GetPlayerSlotType(i) != 3) { players[count++] = i; }
    }
    v->player = count != 0 ? players[HSD_Randi(count)] : 255;
    ac_statistics(gobj);
}

void ac_villager_create(Ground_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    v->following = 1;
    v->target_timer = ac_random_range(ac_param->target_min, ac_param->target_max);
    v->action = -1;
    v->mood = 0;
}

void ac_villager_frame(Ground_GObj* gobj)
{
    AcVillager* v = AC_GET(AcVillager, gobj);
    if (v->action == 4) { return; }
    if (v->following) { ac_following(gobj); }
    ac_mood_think(gobj);
    ac_action_think(gobj);
    ac_statistics(gobj);
    if (!v->following) { return; }
    if (v->target_timer > 0) {
        v->target_timer--;
        if (!ac_player_active(v->player)) { v->target_timer = 0; }
    } else {
        v->target_timer = ac_random_range(ac_param->target_min, ac_param->target_max);
        ac_pick_target(gobj);
    }
}

typedef struct AcList { int count, index[6]; } AcList;

static int ac_list_add(AcList* list, int index)
{
    int slot = list->count;
    if (slot > 5) { return -1; }
    list->index[list->count++] = index;
    return slot;
}

static bool ac_list_can_add(AcList* list, int index)
{
    AcResident* data = DP(ac_database[index]);
    int i;
    for (i = 0; i < list->count; i++) { if (list->index[i] == index) { return false; } }
    if (data->flags & 0x60) { return false; }
    return data->companion_chance <= 99 || list->count < 5;
}

static int ac_list_random(AcList* list, bool human)
{
    int i, total = 0, draw, cumulative = 0;
    AcResident* data;
    if (list->count > 5 || DISC_NULL(ac_database[0])) { return -1; }
    for (i = 0; !DISC_NULL(ac_database[i]); i++) {
        data = DP(ac_database[i]);
        if (!!(data->flags & 1) != human || !ac_list_can_add(list, i)) { continue; }
        total += data->chance > 100 ? 100 : data->chance;
    }
    if (total == 0) { return -1; }
    draw = HSD_Randi(total);
    for (i = 0; !DISC_NULL(ac_database[i]); i++) {
        data = DP(ac_database[i]);
        if (!!(data->flags & 1) != human || !ac_list_can_add(list, i)) { continue; }
        cumulative += data->chance > 100 ? 100 : data->chance;
        if (draw < cumulative) {
            if (!human && (double) HSD_Randf() < (double) data->companion_chance / 100.0 &&
                ac_list_can_add(list, data->companion)) {
                ac_list_add(list, data->companion);
            }
            return ac_list_add(list, i);
        }
    }
    return 0;
}

static void ac_list_fill(AcList* list)
{
    int i;
    unsigned date = (unsigned) ac_date.month * 256 + ac_date.day;
    if (HSD_Randf() > 0.5f) { ac_list_random(list, true); }
    ac_list_random(list, true);
    for (i = 0; !DISC_NULL(ac_database[i]); i++) {
        unsigned flags = DP(ac_database[i])->flags;
        if (((flags & 0x40) && date == 0xA1F) || ((flags & 0x20) && date == 0xC18) ||
            ((flags & 4) && date == 0x401)) { ac_list_add(list, i); }
    }
    while (ac_list_random(list, false) != -1) {}
    /* +3FD0: draw is exclusive of i. The last element always swaps with an earlier element. */
    for (i = list->count - 1; i > 0; i--) {
        int chosen = HSD_Randi(i), saved = list->index[chosen];
        list->index[chosen] = list->index[i];
        list->index[i] = saved;
    }
}

MU_NO_CONTRACT static HSD_GObj* ac_spawn(AcResident* data, HSD_JObj* spawn)
{
    HSD_GObj* gobj = mu_grTSeak_80223908(12);
    AcVillager* v = AC_GET(AcVillager, gobj);
    HSD_Joint* joint;
    HSD_MatAnimJoint* material;
    HSD_JObj* root;
    int duration;
    if (!DISC_NULL(data->file)) {
        HSD_Archive* archive = lbArchive_LoadArchive(DP(data->file));
        joint = HSD_ArchiveGetPublicAddress(archive, "mura_joint");
        material = HSD_ArchiveGetPublicAddress(archive, "mura_matanim_joint");
    } else { joint = DP(data->joint); material = DP(data->material); }
    v->data = data;
    lb_8000B1CC(spawn, NULL, &v->pos);
    root = GET_JOBJ(gobj);
    HSD_JObjRemove(root->child);
    HSD_JObjAddChild(root, HSD_JObjLoadJoint(joint));
    Ground_801C3FA4(gobj, 0);
    if (material != NULL) { HSD_JObjAddAnimAll(Ground_801C3FA4(gobj, 0), NULL, material, NULL); }
    if (data->flags & 4) { ac_eye(gobj, HSD_Randi(7)); }
    if (ac_time == 0 && (data->flags & 2)) { ac_set_action(gobj, 4, 0); return gobj; }
    duration = (int) (HSD_Randf() * 100.0f + 300.0f);
    ac_set_action(gobj, 0, duration);
    ac_set_mood(gobj, 1, duration);
    ac_pick_target(gobj);
    return gobj;
}

/* +C18..E64, population portion. Each spawn consumes a front-placement draw even when no live
 * event is active. The alternate position is the floor's joint 4 origin. */
MU_NO_CONTRACT void ac_population_create(Ground_GObj* gobj)
{
    static const int spawn_bones[6] = { 2, 5, 1, 3, 6, 8 };
    AcList list = { 0 };
    AcFloor* floor = AC_GET(AcFloor, gobj);
    int i;
    ac_list_fill(&list);
    for (i = 0; i < list.count; i++) {
        AcResident* data;
        HSD_GObj* resident;
        float draw;
        floor->resident[i] = NULL;
        if (list.index[i] == -1) { continue; }
        data = DP(ac_database[list.index[i]]);
        resident = ac_spawn(data, Ground_801C3FA4(gobj, spawn_bones[i]));
        floor->resident[i] = resident;
        draw = HSD_Randf();
        if ((ac_event == 2 && ac_param->front_live >= draw) ||
            (ac_event == 1 && ac_param->front_rest >= draw)) {
            AcVillager* v = AC_GET(AcVillager, resident);
            Vec3 target;
            float angle;
            v->following = 0;
            lb_8000B1CC(Ground_801C3FA4(gobj, 4), NULL, &target);
            angle = atanf((v->pos.x - target.x) / (v->pos.z - target.z));
            v->rotation = (float) ((double) angle - 3.141592653589793);
            Ground_801C3FA4(resident, 0)->rotate.y = v->rotation;
        }
    }
}
