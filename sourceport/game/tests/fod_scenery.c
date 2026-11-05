#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef unsigned int u32;
enum { JOBJ_PTCL=0x20, JOBJ_SPLINE=0x4000, JOBJ_INSTANCE=0x1000, DOBJ_HIDDEN=1 };
static const int HSD_GObj_JObjKind=3;
#define DP(slot) (slot)
typedef struct { void* image_ptr; } HSD_ImageDesc;
typedef struct HSD_TObj { struct HSD_TObj* next; HSD_ImageDesc* imagedesc; } HSD_TObj;
typedef struct { HSD_TObj* tobj; } HSD_MObj;
typedef struct HSD_DObj { struct HSD_DObj* next; HSD_MObj* mobj; u32 flags; } HSD_DObj;
typedef struct HSD_JObj { struct HSD_JObj *child, *next; u32 flags; union { HSD_DObj* dobj; } u; } HSD_JObj;
typedef struct { int obj_kind; void* hsd_obj; unsigned long long gxlink_prios; } HSD_GObj;
typedef struct { void* unk0; } UnkArchiveStruct;
typedef struct MuFodScenery {
    HSD_JObj* background;
    HSD_DObj* panorama;
    HSD_JObj* landmarks[16];
    int count;
} MuFodScenery;

static char panorama_image, column_image, star_image, other_image;
static int missing_symbol, visible=1, camera_mode;
static UnkArchiveStruct archive={&archive};
static HSD_GObj* objects[4];
static UnkArchiveStruct* grDatFiles_801C6330(int map) { assert(map==3); return &archive; }
static HSD_GObj* Ground_GetMapGObj(int map) { assert(map>=1 && map<=3); return objects[map]; }
static void* HSD_ArchiveGetPublicAddress(void* arc, const char* name)
{
    assert(arc==&archive);
    if (missing_symbol) return NULL;
    if (!strcmp(name,"GrdIzumiKyoumen1_CMPR_image")) return &panorama_image;
    if (!strcmp(name,"GrdIzumiShrine1_CMPR_image")) return &column_image;
    assert(!strcmp(name,"GrdIzumiStar1_CMPR_image")); return &star_image;
}
static int Camera_80030AC4(void) { return visible; }
static void Camera_80031074(int mode) { camera_mode=mode; }
static HSD_DObj* checked_panorama;
static int drawn, passes;
static unsigned long long links[8];
static u32 masks[8];
static void HSD_StateInvalidate(int mask) { assert(mask==-1); }
static u32 HSD_GObj_80390EB8(int pass) { static const u32 flags[]={1,4,2}; return flags[pass]; }
static void HSD_JObjDispAll(HSD_JObj* joint, void* view, u32 flags, u32 mode)
{
    assert(joint && !view && flags && !mode);
    assert(checked_panorama->flags & DOBJ_HIDDEN); ++drawn;
}
static void HSD_JObjDisp(HSD_JObj* joint, void* view, u32 flags, u32 mode)
{ HSD_JObjDispAll(joint,view,flags,mode); }
static void HSD_GObj_80390ED0(HSD_GObj* camera, u32 mask)
{ assert(passes<8); links[passes]=camera->gxlink_prios; masks[passes++]=mask; }
static void HSD_FogSet(void* fog) { assert(!fog); }

#include "mu_fod_joint_uses_image_body.h"
#include "mu_fod_scenery_plan_body.h"
#include "mu_fod_draw_scenery_body.h"
#include "mu_fod_render_reflection_scene_body.h"

int main(void)
{
    HSD_ImageDesc images[]={{&panorama_image},{&column_image},{&star_image}};
    HSD_TObj textures[]={{NULL,&images[0]},{NULL,&images[1]},{NULL,&images[2]}};
    HSD_MObj materials[]={{&textures[0]},{&textures[1]},{&textures[2]}};
    HSD_DObj picture={NULL,&materials[0],0x90}, column={NULL,&materials[1],0x48}, star={NULL,&materials[2],0x24};
    HSD_JObj mirror_mesh={NULL,NULL,0,{&picture}};
    HSD_JObj mirror_root={&mirror_mesh,NULL,0,{NULL}}, mirror_wrapper={&mirror_root,NULL,0,{NULL}};
    HSD_JObj geometry[17]={{0}};
    HSD_JObj stage_root={geometry,NULL,0,{NULL}}, stage_wrapper={&stage_root,NULL,0,{NULL}};
    HSD_JObj background={NULL,NULL,0,{NULL}};
    HSD_GObj bg={3,&background,0}, mirror={3,&mirror_wrapper,0}, stage={3,&stage_wrapper,0}, camera={0,NULL,0};
    MuFodScenery plan;
    int i;
    for (i=0;i<3;++i) { geometry[i].u.dobj=i==2?&star:&column; geometry[i].next=i==2?NULL:&geometry[i+1]; }
    objects[1]=&bg; objects[2]=&mirror; objects[3]=&stage; checked_panorama=&picture;
    assert(mu_fod_scenery_plan(&plan) && plan.count==3 && plan.panorama==&picture);
    assert(mu_fod_joint_uses_image(geometry,&column_image));
    assert(!mu_fod_joint_uses_image(geometry,&other_image));
    geometry[0].flags=JOBJ_PTCL; assert(!mu_fod_joint_uses_image(geometry,&column_image)); geometry[0].flags=0;
    mu_fod_render_reflection_scene(&camera,1);
    assert(drawn==12 && passes==4 && camera_mode==0 && camera.gxlink_prios==0x80);
    assert(links[0]==1 && masks[0]==3 && links[1]==0x25 && masks[1]==3 && links[2]==0x70 && masks[2]==7 && links[3]==0x80 && masks[3]==7);
    assert(picture.flags==0x90 && column.flags==0x48 && star.flags==0x24);
    drawn=passes=0; mu_fod_render_reflection_scene(&camera,0);
    assert(!drawn && passes==3 && links[0]==0x25 && picture.flags==0x90);
    missing_symbol=1; drawn=passes=0; mu_fod_render_reflection_scene(&camera,1);
    assert(!drawn && passes==3 && picture.flags==0x90); missing_symbol=0;
    visible=0; drawn=passes=0; mu_fod_render_reflection_scene(&camera,1);
    assert(!drawn && passes==3); visible=1;
    picture.flags|=DOBJ_HIDDEN; assert(!mu_fod_scenery_plan(&plan)); picture.flags=0x90;
    bg.obj_kind=0; assert(!mu_fod_scenery_plan(&plan)); bg.obj_kind=3;
    objects[2]=NULL; assert(!mu_fod_scenery_plan(&plan)); objects[2]=&mirror;
    mirror.hsd_obj=NULL; assert(!mu_fod_scenery_plan(&plan)); mirror.hsd_obj=&mirror_wrapper;
    stage_wrapper.child=NULL; assert(!mu_fod_scenery_plan(&plan)); stage_wrapper.child=&stage_root;
    for (i=0;i<17;++i) { geometry[i].u.dobj=&column; geometry[i].next=i==16?NULL:&geometry[i+1]; }
    assert(!mu_fod_scenery_plan(&plan));
    return 0;
}
