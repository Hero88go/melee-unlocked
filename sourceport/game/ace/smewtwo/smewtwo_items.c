/* PlSm.dat article 1 and 3, and ft +31D8. */
#include "smewtwo.h"
#include <melee/db/db.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/inlines.h>
#include <melee/it/inlines.h>
#include <melee/it/item.h>
#include <melee/it/it_2725.h>
#include <melee/it/it_3F14.h>
#include <melee/it/itgroundcoll.h>
#include <melee/it/itmaplib.h>
#include <melee/it/kinds/types.h>
#include <melee/mp/mplib.h>

static HSD_GObj** itSm_Owner(Item* ip) { return (HSD_GObj**)&ip->xDD4_itemVar; }
static bool itSm_Floor(Vec3* pos,int x,int y,int endy)
{
    Vec3 p=*pos,normal;
    int line;
    u32 flags;
    p.x+=(float)x;p.y+=(float)y;
    return mpCheckFloor(p.x,p.y,p.x,p.y+(float)endy,0,&p,&line,&flags,&normal,-1,-1,-1,NULL,NULL);
}
static void itSm_Empty(Item_GObj* gobj) {}
static void itSm_SetSilent(Item* ip)
{
    ip->xCC_item_attr->destroy_gfx=-1;
    ip->xCC_item_attr->destroy_sfx=-1;
}
void ftSm_Projectiles_Spawn(HSD_GObj* gobj)
{
    Fighter* fp=GET_FIGHTER(gobj);
    SpawnItem spawn={0};
    spawn.x0_parent_gobj=spawn.x4_parent_gobj2=gobj;
    spawn.kind=mu_ak_article_kind(gobj,1);
    spawn.hold_kind=8;
    ftLib_80086990(gobj,&spawn.pos);
    spawn.prev_pos.x=__builtin_fmaf(fp->facing_dir,15,fp->cur_pos.x);
    spawn.prev_pos.y=fp->cur_pos.y-5;
    spawn.facing_dir=fp->facing_dir;
    spawn.x44_flag.b0=true;
    Item_GObj* item=Item_80268B18(&spawn);
    Item* ip=GET_ITEM(item);
    *itSm_Owner(ip)=gobj;
    it_80275158(item,35);
    itSm_SetSilent(ip);
    Item_80268E5C(item,0,2);
    db_80225DD8(item,gobj);
    it_802750F8(item);
}
static void itSm_FlameSpawn(HSD_GObj* owner,Item_GObj* source)
{
    Fighter* fp=GET_FIGHTER(owner);
    SpawnItem spawn={0};
    spawn.x0_parent_gobj=spawn.x4_parent_gobj2=owner;
    spawn.kind=mu_ak_article_kind(owner,3);
    spawn.hold_kind=8;
    spawn.pos=spawn.prev_pos=GET_ITEM(source)->pos;
    spawn.facing_dir=fp->facing_dir;
    spawn.x44_flag.b0=true;
    Item_GObj* item=Item_80268B18(&spawn);
    it_80275158(item,40);
    itSm_SetSilent(GET_ITEM(item));
    Item_80268E5C(item,0,2);
    it_80274658(item,0);
    {
        /* The mod calls it_80276174 without loading r4 (+0610): r4 still holds the Item that
         * it_80274658 loaded (lwz r4,0x2C(r3)), so the console sets the collision's last_pos from
         * the Item's first three words read as floats (x0, entity, x8: pool and gobj addresses,
         * around -1e-38, or 0). Natively those words are host pointers; a zero vector stands for
         * them (the specials digest against the Static Recomp checks that the difference never
         * shows). A NULL here faulted. */
        Vec3 console_words = {0.0F, 0.0F, 0.0F};
        it_80276174(item,&console_words);
    }
    db_80225DD8(item,owner);
    it_802750F8(item);
}
static bool itSm_Travel(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    Fighter* fp=GET_FIGHTER(*itSm_Owner(ip));
    if((u32)(fp->motion_id-75)<=14) Item_8026A8EC(gobj);
    if(ip->xDAC_itcmd_var0!=1 || (fp->input.held_buttons[0]&0x200)) ip->x40_vel.x=fp->facing_dir*2.5f;
    else ip->x40_vel.x=0;
    ip->xD44_lifeTimer-=1;
    if(ip->xD44_lifeTimer<=0) {   /* +0088: cror eq,gt,un; bne: ordered <= 0 (not < 0) */
        it_80275158(gobj,40);
        Item_80268E5C(gobj,1,2);
    }
    return false;
}
static bool itSm_TravelColl(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    Fighter* fp=GET_FIGHTER(*itSm_Owner(ip));
    it_8026D9A0(gobj);
    int x=(int)fp->facing_dir*5;
    if(itSm_Floor(&ip->pos,x,0,-5)) return false;
    if(!itSm_Floor(&ip->pos,x,0,-100)) {
        ftSm_GetVars(fp)->hud=NULL;
        return true;
    }
    if(!(fp->input.held_buttons[0]&0x200) && itSm_Floor(&ip->pos,0,0,-5)) {
        Item_80268E5C(gobj,1,2);
        return false;
    }
    for(int i=0;i<10;i++) {
        ip->pos.y-=5;
        if(itSm_Floor(&ip->pos,x,0,-10)) return false;
    }
    return true;
}
static bool itSm_Stationary(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    HSD_GObj* owner=*itSm_Owner(ip);
    Fighter* fp=GET_FIGHTER(owner);
    if((u32)(fp->motion_id-75)<=14) Item_8026A8EC(gobj);
    ip->x40_vel.x=0;
    ip->xD44_lifeTimer-=1;
    if(ip->xDB4_itcmd_var2==1) {
        itSm_FlameSpawn(owner,gobj);
        ip->xDB4_itcmd_var2=0;
    }
    return ip->xD44_lifeTimer<=0;
}
static bool itSm_FlameAnim(Item_GObj* gobj)
{
    Item* ip=GET_ITEM(gobj);
    ip->xD44_lifeTimer-=1;
    return ip->xD44_lifeTimer<=0;
}
static void itSm_FlamePhys(Item_GObj* gobj) { it_80272860(gobj,0,0); }
static bool itSm_FlameColl(Item_GObj* gobj)
{
    it_8026E414(gobj,itSm_Empty);
    return false;
}
static ItemStateTable itSm_TravelStates[]={
    {0,itSm_Travel,NULL,itSm_TravelColl},
    {1,itSm_Stationary,NULL,NULL},
};
static ItemStateTable itSm_FlameStates[]={
    {0,itSm_FlameAnim,itSm_FlamePhys,itSm_FlameColl},
};
static ItemLogicTable itSm_TravelLogic={.states=itSm_TravelStates};
static ItemLogicTable itSm_FlameLogic={.states=itSm_FlameStates};
ItemLogicTable* const itSm_ArticleTables[4]={NULL,&itSm_TravelLogic,NULL,&itSm_FlameLogic};
