#include "score_capture.h"
#include "melee_fighter.h"
#include "../../core/play_context.h"
#include "../../director/rogue_runtime.h"
#include "../../director/rogue_director.h"
#include "specials/special_engine.h"
#include <dolphin/os.h>
#include <melee/ft/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/pl/player.h>
static int opening_stock = -1;
static int reflecting;
void RogueFighter_ResetMatch(void) { opening_stock = -1; reflecting = 0; }
void RogueFighter_OnKO(int attacker, int partner, int victim)
{
    Fighter_GObj* entity;
    Fighter* fighter;
    RogueRun* run=RogueDirector_Run();
    if (!RogueRuntime_IsActive() || partner ||
        !RoguePlay_CombatProc(RoguePlay_Kind(run), RoguePlay_FighterRole(run,attacker),
                             RoguePlay_FighterRole(run,victim))) return;
    entity = Player_GetEntity(0);
    fighter = entity ? entity->user_data : NULL;
    if (fighter) fighter->dmg.x18F0 += RogueDirector_Run()->stacks[20] * 8;
}
void RogueFighter_HurtState(Fighter* fighter, int state)
{
    if (Rogue_IsRunPlayer(fighter) && state == 0 && fighter->x1988 == 2 &&
        fighter->motion_id >= ftCo_MS_EscapeF && fighter->motion_id <= ftCo_MS_EscapeAir &&
        RogueDirector_Run()->stacks[19])
        ftColl_8007B760(fighter->gobj, RogueDirector_Run()->stacks[19] * 2);
}
bool Rogue_IsRunPlayer(const Fighter* fighter)
{
    RogueRun* run=RogueDirector_Run();
    return RogueRuntime_IsActive() && fighter && !fighter->is_sub_fighter &&
        RoguePlay_PlayerEffects(RoguePlay_Kind(run), RoguePlay_FighterRole(run,fighter->player_idx));
}

float RogueFighter_ShieldCapacity(Fighter* fighter, float base)
{
    return Rogue_IsRunPlayer(fighter) ? base * (1.0f + RogueDirector_Run()->stacks[5] * 0.10f) : base;
}
float RogueFighter_ShieldRecovery(Fighter* fighter, float base)
{
    return Rogue_IsRunPlayer(fighter) ? base * (1.0f + RogueDirector_Run()->stacks[6] * 0.08f) : base;
}
int RogueFighter_LedgeFrames(Fighter* fighter, int base)
{
    return Rogue_IsRunPlayer(fighter) ? base + RogueDirector_Run()->stacks[9] * 5 : base;
}

void RogueFighter_Created(Fighter* fighter)
{
    RogueRun* run = RogueDirector_Run();
    ftCo_DatAttrs* attr = &fighter->co_attrs;
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
    float native_walk=attr->walk_max_vel;
#endif
    unsigned play=RoguePlay_Kind(run);
    unsigned role=RoguePlay_FighterRole(run,fighter->player_idx);
    int player_effects=RoguePlay_PlayerEffects(play,role);
    int combat_opponent=play==ROGUE_PLAY_COMBAT && role==ROGUE_ACTOR_COMBAT_OPPONENT;
    RogueScore_ActorCreated(fighter->player_idx,fighter->is_sub_fighter);
    /* Event descriptors share a union with combat descriptors. Only actual
     * combat opponents may read combat modifiers; Sandbag remains native. */
    if (RogueRuntime_IsActive() && (player_effects || combat_opponent)) {
        float ground = player_effects ? 1.0f + run->stacks[2] * 0.06f : run->current.payload.combat.speed * 0.01f;
        float air = player_effects ? 1.0f + run->stacks[3] * 0.06f : ground;
        attr->walk_max_vel *= ground;
        attr->dash_initial_velocity *= ground;
        attr->dash_max_velocity *= ground;
        attr->ground_max_horizontal_velocity *= ground;
        attr->air_drift_max *= air;
        attr->air_max_horizontal_velocity *= air;
#if ROGUE_DEBUG || ROGUE_QA_EVENT_RELEASE
        if(player_effects && RoguePlay_NativeKit(run))
            OSReport("[rogue] native_kit_stats speed_rank=%u native_walk=%f event_walk=%f multiplier=%f\n",
                run->stacks[2],native_walk,attr->walk_max_vel,ground);
#endif
        if (combat_opponent) {
            if (run->current.payload.combat.tags & 1) attr->weight *= 1.25f;
            if (run->current.payload.combat.tags & (4 | 32)) attr->weight *= 0.85f;
        }
        if (player_effects) {
            float jump = 1.0f + run->stacks[4] * 0.025f;
            float landing = 1.0f - run->stacks[18] * 0.05f;
            attr->jump_v_initial_velocity *= jump;
            attr->hop_v_initial_velocity *= jump;
            attr->weight *= 1.0f + run->stacks[7] * 0.05f;
            attr->dash_accel_mul *= 1.0f + run->stacks[16] * 0.06f;
            attr->dash_accel_base *= 1.0f + run->stacks[16] * 0.06f;
            attr->terminal_velocity *= 1.0f - run->stacks[17] * 0.05f;
            attr->normal_landing_lag *= landing;
            attr->landingairn_lag *= landing;
            attr->landingairf_lag *= landing;
            attr->landingairb_lag *= landing;
            attr->landingairhi_lag *= landing;
            attr->landingairlw_lag *= landing;
        }
    }
    Rogue_AbilityFighterCreated(fighter);
}

float RogueFighter_AdjustDamage(Fighter* fighter, float damage)
{
    RogueRun* run = RogueDirector_Run();
    if (!RogueRuntime_IsActive() || run->phase != ROGUE_FIGHT || damage <= 0 || reflecting) return damage;
    if (fighter->player_idx == 0) {
        damage *= 1.0f - run->stacks[1] * 0.08f;
        if (fighter->dmg.x18c4_source_ply > 0 && fighter->dmg.x18c4_source_ply < 4) {
            Fighter_GObj* source = Player_GetEntity(fighter->dmg.x18c4_source_ply);
            damage *= run->current.payload.combat.damage * 0.01f;
            if (source && run->stacks[21]) {
                reflecting = 1;
                Fighter_TakeDamage_8006CC7C(source->user_data, damage * run->stacks[21] * 0.04f);
                reflecting = 0;
            }
        }
    } else {
        if (fighter->dmg.x18c4_source_ply == 0) {
            int stocks = Player_GetStocks(0);
            damage *= 1.0f + run->stacks[0] * 0.08f;
            if (fighter->motion_id >= ftCo_MS_ThrownF && fighter->motion_id <= ftCo_MS_ThrownlwWomen)
                damage *= 1.0f + run->stacks[8] * 0.10f;
            if (stocks != opening_stock) {
                damage *= 1.0f + run->stacks[23] * 0.15f;
                opening_stock = stocks;
            }
        }
        damage *= 100.0f / run->current.payload.combat.defense;
    }
    return damage;
}
