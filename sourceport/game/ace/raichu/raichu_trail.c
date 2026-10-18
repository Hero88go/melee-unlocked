/* PlRc.dat's reachable trail path: +0000, +06D0..+0C00, +1B44..+1D24,
 * +25FC..+2B1C, +2CD4..+2E98, +3478..+3DFC.
 * Parameters are copied field for field from +06E4 (nine 0x60-byte rows).
 * The item-owner branch and follow modes 0..3 have no caller in PlRc.dat. */
#include "raichu_trail.h"
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

#define RC_TRAIL_ROW(id, history, start, end, fade, bone, sx, sy, sz, ax, ay, az, px, py, pz) \
    { id, 4, 0, 2, history, start, end, fade, bone, { sx, sy, sz }, { 1, 1, 1 }, \
      { ax, ay, az }, { px, py, pz }, 0xFFFFFFFF, 0xFFFF0000, 0 }
static const ftRc_TrailSpec ftRc_TrailSpecs[] = {
    RC_TRAIL_ROW(68, 4, 7, 18, 15, 50, 0, 6, 0, 0, 1, 0, 0, 0, 1),
    RC_TRAIL_ROW(57, 4, 8, 15, 16, 50, 0, 9, 0, 0, 1, 0, 0, 0, -1),
    RC_TRAIL_ROW(53, 4, 8, 15, 16, 50, 0, 9, 0, 0, 1, 0, 0, 0, -1),
    RC_TRAIL_ROW(51, 4, 8, 15, 16, 50, 0, 9, 0, 0, 1, 0, 0, 0, -1),
    RC_TRAIL_ROW(55, 4, 8, 15, 16, 50, 0, 9, 0, 0, 1, 0, 0, 0, -1),
    RC_TRAIL_ROW(222, 4, 33, 37, 38, 50, 0, 9, 0, 0, 1, 0, 0, 0, -1),
    RC_TRAIL_ROW(66, 3, 10, 27, 23, 50, 0, 6, 0, 0, 1, 0, 0, -1, 0),
    RC_TRAIL_ROW(69, 3, 17, 21, 25, 50, 0, 6, 0, 1, 0, 0, 0, 1, 1),
    RC_TRAIL_ROW(187, 3, 17, 21, 25, 50, 0, 6, 0, 1, 0, 0, 0, 1, 1),
};
#undef RC_TRAIL_ROW

static void* ftRc_Trail_Alloc(size_t size)
{
    void* result = HSD_MemAlloc(size);
    __builtin_memset(result, 0, size);
    return result;
}

static void ftRc_Trail_FreeMatrices(ftRc_TrailMatrix* node)
{
    /* +2DAC..+2DD0 leaves the final history node allocated. */
    while (node->next != NULL) {
        ftRc_TrailMatrix* next = node->next;
        HSD_Free(node);
        node = next;
    }
}

static void ftRc_Trail_SetSRT(HSD_JObj* joint, MtxPtr m)
{
    Vec3 rot;
    HSD_MtxGetRotation(m, &rot);
    joint->rotate.x = rot.x;
    joint->rotate.y = rot.y;
    joint->rotate.z = rot.z;
    HSD_MtxGetTranslate(m, &joint->translate);
    HSD_MtxGetScale(m, &joint->scale);
}

static void ftRc_Trail_JointSRT(HSD_JObj* joint, MtxPtr m)
{
    Vec3 rot = { joint->rotate.x, joint->rotate.y, joint->rotate.z };
    HSD_MtxSRT(m, &joint->scale, &rot, &joint->translate, NULL);
}

static void ftRc_Trail_ShiftHistory(ftRc_TrailMatrix* head)
{
    ftRc_TrailMatrix* tail = head;
    while (tail->next != NULL) tail = tail->next;
    while (tail->prev != NULL) {
        PSMTXCopy(tail->prev->current, tail->current);
        tail = tail->prev;
    }
}

/* +39DC. The final non-root ancestor's local matrix is retained for joints
 * using quaternions, as in +3AC0..+3AF8. */
static bool ftRc_Trail_Follow(HSD_GObj* gobj)
{
    ftRc_TrailData* data = gobj->user_data;
    const ftRc_TrailSpec* spec = data->spec;
    HSD_JObj* model = gobj->hsd_obj;
    HSD_JObj* parent = data->anchor->parent;
    Vec3 one = { 1, 1, 1 }, zero = { 0 };
    Vec3 rot = { data->anchor->rotate.x, data->anchor->rotate.y, data->anchor->rotate.z };
    Vec3 pos, last, displacement, axis, plane, x;
    Mtx offset, local, ancestor, root, world, oriented;
    HSD_MtxSRT(offset, &one, &zero, (Vec3*) &spec->offset, NULL);
    HSD_MtxSRT(local, &data->anchor->scale, &rot, &data->anchor->translate, NULL);
    PSMTXConcat(local, offset, local);
    while (parent->parent != NULL) {
        if (!(parent->flags & JOBJ_USE_QUATERNION)) {
            ftRc_Trail_JointSRT(parent, ancestor);
        }
        if (ancestor[0][0] == ancestor[0][0]) {
            PSMTXConcat(ancestor, local, local);
        }
        parent = parent->parent;
    }
    ftRc_Trail_JointSRT(data->fighter_root, root);
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
    ftRc_Trail_SetSRT(model, oriented);
    HSD_JObjSetMtxDirtySub(model);
    HSD_MtxGetTranslate(data->history->current, &last);
    HSD_MtxGetTranslate(oriented, &pos);
    PSVECSubtract(&pos, &last, &displacement);
    float distance = sqrtf(__builtin_fmaf(displacement.z, displacement.z,
                            __builtin_fmaf(displacement.x, displacement.x,
                                           displacement.y * displacement.y)));
    if (distance > 1.0f) ftRc_Trail_ShiftHistory(data->history);
    PSMTXCopy(oriented, data->history->current);
    return false;
}

/* +3478. The first child is a neutral bridge joint. The remaining model
 * joints sample the saved path at equal distances, with the disc's fade. */
static bool ftRc_Trail_Curve(HSD_GObj* gobj)
{
    ftRc_TrailData* data = gobj->user_data;
    const ftRc_TrailSpec* spec = data->spec;
    ftRc_TrailMatrix* history = data->history;
    ftRc_TrailMatrix* model_matrix = data->model;
    HSD_JObj* joint = gobj->hsd_obj;
    Vec3 one = { 1, 1, 1 }, zero = { 0 }, dummy;
    Mtx offset, position_offset, sample, inverse, relative;
    float total = 0;
    ftRc_TrailSegment* segments = ftRc_Trail_Alloc(sizeof(*segments) * spec->history_count);
    HSD_MtxSRT(position_offset, (Vec3*) &spec->scale, &zero, &zero, NULL);
    HSD_MtxSRT(offset, &one, &zero, (Vec3*) &spec->axis, NULL);
    for (int i = 0; i < spec->history_count; i++) {
        ftRc_TrailMatrix* next = history->next;
        ftRc_Trail_Controls(i ? history->prev->current : NULL,
                            history->current, next->current,
                            i + 1 < spec->history_count ? next->next->current : NULL,
                            offset, &segments[i]);
        total += segments[i].length;
        history = next;
    }
    history = data->history;
    ftRc_Trail_Sample(&segments[0], history->current, history->next->current, 0, sample);
    PSMTXConcat(sample, position_offset, sample);
    ftRc_Trail_SetSRT(joint, sample);
    PSMTXCopy(sample, model_matrix->current);
    lb_8000B1CC(joint, NULL, &dummy);
    model_matrix = model_matrix->next;
    joint = joint->child;
    joint->translate = zero;
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
        ftRc_Trail_Sample(&segments[segment], history->current, history->next->current, t, sample);
        PSMTXConcat(sample, position_offset, sample);
        PSMTXConcat(inverse, sample, relative);
        ftRc_Trail_SetSRT(joint, relative);
        PSMTXCopy(sample, model_matrix->current);
        PSMTXCopy(model_matrix->prev->current, model_matrix->prev->previous);
        lb_8000B1CC(joint, NULL, &dummy);
    }
    HSD_Free(segments);
    return false;
}

static void ftRc_Trail_Destroy(void* raw)
{
    ftRc_TrailData* data = raw;
    if (data->controller != NULL) {
        ftRc_TrailController* controller = data->controller->user_data;
        controller->active = NULL;
    }
    ftRc_Trail_FreeMatrices(data->history);
    /* The console never releases the separate model-matrix list (+20).
     * Keep that allocation behavior until the reference teardown is checked. */
    HSD_Free(data);
}

/* +2CD4. Follow, lifetime, curve run in that order, even when a prior
 * callback has requested removal. Hitlag and pause suppress the whole step. */
static void ftRc_Trail_Proc(HSD_GObj* gobj)
{
    ftRc_TrailData* data = gobj->user_data;
    Fighter* fp = GET_FIGHTER(data->fighter);
    if (fp->allow_sdi || gm_GetDbPauseFlag(1) == 2) return;
    bool a = ftRc_Trail_Follow(gobj);
    data->frame += 1.0f;
    bool b = data->frame >= data->spec->end || fp->motion_id != data->spec->motion;
    bool c = ftRc_Trail_Curve(gobj);
    if (a || b || c) HSD_GObjFree(gobj);
}

static HSD_GObj* ftRc_Trail_Create(HSD_GObj* controller_gobj, const ftRc_TrailSpec* spec)
{
    ftRc_TrailController* controller = controller_gobj->user_data;
    Fighter* fp = GET_FIGHTER(controller->fighter);
    typedef DISC_PTR(HSD_Joint) JointSlot;
    JointSlot* symbol = HSD_ArchiveGetPublicAddress(controller->archive, "trJoint");
    HSD_JObj* joint = HSD_JObjLoadJoint(DP(*symbol));
    HSD_GObj* gobj = GObj_Create(10, 0, 0);
    ftRc_TrailData* data = ftRc_Trail_Alloc(sizeof(*data));
    HSD_GObj_SetupProc(gobj, ftRc_Trail_Proc, 10);
    GObj_InitUserData(gobj, 4, ftRc_Trail_Destroy, data);
    HSD_GObjObject_80390A70(gobj, 3, joint);
    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 5, 0);
    data->fighter = controller->fighter;
    data->controller = controller_gobj;
    data->fighter_root = controller->fighter->hsd_obj;
    data->spec = spec;
    data->frame = spec->start;
    data->anchor = fp->parts[spec->bone].joint;
    data->history = ftRc_Trail_Alloc(sizeof(*data->history));
    ftRc_TrailMatrix* matrix = data->history;
    for (int i = 0; i < spec->history_count; i++) {
        matrix->next = ftRc_Trail_Alloc(sizeof(*matrix));
        matrix->next->prev = matrix;
        matrix = matrix->next;
    }
    ftRc_Trail_Follow(gobj);
    data->model = ftRc_Trail_Alloc(sizeof(*data->model));
    matrix = data->model;
    ftRc_Trail_JointSRT(joint, matrix->current);
    PSMTXCopy(matrix->current, matrix->previous);
    HSD_JObj* child = joint;
    Mtx tip_local;
    while (child->child != NULL) {
        child = child->child;
        matrix->next = ftRc_Trail_Alloc(sizeof(*matrix));
        matrix->next->prev = matrix;
        matrix = matrix->next;
        ftRc_Trail_JointSRT(child, tip_local);
        PSMTXConcat(matrix->prev->current, tip_local, matrix->current);
        PSMTXCopy(matrix->current, matrix->previous);
        data->model_segments++;
        HSD_JObjSetMtxDirtySub(child);
    }
    Mtx initial;
    ftRc_Trail_JointSRT(joint, initial);
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

static void ftRc_Trail_ControllerDestroy(void* raw)
{
    ftRc_TrailController* controller = raw;
    if (controller->active != NULL) {
        ftRc_TrailData* data = controller->active->user_data;
        data->controller = NULL;
        HSD_GObjFree(controller->active);
    }
    HSD_Free(controller->archive);
    HSD_Free(controller);
}

/* +1B44: strict bounds, with the current frame between start and start+speed.
 * At a speed of one, integer frame values never satisfy this condition. */
static void ftRc_Trail_ControllerProc(HSD_GObj* gobj)
{
    ftRc_TrailController* controller = gobj->user_data;
    Fighter* fp = GET_FIGHTER(controller->fighter);
    if (controller->active != NULL) return;
    for (int i = 0; i < 9; i++) {
        const ftRc_TrailSpec* spec = &ftRc_TrailSpecs[i];
        if (fp->motion_id == spec->motion && spec->start < fp->cur_anim_frame &&
            fp->cur_anim_frame < spec->start + fp->frame_speed_mul) {
            controller->active = ftRc_Trail_Create(gobj, spec);
            return;
        }
    }
}

/* +0A44. The mode-0 lookup table is represented directly by the nine rows. */
void ftRc_Trail_OnLoad(HSD_GObj* fighter)
{
    HSD_GObj* gobj = GObj_Create(0, 0, 0);
    ftRc_TrailController* data = ftRc_Trail_Alloc(sizeof(*data));
    HSD_GObj_SetupProc(gobj, ftRc_Trail_ControllerProc, 13);
    GObj_InitUserData(gobj, 4, ftRc_Trail_ControllerDestroy, data);
    data->fighter = fighter;
    data->archive = lbArchive_LoadArchive("largetrailrc.dat");
}
