/* PlSd.dat ft +3E88/+3FDC and all three itFunction blocks. */
#include "skullkid.h"
#include <math.h>
#include <melee/db/db.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/ftpickupitem.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/iteffect.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/types.h>
#include <melee/lb/lbvector.h>
#include <melee/mp/mpcoll.h>
#include <sysdolphin/baselib/jobj.h>

typedef struct itSd_Vars {
    HSD_GObj* fighter;
    float x4, x8;
} itSd_Vars;
static itSd_Vars* itSd_GetVars(Item* ip) { return (itSd_Vars*)&ip->xDD4_itemVar; }
static bool itSd_False(Item_GObj* gobj) { return false; }
static void itSd_Empty(Item_GObj* gobj) {}
static void itSd_Scale(Item_GObj* gobj,float scale)
{
    HSD_JObj* joint=it_80272CC0(gobj,0);
    joint->scale.x=joint->scale.y=joint->scale.z=scale;
    GET_ITEM(gobj)->scl=scale;
}

Item_GObj* itSd_Spell_Spawn(HSD_GObj* fighter,Vec3* pos,ItemKind kind,float facing)
{
    Fighter* fp=GET_FIGHTER(fighter);
    SpawnItem spawn={0};
    spawn.x0_parent_gobj=spawn.x4_parent_gobj2=fighter;
    spawn.kind=kind;
    ftLib_80086990(fighter,&spawn.pos);
    spawn.prev_pos.x=pos->x;
    spawn.prev_pos.y=pos->y;
    spawn.facing_dir=facing;
    Item_GObj* item=mu_ak_item_create(&spawn);
    itSd_Scale(item,0.1f);
    itSd_GetVars(GET_ITEM(item))->fighter=fighter;
    ftSd_Vars(fp)->ready--;
    ftSd_Vars(fp)->spell=item;
    ftpickupitem_800948A8(fighter,item);
    Item_80268E5C(item,0,2);
    it_80275158(item,9);
    db_80225DD8(item,fighter);
    it_802750F8(item);
    return item;
}
Item_GObj* itSd_Model_Spawn(HSD_GObj* fighter,Vec3* pos,ItemKind kind,
                          int state,int bone,float facing)
{
    SpawnItem spawn={0};
    spawn.x0_parent_gobj=spawn.x4_parent_gobj2=fighter;
    spawn.kind=kind;
    spawn.hold_kind=8;
    ftLib_80086990(fighter,&spawn.pos);
    spawn.prev_pos=*pos;
    spawn.facing_dir=facing;
    spawn.x44_flag.b0=true;
    Item_GObj* item=Item_80268B18(&spawn);
    Item* ip=GET_ITEM(item);
    itSd_GetVars(ip)->fighter=fighter;
    ip->xCC_item_attr->destroy_gfx=-1;
    ip->xCC_item_attr->destroy_sfx=-1;
    Item_80268E5C(item,state,2);
    Item_8026AB54(item,fighter,bone);
    db_80225DD8(item,fighter);
    it_802750F8(item);
    return item;
}
static void itSd_Spell_Destroy(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    itSd_Vars* v=itSd_GetVars(ip);
    ftSd_Vars(GET_FIGHTER(v->fighter))->spell=NULL;
    v->x4=v->x8=0;
}
static void itSd_Model1_Destroy(Item_GObj* gobj)
{
    ftSd_Vars(GET_FIGHTER(itSd_GetVars(GET_ITEM(gobj))->fighter))->model2=NULL;
}
static void itSd_Model2_Destroy(Item_GObj* gobj)
{
    ftSd_Vars(GET_FIGHTER(itSd_GetVars(GET_ITEM(gobj))->fighter))->model1=NULL;
}
static void itSd_Spell_Drop(Item_GObj* gobj) { Item_80268E5C(gobj,2,2); }
static void itSd_Spell_Throw(Item_GObj* gobj)
{
    Item_80268E5C(gobj,3,2);
    it_80275158(gobj,150);
    GET_ITEM(gobj)->x40_vel.y+=0.8f;
}
static bool itSd_Spell_Hit(Item_GObj* gobj)
{
    int state=GET_ITEM(gobj)->msid;
    if (state==3 || state==6) Item_8026A8EC(gobj);
    return false;
}
/* The mod computes reflected vectors and never writes them to x40_vel. */
static bool itSd_Spell_Surfaces(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    CollData* cd=&ip->x378_itemColl;
    Vec3 sum={0},speed_sum={0};
    u32 masks[]={0x3F,0xFC0,0x6000,0x18000};
    SurfaceData* surfaces[]={&cd->left_facing_wall,&cd->right_facing_wall,&cd->ceiling,&cd->floor};
    bool (*speeds[])(CollData*,Vec3*)={mpCollGetSpeedLeftWall,mpCollGetSpeedRightWall,
                                     mpCollGetSpeedCeiling,mpCollGetSpeedFloor};
    bool result=false;
    for(int i=0;i<4;i++) {
        Vec3 velocity=ip->x40_vel;
        Vec3* normal=&surfaces[i]->normal;
        if ((cd->env_flags&masks[i]) &&
            __builtin_fmaf(velocity.x,normal->x,velocity.y*normal->y)<0) {
            lbVector_Mirror(&velocity,normal);
            lbVector_Add_xy(&sum,&velocity);
            if (speeds[i](cd,&velocity)) lbVector_Add_xy(&speed_sum,&velocity);
            result=true;
        }
    }
    return result;
}
static bool itSd_Spell_Grow(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    if (ip->xD44_lifeTimer>0) {
        float scale=(float)__builtin_fma(-(double)ip->xD44_lifeTimer,0.1,1.0);
        itSd_Scale(gobj,scale);
        ip->xD44_lifeTimer-=1;
    } else Item_80268E5C(gobj,1,2);
    return false;
}
static bool itSd_Spell_Map(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    return false;
}
static void itSd_Spell_MapPhys(Item_GObj* gobj) { it_8026D9A0(gobj); }
static void itSd_Spell_Fall(Item_GObj* gobj) { it_80272860(gobj,0.1f,3); }
static void itSd_Spell_ThrownFall(Item_GObj* gobj) { it_80272860(gobj,0.1f,2.5f); }
static bool itSd_Spell_FallColl(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    if (itSd_Spell_Surfaces(gobj)) {
        Item_80268E5C(gobj,4,2);
        it_80275158(gobj,5);
    }
    return false;
}
static bool itSd_Spell_ThrownColl(Item_GObj* gobj)
{
    it_8026D9A0(gobj);
    if (itSd_Spell_Surfaces(gobj)) {
        it_802762B0(GET_ITEM(gobj));
        Item_80268E5C(gobj,4,2);
        it_80275158(gobj,5);
    }
    return false;
}
static bool itSd_Spell_ThrowAnim(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    ip->xD44_lifeTimer-=1;
    if (ip->xD44_lifeTimer==0) Item_8026A8EC(gobj);
    return false;
}
static bool itSd_Spell_Settle(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    if (ip->xD44_lifeTimer==0) {
        it_80275158(gobj,600);
        Item_80268E5C(gobj,5,2);
    }
    ip->xD44_lifeTimer-=1;
    return false;
}
static s32 itSd_Spell_BlinkCounter;
static bool itSd_Spell_Blink(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    float period=(ip->xD44_lifeTimer/600.0f)*78.0f;
    if (ip->xD44_lifeTimer==0) {
        Item_80268E5C(gobj,6,2);
        it_80275158(gobj,5);
    }
    int divisor=(int)period;
    itSd_Spell_BlinkCounter=(s32)((u32)itSd_Spell_BlinkCounter+1);
    /* divw by zero has no useful quotient, but its product with zero is
     * still zero. The console therefore keeps the counter as remainder. */
    int remainder=divisor ? itSd_Spell_BlinkCounter%divisor : itSd_Spell_BlinkCounter;
    if (remainder==0) {
        it_802787B4(gobj,417);
        itSd_Spell_BlinkCounter=0;
    }
    ip->xD44_lifeTimer-=1;
    return false;
}
static bool itSd_Spell_End(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    if (ip->xD44_lifeTimer>0) ip->xD44_lifeTimer-=1;
    else Item_8026A8EC(gobj);
    return false;
}
static ItemStateTable itSd_Spell_States[]={
    {0,itSd_Spell_Grow,itSd_Empty,itSd_Spell_Map},
    {1,itSd_False,itSd_Empty,itSd_False},
    {2,itSd_False,itSd_Spell_Fall,itSd_Spell_FallColl},
    {3,itSd_Spell_ThrowAnim,itSd_Spell_ThrownFall,itSd_Spell_ThrownColl},
    {4,itSd_Spell_Settle,itSd_Empty,itSd_False},
    {5,itSd_Spell_Blink,itSd_Empty,itSd_False},
    {6,itSd_Spell_End,itSd_Spell_MapPhys,itSd_False},
};
/* +50/+58/+60 are three exact li r3,0; blr bodies. The old inferred
 * listing ended at +50; cached data confirms the true extent is +68. */
static ItemStateTable itSd_Model_States[]={
    {0,itSd_False,itSd_Empty,itSd_False},
    {1,itSd_False,itSd_Empty,itSd_False},
    {2,itSd_False,itSd_Empty,itSd_False},
};
const ItemLogicTable itSd_Articles[3]={
    {.states=itSd_Spell_States,.spawned=itSd_Empty,.destroyed=itSd_Spell_Destroy,
     .dropped=itSd_Spell_Drop,.thrown=itSd_Spell_Throw,.dmg_dealt=itSd_Spell_Hit},
    {.states=itSd_Model_States,.spawned=itSd_Empty,.destroyed=itSd_Model1_Destroy},
    {.states=itSd_Model_States,.spawned=itSd_Empty,.destroyed=itSd_Model2_Destroy},
};
