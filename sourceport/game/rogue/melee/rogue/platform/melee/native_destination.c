#include "native_destination.h"
#include "native_target_courses.h"
#include "native_race_rules.h"
#include "../../director/rogue_director.h"
#include <melee/gr/types.h>
#include <melee/mp/types.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <dolphin/os.h>
#include <string.h>

/* Scene-owned presentation objects only. Never install stage callbacks,
 * collision, campaign globals, item spawners or live demo fighters. */
static HSD_Archive* archives[2];
static HSD_GObj* cameras[2];
#define DESTINATION_MODELS 4
static HSD_GObj* models[2][DESTINATION_MODELS];
static Vec3 scales[2][DESTINATION_MODELS];
static Vec3 focus_pivots[2];
static unsigned generation;
/* IntroEasy owns link 17 for its native sprite camera. Never share that
 * rendering list: a camera can invoke every object on its selected links. */
static const unsigned model_links[2]={16,18};
/* Match Ground_GetStageGObj's independent parent: focus scaling must not
 * replace the authored model root's own animation transform. */
static HSD_Joint transform_root={
    NULL,0,NULL,NULL,NULL,{0,0,0},{1,1,1},{0,0,0},NULL,NULL
};
static HSD_WObjDesc eye={NULL,{0,20,85},NULL};
static HSD_WObjDesc target={NULL,{0,20,0},NULL};
static HSD_CameraDescPerspective descriptor={
    NULL,0,PROJ_PERSPECTIVE,{20,310,128,332},{20,310,128,332},
    DISC_STATIC(&eye),DISC_STATIC(&target),0,{0},1,2500,30,290.0f/204.0f
};
/* Native build: a disc slot cannot hold an address at compile time (mu_disc.h DISC_STATIC). */
__attribute__((constructor)) static void rogue_destination_disc_fixups(void)
{
    DISC_SET(descriptor.eyepos,&eye);
    DISC_SET(descriptor.interest,&target);
}
/* Native grDisplay_801C5DB0 invalidates cached GX state before a stage
 * model draw. Keep that presentation boundary without its Ground callbacks. */
static void draw_model(HSD_GObj* object,intptr_t pass)
{
    unsigned side,i;
    for(side=0;side<2;++side) if(HSD_GObj_804D7818==cameras[side]) {
        for(i=0;i<DESTINATION_MODELS;++i) if(object==models[side][i]) {
            HSD_StateInvalidate(-1);
            HSD_GObj_JObjCallback(object,pass);
            return;
        }
    }
}
static void draw_camera(HSD_GObj* object,intptr_t pass)
{
    HSD_CObj* camera=object->hsd_obj;
    (void)pass;
    if(!object->gxlink_prios) return;
    if(HSD_CObjSetCurrent(camera)) {
        /* This viewport has a different projection from the VS cameras.
         * Clear only its depth; native backdrop color and other side survive. */
        HSD_CObjEraseScreen(camera,0,0,1);
        HSD_GObj_80390ED0(object,7);
        HSD_CObjEndCurrent();
    }
}
static void frame_course(Vec3* bounds,HSD_CObj* camera,const char* name)
{
    Vec3 look,eye_pos;
    float width=bounds[1].x-bounds[0].x,height=bounds[1].y-bounds[0].y,depth;
    if(width<0) width=-width;
    if(height<0) height=-height;
    if(width<=0 || height<=0) OSPanic(__FILE__,__LINE__,"empty course camera bounds");
    look.x=(bounds[0].x+bounds[1].x)*0.5f;
    look.y=(bounds[0].y+bounds[1].y)*0.5f;look.z=0;
    depth=height/(2.0f*0.2679492f);
    if(width/(2.0f*0.2679492f*(290.0f/204.0f))>depth)
        depth=width/(2.0f*0.2679492f*(290.0f/204.0f));
    eye_pos=look;eye_pos.z=depth*1.05f;
    HSD_CObjSetEyePosition(camera,&eye_pos);HSD_CObjSetInterest(camera,&look);
    HSD_CObjSetFar(camera,10000.0f);
    OSReport("[rogue] native_%s_camera bounds=1 center=%f,%f depth=%f\n",name,look.x,look.y,eye_pos.z);
}
static void collision_camera(HSD_Archive* archive,HSD_CObj* camera,const char* name)
{
    /* Traversal camera locators can describe a local gameplay window. Show the
     * authored course bounds without installing collision or live callbacks. */
    MapCollData* collision=HSD_ArchiveGetPublicAddress(archive,"coll_data");
    Vec3 bounds[2];
    int i;
    if(!collision || collision->joint_count<=0) OSPanic(__FILE__,__LINE__,"missing race course bounds");
    bounds[0].x=DP(collision->joints)[0].left_bound;bounds[0].y=DP(collision->joints)[0].bottom_bound;
    bounds[1].x=DP(collision->joints)[0].right_bound;bounds[1].y=DP(collision->joints)[0].top_bound;
    for(i=1;i<collision->joint_count;++i) {
        struct MapJoint* joint=&DP(collision->joints)[i];
        if(joint->left_bound<bounds[0].x) bounds[0].x=joint->left_bound;
        if(joint->bottom_bound<bounds[0].y) bounds[0].y=joint->bottom_bound;
        if(joint->right_bound>bounds[1].x) bounds[1].x=joint->right_bound;
        if(joint->top_bound>bounds[1].y) bounds[1].y=joint->top_bound;
    }
    frame_course(bounds,camera,name);
}
static void course_camera(UnkStageDat* stage,HSD_Joint* source,HSD_JObj* joint,HSD_CObj* camera,const char* name)
{
    /* Same point table and authored joint numbering used by
     * Ground_801C34AC/801C39C0, without installing any stage globals. */
    struct PointMap { DISC_PTR(HSD_Joint) joint; DISC_PTR(be_s16) pairs; int count; } DISC_STRUCT;   /* disc data */
    struct PointMap* maps=DISC_GET(struct PointMap,stage->unk0);
    Vec3 bounds[2];
    unsigned found=0;
    int i,j;
    for(i=0;i<stage->unk4;++i) if(DP(maps[i].joint)==source) {
        for(j=0;j<maps[i].count;++j) {
            int point=BEV(DP(maps[i].pairs)[2*j+1]);
            if(point==0x95 || point==0x96) {
                HSD_JObj* locator=NULL;
                unsigned index=point-0x95;
                lb_80011E24(joint,&locator,BEV(DP(maps[i].pairs)[2*j]),-1);
                if(!locator || (found&(1U<<index))) OSPanic(__FILE__,__LINE__,"invalid target camera point");
                lb_8000B1CC(locator,NULL,&bounds[index]);
                found|=1U<<index;
            }
        }
    }
    if(found!=3) OSPanic(__FILE__,__LINE__,"missing target camera bounds");
    frame_course(bounds,camera,name);
}
static HSD_JObj* dash_point(UnkStageDat* stage,HSD_Joint* source,HSD_JObj* joint,int id)
{
    struct PointMap { DISC_PTR(HSD_Joint) joint; DISC_PTR(be_s16) pairs; int count; } DISC_STRUCT;   /* disc data */
    struct PointMap* maps=DISC_GET(struct PointMap,stage->unk0);
    HSD_JObj* result=NULL;
    int i,j;
    for(i=0;i<stage->unk4;++i) if(DP(maps[i].joint)==source) {
        for(j=0;j<maps[i].count;++j) if(BEV(DP(maps[i].pairs)[2*j+1])==id) {
            if(result) OSPanic(__FILE__,__LINE__,"duplicate dash preview locator");
            lb_80011E24(joint,&result,BEV(DP(maps[i].pairs)[2*j]),-1);
        }
    }
    if(!result) OSPanic(__FILE__,__LINE__,"missing dash preview locator");
    return result;
}
static void dash_camera(UnkStageDat* stage,HSD_Joint* source,HSD_JObj* joint,HSD_CObj* camera,Vec3* pivot)
{
    Vec3 spawn,origin,bounds[2],pos;
    HSD_JObj* locator;
    int i;
    /* grKinokoRoute_80207490 centers the live view at player x,y+20.
     * Use the authored player-start locator without creating a fighter. */
    lb_8000B1CC(dash_point(stage,source,joint,0),NULL,&spawn);
    lb_8000B1CC(dash_point(stage,source,joint,0x94),NULL,&origin);
    for(i=0;i<2;++i) {
        lb_8000B1CC(dash_point(stage,source,joint,0x95+i),NULL,&bounds[i]);
        bounds[i].x+=spawn.x-origin.x;
        bounds[i].y+=spawn.y+20.0f-origin.y;
    }
    /* Native 80207634/802078F0 position these background anchors relative
     * to the player. Apply the initial pose only to this scene's own joints. */
    for(i=0;i<4;++i) {
        locator=dash_point(stage,source,joint,0x7F+i);
        HSD_JObjGetTranslation(locator,&pos);
        pos.x+=spawn.x-origin.x;pos.y+=spawn.y-origin.y;
        HSD_JObjSetTranslate(locator,&pos);
    }
    frame_course(bounds,camera,"dash");
    HSD_CObjGetInterest(camera,pivot);
    OSReport("[rogue] native_dash_start spawn=%f,%f parallax=4 pivot=1\n",spawn.x,spawn.y);
}
void RogueNativeDestination_Create(void)
{
    const RogueRun* run=RogueDirector_Run();
    UnkStageDat* stage;
    unsigned side,i;
    static const unsigned room_parts[2]={1,3};
    /* Retail HRC model 0 is a locator skeleton, not visible geometry.
     * Model 10 owns the starting platform, 3 the distant scenery, and 4
     * the first runway section (authored translation x=160).
     * Its gameplay initializer also creates cameras/items/tiled distance
     * geometry, none of which belong in this scene-owned visual preview. */
    static const unsigned home_run_parts[3]={10,3,4};
    static const unsigned target_parts[3]={0,1,2};
    static const unsigned dash_parts[4]={2,0,1,3};
    const char* names[2]={NULL,NULL};
    if(generation) OSPanic(__FILE__,__LINE__,"destination preview overlap");
    for(side=0;side<2;++side) {
        unsigned kind=run->preview[side].kind;
        unsigned sandbag=kind==ROGUE_NODE_EVENT &&
            run->preview[side].payload.event.template_id==ROGUE_EVENT_SANDBAG;
        unsigned home_run=kind==ROGUE_NODE_EVENT &&
            (run->preview[side].payload.event.template_id==ROGUE_EVENT_HOME_RUN || sandbag);
        unsigned targets=kind==ROGUE_NODE_EVENT &&
            run->preview[side].payload.event.template_id==ROGUE_EVENT_TARGETS;
        unsigned race=kind==ROGUE_NODE_EVENT &&
            run->preview[side].payload.event.template_id==ROGUE_EVENT_RACE;
        unsigned dash=kind==ROGUE_NODE_EVENT &&
            run->preview[side].payload.event.template_id==ROGUE_EVENT_DASH;
        unsigned escape=kind==ROGUE_NODE_EVENT &&
            run->preview[side].payload.event.template_id==ROGUE_EVENT_ESCAPE;
        const unsigned* parts=dash ? dash_parts : (targets || race || escape) ? target_parts : home_run ? home_run_parts : room_parts;
        if(kind!=ROGUE_NODE_SHOP && kind!=ROGUE_NODE_REST && !home_run && !targets && !race && !dash && !escape) continue;
        if(sandbag) {
            const RogueEventDesc* event=&run->preview[side].payload.event;
            /* Rogue-authored challenge uses the native practice platform only.
             * No bat, distance tracking or HRC callbacks enter this preview. */
            if(event->native_stage!=84 || event->timer_frames!=20*60)
                OSPanic(__FILE__,__LINE__,"sandbag preview stage/timer mismatch");
            OSReport("[rogue] native_sandbag_course stage=%u seconds=%u\n",event->native_stage,event->timer_frames/60);
        }
        if(targets) {
            unsigned base=run->preview[side].payload.event.base_character;
            if(base>=26 || rogue_target_courses[base].stage!=run->preview[side].payload.event.native_stage)
                OSPanic(__FILE__,__LINE__,"target preview course does not match base character");
            names[side]=rogue_target_courses[base].archive;
            OSReport("[rogue] native_target_course base=%u stage=%u archive=%s\n",base,rogue_target_courses[base].stage,names[side]);
        } else if(race) {
            const RogueEventDesc* event=&run->preview[side].payload.event;
            if(event->base_character>=26 || event->native_stage!=82 ||
                event->timer_frames!=60*rogue_race_seconds[event->base_character])
                OSPanic(__FILE__,__LINE__,"race preview differs from native course/timer");
            names[side]="GrNPo.dat";
            OSReport("[rogue] native_race_course base=%u stage=%u seconds=%u\n",
                event->base_character,event->native_stage,event->timer_frames/60);
        } else if(dash) {
            const RogueEventDesc* event=&run->preview[side].payload.event;
            if(event->native_stage!=59 || event->timer_frames!=90*60)
                OSPanic(__FILE__,__LINE__,"dash preview course/timer mismatch");
            names[side]="GrNKr.dat";
            OSReport("[rogue] native_dash_course stage=%u seconds=%u\n",event->native_stage,event->timer_frames/60);
        } else if(escape) {
            const RogueEventDesc* event=&run->preview[side].payload.event;
            /* Adventure scene0x1B supplies x2=0x28 seconds and x6=0x42. */
            if(event->native_stage!=66 || event->timer_frames!=40*60)
                OSPanic(__FILE__,__LINE__,"escape preview course/timer mismatch");
            names[side]="GrNZr.dat";
            OSReport("[rogue] native_escape_course stage=%u seconds=%u\n",event->native_stage,event->timer_frames/60);
        } else names[side]=home_run ? "GrHr.dat" : "GrHe.dat";
        if(side && names[0] && !strcmp(names[0],names[side])) archives[side]=archives[0];
        else {
            generation=RogueRuntime_Get()->scene_generation;
            archives[side]=lbArchive_LoadArchive(names[side]);
            RogueRuntime_ResourceAcquire(generation);
        }
        stage=HSD_ArchiveGetPublicAddress(archives[side],"map_head");
        if(!stage || stage->unkC!=(dash ? 4 : (targets || escape) ? 3 : race ? 2 : home_run ? 11 : 5))
            OSPanic(__FILE__,__LINE__,"retail destination model layout changed");
        descriptor.viewport.xmin=descriptor.scissor.left=side ? 330 : 20;
        descriptor.viewport.xmax=descriptor.scissor.right=side ? 620 : 310;
        if(HSD_GObjGXLinkHead[model_links[side]])
            OSPanic(__FILE__,__LINE__,"destination render link is already owned");
        cameras[side]=GObj_Create(2,3,128);
        HSD_GObjObject_80390A70(cameras[side],HSD_GObj_CameraKind,
            HSD_CObjLoadDesc((HSD_CObjDesc*)&descriptor));
        if(home_run) {
            Vec3 home_eye={0,25,180},home_target={0,0,0};
            HSD_CObjSetEyePosition(cameras[side]->hsd_obj,&home_eye);
            HSD_CObjSetInterest(cameras[side]->hsd_obj,&home_target);
        }
        GObj_SetupGXLinkMax(cameras[side],draw_camera,18);
        cameras[side]->gxlink_prios=1ULL<<model_links[side];
        RogueRuntime_ResourceAcquire(generation);
        for(i=0;i<(dash ? 4 : home_run || targets || escape ? 3 : 2);++i) {
            struct UnkStageDat_x8_t* model=&DP(stage->unk8)[parts[i]];
            HSD_JObj* joint=HSD_JObjLoadJoint(DP(model->unk0));
            HSD_JObj* root=HSD_JObjLoadJoint(&transform_root);
            HSD_JObjAddNext(joint,root);
            models[side][i]=GObj_Create(4,5,128);
            HSD_GObjObject_80390A70(models[side][i],HSD_GObj_JObjKind,root);
            HSD_JObjAddAnimAll(joint,DISC_NULL(model->unk4) ? NULL : DP(DP(model->unk4)[0]),
                DISC_NULL(model->unk8) ? NULL : DP(DP(model->unk8)[0]),DISC_NULL(model->unkC) ? NULL : DP(DP(model->unkC)[0]));
            HSD_JObjReqAnimAll(joint,0);HSD_JObjAnimAll(joint);
            if(targets && i==0) course_camera(stage,DP(model->unk0),joint,cameras[side]->hsd_obj,"target");
            if(race && i==0) collision_camera(archives[side],cameras[side]->hsd_obj,"race");
            if(escape && i==0) {
                collision_camera(archives[side],cameras[side]->hsd_obj,"escape");
                HSD_CObjGetInterest(cameras[side]->hsd_obj,&focus_pivots[side]);
            }
            if(dash && parts[i]==0) dash_camera(stage,DP(model->unk0),joint,cameras[side]->hsd_obj,&focus_pivots[side]);
            scales[side][i]=root->scale;
            GObj_SetupGXLink(models[side][i],draw_model,model_links[side],128+i);
            RogueRuntime_ResourceAcquire(generation);
        }
        OSReport("[rogue] native_destination side=%u kind=%u archive=%s parts=%s generation=%u\n",
            side,kind,names[side],dash ? "2,0,1,3" : (targets || escape) ? "0,1,2" : race ? "0,1" : home_run ? "10,3,4" : "1,3",generation);
    }
}
void RogueNativeDestination_Focus(unsigned side,unsigned phase)
{
    unsigned s,i;
    for(s=0;s<2;++s) for(i=0;i<DESTINATION_MODELS;++i) if(models[s][i]) {
        float factor=phase!=ROGUE_ROUTE || side==s ? 1.0f : .92f;
        Vec3 scale=scales[s][i];
        scale.x*=factor;scale.y*=factor;scale.z*=factor;
        HSD_JObjSetScale(models[s][i]->hsd_obj,&scale);
        if(focus_pivots[s].x || focus_pivots[s].y) {
            Vec3 position=focus_pivots[s];
            position.x*=1.0f-factor;position.y*=1.0f-factor;position.z*=1.0f-factor;
            HSD_JObjSetTranslate(models[s][i]->hsd_obj,&position);
        }
    }
}
void RogueNativeDestination_Hide(unsigned side)
{ if(side<2 && cameras[side]) cameras[side]->gxlink_prios=0; }
void RogueNativeDestination_End(void)
{
    unsigned side,i;
    if(!generation) return;
    for(side=0;side<2;++side) {
        for(i=0;i<DESTINATION_MODELS;++i) if(models[side][i]) {
            HSD_GObjFree(models[side][i]);RogueRuntime_ResourceRelease(generation);
        }
        if(cameras[side]) {HSD_GObjFree(cameras[side]);RogueRuntime_ResourceRelease(generation);}
    }
    if(archives[1] && archives[1]!=archives[0]) {
        lbArchive_80016EFC(archives[1]);RogueRuntime_ResourceRelease(generation);
    }
    if(archives[0]) {lbArchive_80016EFC(archives[0]);RogueRuntime_ResourceRelease(generation);}
    OSReport("[rogue] native_destination_end generation=%u\n",generation);
    memset(models,0,sizeof(models));memset(cameras,0,sizeof(cameras));
    memset(archives,0,sizeof(archives));memset(focus_pivots,0,sizeof(focus_pivots));generation=0;
}
