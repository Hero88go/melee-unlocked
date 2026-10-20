#include "camp_scene.h"
#include "camp_ui.h"
#include "melee_fighter.h"
#include "../../director/rogue_director.h"
#include "../../core/camp_vendor.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/mn/types.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/lb/lbaudio_ax.h>
#include <dolphin/os.h>

/* Value-only lifetime marker. Native stage/fighter objects remain owned by
 * GS_VS and are never retained by the camp adapter across scene boundaries. */
static unsigned active, frames, input_port;
int RogueCampScene_Active(void)
{
    return active && RogueRuntime_IsActive() && gm_GetCurrentGameMode()==GM_ROGUE;
}
void RogueCampScene_Prepare(StartMeleeData* match, unsigned port)
{
    RogueRun* run=RogueDirector_Run();
    unsigned i;
    if(port>=4 || !RogueCamp_Active(run))
        OSPanic(__FILE__,__LINE__,"invalid native camp visit");
    active=1; frames=0; input_port=port;
    OSReport("[rogue] camp_kit specials=%u,%u,%u,%u aerials=%u,%u,%u,%u,%u\n",
        run->specials[0],run->specials[1],run->specials[2],run->specials[3],
        run->aerials[0],run->aerials[1],run->aerials[2],run->aerials[3],run->aerials[4]);
    RogueFighter_ResetMatch();
    gm_SetupRulesDefaults(&match->rules);
    /* The native rest-room rules, without its campaign initializer/continuation. */
    match->rules.stkind=85;
    match->rules.timer_enabled=0;
    match->rules.timer_counts_up=1;
    match->rules.x1_0=1;
    match->rules.x1_2=1;
    match->rules.x1_3=1;
    match->rules.x5_1=1; /* No opponent-elimination victory in a one-player room. */
    match->rules.x4_4=0; /* No Stage Clear or bonus payout. */
    match->rules.x2_5=0;
    match->rules.x7=9;
    match->rules.xD=GM_NAMETAG_COUNT;   /* no name tag: upstream calls it GM_NAMETAG_NONE (120) */
    match->rules.item_freq=-1;
    match->rules.disable_pausing=1; /* The room owns deliberate exit and modal input. */
    for(i=0;i<GM_MAX_PLAYERS;++i) {
        PlayerInitData* player=&match->players[i];
        gm_SetupPlayerDefaults(player);
        player->slot=i;
        if(i) continue;
        player->ckind=run->character;
        player->slot_type=Gm_PKind_Human;
        player->sub_color=port;
        player->color=run->costume;
        player->stocks=run->starting_stocks;
        player->damage=run->carried_percent;
        player->xC_b1=0;
        player->xD_b2=1;
    }
    lbAudioAx_80026F2C(24);
    lbAudioAx_8002702C(4,lbAudioAx_80026E84(run->character));
    lbAudioAx_8002702C(8,lbAudioAx_80026EBC(85));
    lbAudioAx_80027168();
    gm_LoadRumbleEnabled(match);
    OSReport("[rogue] camp_prepare kind=%u node=%u stage=85 character=%u port=%u resources=%u\n",
        run->camp.kind,run->camp.node_id,run->character,port,RogueRuntime_Get()->match_resources);
}
void RogueCampScene_Frame(void)
{
    Fighter_GObj* object;
    Fighter* fighter;
    if(!RogueCampScene_Active() || RogueRuntime_Get()->scene!=GS_VS) return;
    object=Player_GetEntity(0);
    fighter=object ? object->user_data : NULL;
    if(!fighter) return;
    RogueCampUi_Frame(input_port);
    if(++frames%60==0) OSReport("[rogue] camp_observe frame=%u x=%d y=%d air=%u motion=%u damage=%d resources=%u kind=%u\n",
        frames,(int)fighter->cur_pos.x,(int)fighter->cur_pos.y,
        fighter->ground_or_air,fighter->motion_id,Player_GetDamage(0),RogueRuntime_Get()->match_resources,fighter->kind);
}
void RogueCampScene_End(void)
{
    active=frames=0;
}
