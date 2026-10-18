/* PlSm.dat reachable trail mode 4, lifetime 0, curve 2. gaps_lib.txt
 * records register and relocation changes from the reviewed Raichu library.
 * Its one-vector lives at +2FD0, the scalar zero remains +1AF8. */
#include "smewtwo_trail.h"
#include <math.h>
#include <melee/ft/inlines.h>
#include <melee/gm/gmscene.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mtx.h>

#define SM_TRAIL_ROW(id, history, start, end, fade, bone, sx, sy, sz, ax, ay, az, px, py, pz) \
    { id, 4, 0, 2, history, start, end, fade, bone, { sx, sy, sz }, { 1, 1, 1 }, \
      { ax, ay, az }, { px, py, pz }, 0xFFFFFFFF, 0xFFFF0000, 0 }
static const ftSm_TrailSpec ftSm_TrailSpecs[] = {
    SM_TRAIL_ROW(66,5,6,20,14,34,5,0,0,1,0,0,1,1,0),
    SM_TRAIL_ROW(53,5,5,18,13,57,10,0,0,1,0,0,-1,-1,-1),
    SM_TRAIL_ROW(51,5,5,18,13,57,10,0,0,1,0,0,-1,-1,-1),
    SM_TRAIL_ROW(55,5,5,18,13,57,10,0,0,1,0,0,-1,-1,-1),
};
static const ftSm_TrailSpec ftSm_TrailSpecs2[] = {
    SM_TRAIL_ROW(56,5,6,15,10,57,10,0,0,1,0,0,-1,-1,-1),
};
static const ftSm_TrailSpec ftSm_TrailSpecs3[] = {
    {68,4,0,2,5,4,15,11,34,{0,0,0},{0.5f,0.7f,1},{1,0,0},{-1,-1,-1},0,0,0},
    {359,4,0,2,5,29,37,34,58,{0,0,0},{0.5f,0.7f,1},{1,0,0},{-1,-1,-1},0,0,0},
    {360,4,0,2,5,29,37,34,58,{0,0,0},{0.5f,0.7f,1},{1,0,0},{-1,-1,-1},0,0,0},
};
#undef SM_TRAIL_ROW

static void* ftSm_Trail_Alloc(size_t size)
{
    void* result = HSD_MemAlloc(size);
    __builtin_memset(result, 0, size);
    return result;
}

static void ftSm_Trail_FreeMatrices(ftSm_TrailMatrix* node)
{
    /* +2DAC..+2DD0 leaves the final history node allocated. */
    while (node->next != NULL) {
        ftSm_TrailMatrix* next = node->next;
        HSD_Free(node);
        node = next;
    }
}

static void ftSm_Trail_SetSRT(HSD_JObj* joint, MtxPtr m)
{
    Vec3 rot;
    HSD_MtxGetRotation(m, &rot);
    joint->rotate.x = rot.x;
    joint->rotate.y = rot.y;
    joint->rotate.z = rot.z;
    HSD_MtxGetTranslate(m, &joint->translate);
    HSD_MtxGetScale(m, &joint->scale);
}

static void ftSm_Trail_JointSRT(HSD_JObj* joint, MtxPtr m)
{
    Vec3 rot = { joint->rotate.x, joint->rotate.y, joint->rotate.z };
    HSD_MtxSRT(m, &joint->scale, &rot, &joint->translate, NULL);
}

static void ftSm_Trail_ShiftHistory(ftSm_TrailMatrix* head)
{
    ftSm_TrailMatrix* tail = head;
    while (tail->next != NULL) tail = tail->next;
    while (tail->prev != NULL) {
        PSMTXCopy(tail->prev->current, tail->current);
        tail = tail->prev;
    }
}

/* +39DC. The final non-root ancestor's local matrix is retained for joints
 * using quaternions, as in +3AC0..+3AF8. */
static bool ftSm_Trail_Follow(HSD_GObj* gobj)
{
    ftSm_TrailData* data = gobj->user_data;
    const ftSm_TrailSpec* spec = data->spec;
    HSD_JObj* model = gobj->hsd_obj;
    HSD_JObj* parent = data->anchor->parent;
    Vec3 one = { 1, 1, 1 }, neutral = { 0 };
    Vec3 rot = { data->anchor->rotate.x, data->anchor->rotate.y, data->anchor->rotate.z };
    Vec3 pos, last, displacement, axis, plane, x;
    Mtx offset, local, ancestor, root, world, oriented;
    HSD_MtxSRT(offset, &one, &neutral, (Vec3*) &spec->offset, NULL);
    HSD_MtxSRT(local, &data->anchor->scale, &rot, &data->anchor->translate, NULL);
    PSMTXConcat(local, offset, local);
    while (parent->parent != NULL) {
        if (!(parent->flags & JOBJ_USE_QUATERNION)) {
            ftSm_Trail_JointSRT(parent, ancestor);
        }
        if (ancestor[0][0] == ancestor[0][0]) {
            PSMTXConcat(ancestor, local, local);
        }
        parent = parent->parent;
    }
    ftSm_Trail_JointSRT(data->fighter_root, root);
    PSMTXConcat(root, local, world);
    HSD_MtxGetTranslate(world, &pos);
    last.x = data->history->current[0][0];
    last.y = data->history->current[0][1];
    last.z = data->history->current[0][2];
    PSVECSubtract(&last, &pos, &displacement);
    axis.x = __builtin_fmaf(world[0][2], spec->axis.z,
               __builtin_fmaf(world[0][0], spec->axis.x, world[0][1] * spec->axis.y));
    axis.y = __builtin_fmaf(world[1][2], spec->axis.z,
               __builtin_fmaf(spec->axis.x, world[1][0], world[1][1] * spec->axis.y));
    axis.z = __builtin_fmaf(world[2][2], spec->axis.z,
               __builtin_fmaf(spec->axis.x, world[2][0], world[2][1] * spec->axis.y));
    plane.x = -__builtin_fmaf(world[0][2], spec->plane.z,
                  __builtin_fmaf(world[0][0], spec->plane.x, world[0][1] * spec->plane.y));
    plane.y = -__builtin_fmaf(world[1][2], spec->plane.z,
                  __builtin_fmaf(world[1][0], spec->plane.x, world[1][1] * spec->plane.y));
    plane.z = -__builtin_fmaf(world[2][2], spec->plane.z,
                  __builtin_fmaf(world[2][0], spec->plane.x, world[2][1] * spec->plane.y));
    PSVECCrossProduct(&axis, &plane, &x);
    PSVECCrossProduct(&x, &axis, &plane);
    PSVECNormalize(&x, &x);
    PSVECNormalize(&axis, &axis);
    PSVECNormalize(&plane, &plane);
    oriented[0][0] = x.x; oriented[1][0] = x.y; oriented[2][0] = x.z;
    oriented[0][1] = axis.x; oriented[1][1] = axis.y; oriented[2][1] = axis.z;
    oriented[0][2] = plane.x; oriented[1][2] = plane.y; oriented[2][2] = plane.z;
    oriented[0][3] = world[0][3]; oriented[1][3] = world[1][3]; oriented[2][3] = world[2][3];
    ftSm_Trail_SetSRT(model, oriented);
    HSD_JObjSetMtxDirtySub(model);
    HSD_MtxGetTranslate(data->history->current, &last);
    HSD_MtxGetTranslate(oriented, &pos);
    PSVECSubtract(&pos, &last, &displacement);
    float distance = sqrtf(__builtin_fmaf(displacement.z, displacement.z,
                            __builtin_fmaf(displacement.x, displacement.x,
                                           displacement.y * displacement.y)));
    if (distance > 1.0f) ftSm_Trail_ShiftHistory(data->history);
    PSMTXCopy(oriented, data->history->current);
    return false;
}

/* +3478. The first child is a neutral bridge joint. The remaining model
 * joints sample the saved path at equal distances, with the disc's fade. */
static bool ftSm_Trail_Curve(HSD_GObj* gobj)
{
    ftSm_TrailData* data = gobj->user_data;
    const ftSm_TrailSpec* spec = data->spec;
    ftSm_TrailMatrix* history = data->history;
    ftSm_TrailMatrix* model_matrix = data->model;
    HSD_JObj* joint = gobj->hsd_obj;
    Vec3 one = { 1, 1, 1 }, neutral = { 0 }, dummy;
    Mtx offset, position_offset, sample, inverse, relative;
    float total = 0;
    ftSm_TrailSegment* segments = ftSm_Trail_Alloc(sizeof(*segments) * spec->history_count);
    HSD_MtxSRT(position_offset, (Vec3*) &spec->scale, &neutral, &neutral, NULL);
    HSD_MtxSRT(offset, &one, &neutral, (Vec3*) &spec->axis, NULL);
    for (int i = 0; i < spec->history_count; i++) {
        ftSm_TrailMatrix* next = history->next;
        ftSm_Trail_Controls(i ? history->prev->current : NULL,
                            history->current, next->current,
                            i + 1 < spec->history_count ? next->next->current : NULL,
                            offset, &segments[i]);
        total += segments[i].length;
        history = next;
    }
    history = data->history;
    ftSm_Trail_Sample(&segments[0], history->current, history->next->current, 0, sample);
    PSMTXConcat(sample, position_offset, sample);
    ftSm_Trail_SetSRT(joint, sample);
    PSMTXCopy(sample, model_matrix->current);
    lb_8000B1CC(joint, NULL, &dummy);
    model_matrix = model_matrix->next;
    joint = joint->child;
    joint->translate = neutral;
    joint->scale = one;
    joint->rotate = (Quaternion) { 0 };
    PSMTXCopy(model_matrix->prev->current, model_matrix->current);
    lb_8000B1CC(joint, NULL, &dummy);
    joint = joint->child;
    for (int i = 1; joint != NULL; i++, joint = joint->child) {
        float distance = total;
        float prefix = 0;
        int segment = 0;
        model_matrix = model_matrix->next;
        PSMTXInverse(model_matrix->prev->current, inverse);
        if (data->frame > spec->fade_start) {
            float fade = (spec->end - data->frame) / (spec->end - spec->fade_start);
            if (fade <= 0) fade = 0.01f;
            distance = total * fade;
        }
        distance = ((float) i / (float) data->model_segments) * distance;
        history = data->history;
        /* Keep the last segment even when distance exceeds the saved length. */
        while (segment + 1 < spec->history_count) {
            float edge = prefix + segments[segment].length;
            /* +37F4..+39AC keeps an odd table's first boundary in segment
             * zero, but an equal later boundary advances to the next one. */
            if ((segment == 0 && (spec->history_count & 1)) ?
                distance <= edge : distance < edge) break;
            prefix += segments[segment].length;
            segment++;
            history = history->next;
        }
        float t = (distance - prefix) / segments[segment].length;
        ftSm_Trail_Sample(&segments[segment], history->current, history->next->current, t, sample);
        PSMTXConcat(sample, position_offset, sample);
        PSMTXConcat(inverse, sample, relative);
        ftSm_Trail_SetSRT(joint, relative);
        PSMTXCopy(sample, model_matrix->current);
        PSMTXCopy(model_matrix->prev->current, model_matrix->prev->previous);
        lb_8000B1CC(joint, NULL, &dummy);
    }
    HSD_Free(segments);
    return false;
}

static void ftSm_Trail_Destroy(void* raw)
{
    ftSm_TrailData* data = raw;
    if (data->controller != NULL) {
        ftSm_TrailController* controller = data->controller->user_data;
        controller->active = NULL;
    }
    ftSm_Trail_FreeMatrices(data->history);
    /* The console never releases the separate model-matrix list (+20).
     * Keep that allocation behavior until the reference teardown is checked. */
    HSD_Free(data);
}

/* +2CD4. Follow, lifetime, curve run in that order, even when a prior
 * callback has requested removal. Hitlag and pause suppress the whole step. */
static void ftSm_Trail_Proc(HSD_GObj* gobj)
{
    ftSm_TrailData* data = gobj->user_data;
    Fighter* fp = GET_FIGHTER(data->fighter);
    if (fp->allow_sdi || gm_GetDbPauseFlag(1) == 2) return;
    bool a = ftSm_Trail_Follow(gobj);
    data->frame += 1.0f;
    bool b = data->frame >= data->spec->end || fp->motion_id != data->spec->motion;
    bool c = ftSm_Trail_Curve(gobj);
    if (a || b || c) HSD_GObjFree(gobj);
}

static HSD_GObj* ftSm_Trail_Create(HSD_GObj* controller_gobj, const ftSm_TrailSpec* spec)
{
    ftSm_TrailController* controller = controller_gobj->user_data;
    Fighter* fp = GET_FIGHTER(controller->fighter);
    typedef DISC_PTR(HSD_Joint) JointSlot;
    JointSlot* symbol = HSD_ArchiveGetPublicAddress(controller->archive, "trJoint");
    HSD_JObj* joint = HSD_JObjLoadJoint(DP(*symbol));
    HSD_GObj* gobj = GObj_Create(10, 0, 0);
    ftSm_TrailData* data = ftSm_Trail_Alloc(sizeof(*data));
    HSD_GObj_SetupProc(gobj, ftSm_Trail_Proc, 10);
    GObj_InitUserData(gobj, 4, ftSm_Trail_Destroy, data);
    HSD_GObjObject_80390A70(gobj, 3, joint);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 5, 0);
    data->fighter = controller->fighter;
    data->controller = controller_gobj;
    data->fighter_root = controller->fighter->hsd_obj;
    data->spec = spec;
    data->frame = spec->start;
    data->anchor = fp->parts[spec->bone].joint;
    data->history = ftSm_Trail_Alloc(sizeof(*data->history));
    ftSm_TrailMatrix* matrix = data->history;
    for (int i = 0; i < spec->history_count; i++) {
        matrix->next = ftSm_Trail_Alloc(sizeof(*matrix));
        matrix->next->prev = matrix;
        matrix = matrix->next;
    }
    ftSm_Trail_Follow(gobj);
    data->model = ftSm_Trail_Alloc(sizeof(*data->model));
    matrix = data->model;
    ftSm_Trail_JointSRT(joint, matrix->current);
    PSMTXCopy(matrix->current, matrix->previous);
    HSD_JObj* child = joint;
    Mtx tip_local;
    while (child->child != NULL) {
        child = child->child;
        matrix->next = ftSm_Trail_Alloc(sizeof(*matrix));
        matrix->next->prev = matrix;
        matrix = matrix->next;
        ftSm_Trail_JointSRT(child, tip_local);
        PSMTXConcat(matrix->prev->current, tip_local, matrix->current);
        PSMTXCopy(matrix->current, matrix->previous);
        data->model_segments++;
        HSD_JObjSetMtxDirtySub(child);
    }
    Mtx initial;
    ftSm_Trail_JointSRT(joint, initial);
    PSMTXCopy(initial, data->history->current);
    Vec3 root_pos, tip_pos, dummy;
    HSD_MtxGetTranslate(initial, &root_pos);
    lb_8000B1CC(child, NULL, &tip_pos);
    matrix = data->history;
    for (int i = 1; i <= spec->history_count; i++) {
        Vec3 delta;
        float t = (float) i / (float) spec->history_count;
        matrix = matrix->next;
        PSVECSubtract(&tip_pos, &root_pos, &delta);
        delta.x *= t; delta.y *= t; delta.z *= t;
        PSVECAdd(&delta, &root_pos, &dummy);
        /* +2A08 copies the final child's LOCAL matrix, not the root SRT. */
        PSMTXCopy(tip_local, matrix->current);
        matrix->current[0][3] = dummy.x;
        matrix->current[1][3] = dummy.y;
        matrix->current[2][3] = dummy.z;
    }
    return gobj;
}

static void ftSm_Trail_ControllerDestroy(void* raw)
{
    ftSm_TrailController* controller = raw;
    if (controller->active != NULL) {
        ftSm_TrailData* data = controller->active->user_data;
        data->controller = NULL;
        HSD_GObjFree(controller->active);
    }
    HSD_Free(controller->archive);
    HSD_Free(controller);
}

/* +1B44: strict bounds, with the current frame between start and start+speed.
 * At a speed of one, integer frame values never satisfy this condition. */
static void ftSm_Trail_ControllerProc(HSD_GObj* gobj)
{
    ftSm_TrailController* controller = gobj->user_data;
    Fighter* fp = GET_FIGHTER(controller->fighter);
    if (controller->active != NULL) return;
    for (int i = 0; i < controller->spec_count; i++) {
        const ftSm_TrailSpec* spec = &controller->specs[i];
        if (fp->motion_id == spec->motion && spec->start < fp->cur_anim_frame &&
            fp->cur_anim_frame < spec->start + fp->frame_speed_mul) {
            controller->active = ftSm_Trail_Create(gobj, spec);
            return;
        }
    }
}

/* +2914. Each mode-0 lookup table is represented directly by its rows. */
static void ftSm_Trail_Load(HSD_GObj* fighter,const ftSm_TrailSpec* specs,
                            int count,const char* file)
{
    HSD_GObj* gobj = GObj_Create(0, 0, 0);
    ftSm_TrailController* data = ftSm_Trail_Alloc(sizeof(*data));
    HSD_GObj_SetupProc(gobj, ftSm_Trail_ControllerProc, 13);
    GObj_InitUserData(gobj, 4, ftSm_Trail_ControllerDestroy, data);
    data->fighter = fighter;
    data->specs = specs;
    data->spec_count = count;
    data->archive = lbArchive_LoadArchive(file);
}
void ftSm_Trails_OnLoad(HSD_GObj* fighter)
{
    ftSm_Trail_Load(fighter,ftSm_TrailSpecs,4,"PlSmTr1.dat");
    ftSm_Trail_Load(fighter,ftSm_TrailSpecs2,1,"PlSmTr3.dat");
    ftSm_Trail_Load(fighter,ftSm_TrailSpecs3,3,"PlSmTr5.dat");
}

