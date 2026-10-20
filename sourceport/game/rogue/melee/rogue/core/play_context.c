#include "play_context.h"
#include "../combat/specials/special_catalog.h"

unsigned RoguePlay_EventKit(const RogueRun* run, const RogueEventDesc* event)
{
    unsigned slot, borrowed=0;
    if(!run || !event || run->character>=26 || event->base_character!=run->character ||
       !event->template_id || event->template_id>ROGUE_EVENT_TEMPLATES ||
       event->native_kit_assist>1) return ROGUE_EVENT_KIT_INVALID;
    if(event->native_kit_assist)
        return RogueEvent_HasNativeKit(event->template_id) ?
            ROGUE_EVENT_KIT_NATIVE : ROGUE_EVENT_KIT_INVALID;
    for(slot=0;slot<4;++slot) if(run->specials[slot]) {
        const RogueSpecialDef* def=RogueSpecial_Find(run->specials[slot]);
        if(!def || def->slot!=slot) return ROGUE_EVENT_KIT_INVALID;
        borrowed=1;
    }
    for(slot=0;slot<5;++slot) if(run->aerials[slot]) {
#if ROGUE_ENABLE_AERIALS
        const RogueAerialDef* def=RogueAerial_Find(run->aerials[slot]);
        if(!def || def->slot!=slot) return ROGUE_EVENT_KIT_INVALID;
        borrowed=1;
#else
        return ROGUE_EVENT_KIT_INVALID;
#endif
    }
    return borrowed ? ROGUE_EVENT_KIT_EQUIPPED : ROGUE_EVENT_KIT_NATIVE;
}
const char* RoguePlay_EventKitLabel(const RogueRun* run, const RogueEventDesc* event)
{
    unsigned kit=RoguePlay_EventKit(run,event);
    if(kit==ROGUE_EVENT_KIT_INVALID) return "Kit invalid - entry blocked";
    if(kit==ROGUE_EVENT_KIT_EQUIPPED) return "Borrowed kit - course not certified";
    return "Native moves - passives kept";
}

int RoguePlay_NativeKit(const RogueRun* run)
{
    return run && run->phase==ROGUE_EVENT && run->current.kind==ROGUE_NODE_EVENT &&
        RogueEvent_HasNativeKit(run->current.payload.event.template_id) &&
        run->current.payload.event.native_kit_assist==1;
}
unsigned RoguePlay_Special(const RogueRun* run, unsigned slot)
{
    return !run || slot>=4 || RoguePlay_NativeKit(run) ? 0 : run->specials[slot];
}
unsigned RoguePlay_Aerial(const RogueRun* run, unsigned slot)
{
    return !run || slot>=5 || RoguePlay_NativeKit(run) ? 0 : run->aerials[slot];
}

unsigned RoguePlay_Kind(const RogueRun* run)
{
    if(!run || run->legacy_active_incompatible) return ROGUE_PLAY_NONE;
    if(run->phase==ROGUE_FIGHT && run->current.kind<=ROGUE_BOSS) return ROGUE_PLAY_COMBAT;
    if(run->phase==ROGUE_EVENT && run->current.kind==ROGUE_NODE_EVENT)
        return run->current.payload.event.template_id==ROGUE_EVENT_GAUNTLET ?
            ROGUE_PLAY_GAUNTLET : ROGUE_PLAY_SKILL;
    if(run->phase==ROGUE_SHOP || run->phase==ROGUE_REST) return ROGUE_PLAY_CAMP;
    return ROGUE_PLAY_NONE;
}
unsigned RoguePlay_FighterRole(const RogueRun* run, unsigned slot)
{
    unsigned play=RoguePlay_Kind(run);
    if(slot>=6 || play==ROGUE_PLAY_NONE || play==ROGUE_PLAY_CAMP) return ROGUE_ACTOR_NONE;
    if(!slot) return ROGUE_ACTOR_PLAYER;
    if(play==ROGUE_PLAY_COMBAT)
        return slot<=run->current.payload.combat.enemy_count ? ROGUE_ACTOR_COMBAT_OPPONENT : ROGUE_ACTOR_NONE;
    if(play==ROGUE_PLAY_GAUNTLET) return slot==1 ? ROGUE_ACTOR_COMBAT_OPPONENT : ROGUE_ACTOR_NONE;
    if(slot==1 && (run->current.payload.event.template_id==ROGUE_EVENT_HOME_RUN ||
                  run->current.payload.event.template_id==ROGUE_EVENT_SANDBAG)) return ROGUE_ACTOR_PRACTICE;
    return ROGUE_ACTOR_STAGE;
}
int RoguePlay_PlayerEffects(unsigned play, unsigned role)
{
    return role==ROGUE_ACTOR_PLAYER && (play==ROGUE_PLAY_COMBAT ||
        play==ROGUE_PLAY_SKILL || play==ROGUE_PLAY_GAUNTLET);
}
int RoguePlay_BorrowedMoves(unsigned play, unsigned role, int native_kit_selected)
{
    return !native_kit_selected && RoguePlay_PlayerEffects(play,role);
}
int RoguePlay_CombatProc(unsigned play, unsigned source, unsigned target)
{
    return (play==ROGUE_PLAY_COMBAT || play==ROGUE_PLAY_GAUNTLET) &&
        source==ROGUE_ACTOR_PLAYER && target==ROGUE_ACTOR_COMBAT_OPPONENT;
}
