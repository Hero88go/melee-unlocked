#include "ui_scene.h"
#include "../../director/rogue_runtime.h"
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/memory.h>
#include <dolphin/os.h>
#include <melee/lb/lbarchive.h>
#include <string.h>

static HSD_WObjDesc eye = { NULL, { 0, 0, 100 }, NULL };
static HSD_WObjDesc target = { NULL, { 0, 0, 0 }, NULL };
static HSD_CameraDescFrustum camera_desc = {
    NULL, 0, PROJ_ORTHO, { 0,640,0,480 }, { 0,640,0,480 },
    DISC_STATIC(&eye), DISC_STATIC(&target), 0, { 0 }, 1, 1000, 0,-480,0,640
};
/* Native build: a disc slot cannot hold an address at compile time (mu_disc.h DISC_STATIC). */
__attribute__((constructor)) static void rogue_ui_disc_fixups(void)
{
    DISC_SET(camera_desc.eyepos, &eye);
    DISC_SET(camera_desc.interest, &target);
}

static void visible(HSD_JObj* joint, int show)
{
    if (show) HSD_JObjClearFlags(joint, JOBJ_HIDDEN);
    else HSD_JObjSetFlags(joint, JOBJ_HIDDEN);
}

/* CSS portrait mesh and texture animation are loaded from the user's retail DAT.
 * Only the portrait DOBJ is instantiated; no stock scene globals are borrowed. */
/* Disc data (MnSelectChrDataTable): 32-bit slots natively, read through DP(). */
typedef struct RogueNativeModel {
    DISC_PTR(HSD_Joint) joint;
    DISC_PTR(HSD_AnimJoint) animation;
    DISC_PTR(HSD_MatAnimJoint) material;
    DISC_PTR(HSD_ShapeAnimJoint) shape;
} DISC_STRUCT RogueNativeModel;
typedef struct RogueCssAssets {
    DISC_PTR(void) camera;
    DISC_PTR(void) light0;
    DISC_PTR(void) light1;
    DISC_PTR(void) fog;
    RogueNativeModel models[9];
} DISC_STRUCT RogueCssAssets;
static int portrait_branch(HSD_Joint* joint, HSD_MatAnimJoint* mat, unsigned* index,
                           HSD_Joint** found, HSD_MatAnimJoint** animation)
{
    if (!joint) return 0;
    if ((*index)++ == 45) { *found = joint; *animation = mat; return 1; }
    if (portrait_branch(DP(joint->child), mat ? DP(mat->child) : NULL, index, found, animation)) return 1;
    return portrait_branch(DP(joint->next), mat ? DP(mat->next) : NULL, index, found, animation);
}
static void create_portraits(RogueUiContext* ui)
{
    RogueCssAssets* assets;
    HSD_Joint* branch = NULL;
    HSD_MatAnimJoint* animation = NULL;
    /* static, not on the stack: the copies are linked through 32-bit disc slots natively */
    static HSD_Joint descriptor;
    static HSD_DObjDesc mesh;
    static HSD_MatAnimJoint material;
    static HSD_MatAnim mat;
    unsigned i, index = 0;
    ui->portrait_archive = lbArchive_LoadArchive("MnSlChr.usd");
    RogueRuntime_ResourceAcquire(ui->generation);
    assets = HSD_ArchiveGetPublicAddress(ui->portrait_archive, "MnSelectChrDataTable");
    portrait_branch(DP(assets->models[6].joint), DP(assets->models[6].material), &index, &branch, &animation);
    if (!branch || !animation) OSPanic(__FILE__, __LINE__, "CSS portrait asset contract changed");
    descriptor = *branch;
    mesh = *DP(branch->u.dobjdesc);
    DISC_SET(mesh.next, NULL);
    DISC_SET(descriptor.child, NULL);
    DISC_SET(descriptor.next, NULL);
    DISC_SET(descriptor.u.dobjdesc, &mesh);
    descriptor.position.x = descriptor.position.y = descriptor.position.z = 0;
    material = *animation;
    DISC_SET(material.child, NULL);
    DISC_SET(material.next, NULL);
    mat = *DP(animation->matanim);
    DISC_SET(mat.next, NULL);
    DISC_SET(material.matanim, &mat);
    for (i = 0; i < 4; ++i) {
        HSD_JObj* joint = HSD_JObjLoadJoint(&descriptor);
        ui->portraits[i] = GObj_Create(4, 5, 129);
        HSD_GObjObject_80390A70(ui->portraits[i], HSD_GObj_JObjKind, joint);
        GObj_SetupGXLink(ui->portraits[i], HSD_GObj_JObjCallback, 1, 129);
        HSD_JObjAddAnimAll(joint, NULL, &material, NULL);
        HSD_JObjSetScaleX(joint, i ? 3.6f : 5.0f);
        HSD_JObjSetScaleY(joint, i ? 3.6f : 5.0f);
        HSD_JObjSetTranslateX(joint, i ? 354.0f + (i-1)*84.0f : 105.0f);
        HSD_JObjSetTranslateY(joint, i ? -285.0f : -275.0f);
        RogueRuntime_ResourceAcquire(ui->generation);
    }
}

void RogueUi_Create(RogueUiContext* ui)
{
    unsigned i;
    HSD_JObj* root;
    memset(ui, 0, sizeof(*ui));
    ui->generation = RogueRuntime_Get()->scene_generation;
    ui->storage = HSD_MemAlloc(rogue_ui_dat_size);
    memcpy(ui->storage, rogue_ui_dat, rogue_ui_dat_size);
    if (HSD_ArchiveParse(&ui->archive, ui->storage, rogue_ui_dat_size))
        OSPanic(__FILE__, __LINE__, "Invalid authored rogue UI archive");
    RogueRuntime_ResourceAcquire(ui->generation);
    ui->camera = GObj_Create(2, 3, 128);
    HSD_GObjObject_80390A70(ui->camera, HSD_GObj_CameraKind,
        HSD_CObjLoadDesc((HSD_CObjDesc*) &camera_desc));
    GObj_SetupGXLinkMax(ui->camera, HSD_GObj_803910D8, 0);
    ui->camera->gxlink_prios = 1ULL << 1;
    RogueRuntime_ResourceAcquire(ui->generation);
    ui->model = GObj_Create(4, 5, 128);
    root = HSD_JObjLoadJoint(HSD_ArchiveGetPublicAddress(&ui->archive, "RogueProgression_joint"));
    HSD_GObjObject_80390A70(ui->model, HSD_GObj_JObjKind, root);
    GObj_SetupGXLink(ui->model, HSD_GObj_JObjCallback, 1, 128);
    root = root->child;
    for (i = 0; i < ROGUE_UI_JOINTS; ++i) { ui->joints[i] = root; root = root->next; }
    RogueRuntime_ResourceAcquire(ui->generation);
    HSD_SisLib_803A62A0(4, "SdMenu.usd", "SIS_MenuData");
    HSD_SisLib_803A611C(4, NULL, 7, 8, 128, 5, 128, 0);
    RogueText_Create(&ui->text, 4, 0);
    RogueRuntime_ResourceAcquire(ui->generation);
    create_portraits(ui);
}

void RogueUi_Update(RogueUiContext* ui, const RogueRun* run, unsigned cursor, int build)
{
    unsigned i;
    for (i = 0; i < 3; ++i) {
        unsigned j;
        int cards = !build && (run->phase == ROGUE_REWARD || run->phase == ROGUE_SHOP);
        for (j = 0; j < 5; ++j)
            visible(ui->joints[ROGUE_UI_FOCUS_0 + i*5+j], cards && (j || cursor == i));
    }
    for (i = 0; i < 2; ++i) {
        unsigned j;
        for (j = 0; j < 3; ++j)
            visible(ui->joints[ROGUE_UI_ROUTE_FOCUS_0 + i*3+j], !build && run->phase == ROGUE_ROUTE && (j || cursor == i));
    }
    visible(ui->joints[ROGUE_UI_BUILD_PANEL], build);
    for (i = 0; i < 4; ++i) {
        const RogueEncounter* encounter = (run->phase == ROGUE_DEAD || run->phase == ROGUE_COMPLETE)
            ? RogueNode_Combat(&run->current) : RogueNode_Combat(&run->preview[run->phase == ROGUE_ROUTE ? cursor % 2 : 0]);
        unsigned kind = i ? encounter->fighters[i-1] : run->character;
        unsigned frame = kind == 19 ? 18 : kind > 19 ? kind - 1 : kind;
        HSD_JObj* joint = GET_JOBJ(ui->portraits[i]);
        visible(joint, !build && (!i || i <= encounter->enemy_count));
        HSD_JObjReqAnimAll(joint, (float) frame);
        HSD_JObjAnimAll(joint);
    }
    for (i = 0; i < 15; ++i) {
        GXColor color = { 74,84,116,255 };
        if (i < run->floor) { color.r=173; color.g=45; color.b=49; }
        else if (i == run->floor) { color.r=246; color.g=205; color.b=54; }
        ui->joints[ROGUE_UI_ROUTE_0+i]->u.dobj->mobj->mat->diffuse = color;
    }
}

void RogueUi_Destroy(RogueUiContext* ui)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        HSD_GObjFree(ui->portraits[i]);
        RogueRuntime_ResourceRelease(ui->generation);
    }
    lbArchive_80016EFC(ui->portrait_archive);
    RogueRuntime_ResourceRelease(ui->generation);
    RogueText_Destroy(&ui->text);
    HSD_SisLib_803A5F50(4);
    RogueRuntime_ResourceRelease(ui->generation);
    HSD_GObjFree(ui->model);
    RogueRuntime_ResourceRelease(ui->generation);
    HSD_GObjFree(ui->camera);
    RogueRuntime_ResourceRelease(ui->generation);
    HSD_Free(ui->storage);
    RogueRuntime_ResourceRelease(ui->generation);
    memset(ui, 0, sizeof(*ui));
}
