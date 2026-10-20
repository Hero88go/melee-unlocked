#include "run.h"
#include "balance.h"
#include "offers.h"
#include "score.h"
#include "reward_policy.h"
#include "camp_vendor.h"
#include "play_context.h"
#include "../upgrades/upgrade_registry.h"
#include "../encounters/encounter_registry.h"
#include <string.h>

const RogueEncounter* RogueNode_Combat(const RogueNodeDesc* node)
{
    static const RogueEncounter empty = { 0 };
    return node && node->kind <= ROGUE_BOSS ? &node->payload.combat : &empty;
}
static RogueRunObserver observer;
void RogueRun_SetObserver(RogueRunObserver value) { observer = value; }

static void history(RogueRun* run, unsigned kind, unsigned cost, unsigned a, unsigned b, unsigned c)
{
    RogueHistoryEvent* event = &run->history[run->history_count % ROGUE_HISTORY];
    memset(event,0,sizeof(*event));
    event->kind=kind; event->floor=run->floor; event->cost=cost;
    event->generation=run->history_count;
    event->after[0]=a; event->after[1]=b; event->after[2]=c;
    run->history_count++;
}

unsigned RogueEncounter_Budget(unsigned act, unsigned floor, unsigned tier)
{
    return 2 + tier * 4 + (act - 1) * 2 + (floor % 5) / 2;
}
unsigned RogueEncounter_Threat(const RogueEncounter* e)
{
    unsigned bits = e->tags >> 16, cost = 0;
    if (!e->recipe || e->recipe > ROGUE_RECIPES) return 0;
    while (bits) { cost += (bits & 1) * 2; bits >>= 1; }
    return rogue_recipes[e->recipe - 1].threat_cost + cost;
}
static int recipe_allowed(const RogueRun* run, unsigned tier, const RogueRecipe* r)
{
    return r->tier == tier && run->act >= r->min_act && run->act <= r->max_act &&
        run->floor >= r->min_floor && run->floor <= r->max_floor &&
        r->threat_cost <= RogueEncounter_Budget(run->act,run->floor,tier) &&
        !(r->tags & r->incompatible_tags);
}
void RogueEncounter_Generate(RogueRun* run, unsigned tier, RogueEncounter* out)
{
    unsigned pool[ROGUE_RECIPES], count = 0, i, j, pick, total = 0;
    const RogueRecipe* recipe;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < ROGUE_RECIPES; ++i) {
        int recent = 0;
        if (!recipe_allowed(run,tier,&rogue_recipes[i])) continue;
        for (j = 0; j < rogue_recipes[i].cooldown && j < 4; ++j)
            if (run->recent_recipes[j] == i + 1) recent = 1;
        if (!recent) pool[count++] = i;
    }
    if (!count) for (i = 0; i < ROGUE_RECIPES; ++i)
        if (recipe_allowed(run,tier,&rogue_recipes[i])) pool[count++] = i;
    if (!count) return; /* Invalid external request never reads an empty pool. */
    for (i = 0; i < count; ++i) total += rogue_recipes[pool[i]].weight;
    pick = RogueRng_Bounded(&run->encounter_rng, total);
    for (i = 0; i + 1 < count && pick >= rogue_recipes[pool[i]].weight; ++i)
        pick -= rogue_recipes[pool[i]].weight;
    out->recipe = pool[i] + 1; recipe = &rogue_recipes[pool[i]];
    out->enemy_count = recipe->enemies;
    out->tags = recipe->tags;
    out->damage = recipe->damage;
    out->defense = recipe->defense; out->speed = recipe->speed;
    out->scale = recipe->scale; out->starting_percent = recipe->percent;
    count = 0;
    for (i = 0; i < 6; ++i) if ((recipe->stage_mask & (1U << i)) && run->recent_stages[0] != i + 1)
        pool[count++] = i + 1;
    if (!count) for (i = 0; i < 6; ++i) if (recipe->stage_mask & (1U << i)) pool[count++] = i + 1;
    out->stage = pool[RogueRng_Bounded(&run->encounter_rng, count)];
    for (i = 0; i < out->enemy_count; ++i) {
        out->fighters[i] = RogueRng_Bounded(&run->encounter_rng, 26);
        if (recipe->roster[(run->act - 1 + i) % 3] != 255)
            out->fighters[i] = recipe->roster[(run->act - 1 + i) % 3];
        out->stocks[i] = recipe->stocks;
    }
    if (out->tags & 128U) out->fighters[0] = run->character;
    if (out->tags & 128U) {
        unsigned offense = run->stacks[0] + run->stacks[14] + run->stacks[23];
        unsigned mobility = run->stacks[2] + run->stacks[3] + run->stacks[4];
        /* Bounded counter-build policy uses purchased traits, never live inputs. */
        if (offense > mobility) out->defense += offense > 5 ? 15 : offense * 3;
        else out->speed += mobility > 5 ? 15 : mobility * 3;
    }
    if (out->tags & 1024U) out->stocks[0]++;
    /* Curated, single-use mutations cost two threat points each. The mask
       preserves weaknesses (no armor on fragile builds, no haste on tanks). */
    count = 0;
    for (i = 0; i < 3; ++i) if (recipe->mutation_mask & (1U << i)) pool[count++] = i;
    while (count && RogueEncounter_Threat(out) + 2 <= RogueEncounter_Budget(run->act,run->floor,tier)) {
        pick = RogueRng_Bounded(&run->encounter_rng,count);
        i = pool[pick]; pool[pick] = pool[--count];
        out->tags |= 1U << (16 + i);
        if (i == 0) out->damage += 5;
        if (i == 1) out->defense += 5;
        if (i == 2) out->speed += 5;
    }
    for (i = 3; i > 0; --i) run->recent_recipes[i] = run->recent_recipes[i - 1];
    run->recent_recipes[0] = out->recipe;
    for (i = 2; i > 0; --i) run->recent_stages[i] = run->recent_stages[i - 1];
    run->recent_stages[0] = out->stage;
    run->encounters_offered++;
    if (observer) observer(run, 1, out->recipe, out->tags, out->stage);
}

static void previews(RogueRun* run)
{
    unsigned i;
    for (i = 0; i < 2; ++i) {
        unsigned tier = run->route[run->floor][i];
        memset(&run->preview[i], 0, sizeof(run->preview[i]));
        run->preview[i].kind = tier;
        run->preview[i].node_id = 1 + run->floor * 2 + i;
        run->preview[i].ruleset_version = run->ruleset_version;
        if (tier <= ROGUE_BOSS) RogueEncounter_Generate(run, tier, &run->preview[i].payload.combat);
        else if (tier == ROGUE_NODE_EVENT)
            run->preview[i].payload.event = run->event_opportunities[run->act - 1].event;
    }
}

void RogueRun_Reward(RogueRun* run)
{
    unsigned fallback;
    if(run->reward_source.generated || run->reward_source.claimed) return;
    if(!RogueReward_Generate(run,&run->reward_rng,0,&run->reward,&fallback)) return;
    run->reward_source.generated=1;
    if(run->reward_source.kind!=ROGUE_REWARD_STARTER)
        history(run,8,0,fallback,run->reward_source.node_id,run->reward_source.receipt_id);
    run->phase = ROGUE_REWARD;
}
static unsigned current_node_id(const RogueRun* run)
{
    /* Direct developer service fixtures may not have committed a preview. */
    if(run->floor>=ROGUE_FLOORS) return run->current.node_id;
    return run->current.node_id ? run->current.node_id :
        1 + run->floor * 2 + run->selected_route[run->floor];
}
static void reward_source(RogueRun* run, unsigned kind, unsigned won)
{
    memset(&run->reward_source,0,sizeof(run->reward_source));
    run->reward_source.run_id=run->run_id;
    run->reward_source.kind=kind;
    run->reward_source.policy_version=run->reward_policy_version;
    run->reward_source.won=won;
    if(kind==ROGUE_REWARD_COMBAT || kind==ROGUE_REWARD_SKILL || kind==ROGUE_REWARD_GAUNTLET)
        run->reward_source.node_id=current_node_id(run);
    /* Receipt/verified donor fields are populated only by a verified result
     * capture. A configured roster alone is not defeated-donor evidence. */
}
static int camp_begin(RogueRun* run, unsigned kind)
{
    unsigned node_id=current_node_id(run);
    RogueOfferState old_general=run->camp.general;
    unsigned old_prices[3];
    memcpy(old_prices,run->camp.general_prices,sizeof(old_prices));
    if(run->camp.active && run->camp.node_id==node_id && run->camp.kind==kind)
        return 0; /* Reopening is a read of the same visit, never a refill. */
    memset(&run->camp,0,sizeof(run->camp));
    /* REST has no general service. Retain its inactive last-shop values for
     * legacy history/export; only a new SHOP replaces that inventory. */
    if(kind==ROGUE_NODE_REST) {
        run->camp.general=old_general;
        memcpy(run->camp.general_prices,old_prices,sizeof(old_prices));
    }
    run->camp.run_id=run->run_id; run->camp.node_id=node_id;
    run->camp.kind=kind; run->camp.active=1;
    RogueCamp_PrepareAerials(run);
    return 1;
}
void RogueRun_Shop(RogueRun* run)
{
    if(run->camp.active && run->camp.node_id==current_node_id(run) &&
       run->camp.kind!=ROGUE_NODE_SHOP) return;
    if(run->floor>=ROGUE_FLOORS || (run->camp.exit_committed &&
        run->camp.node_id==current_node_id(run))) return;
    if(camp_begin(run,ROGUE_NODE_SHOP)) {
        RogueCamp_GeneralOffers(run, &run->camp.general, &run->shop_rng, 0);
        RogueCamp_Prices(run);
    }
    run->phase = ROGUE_SHOP;
}
void RogueRun_Init(RogueRun* run, RogueSeed seed, unsigned character)
{
    unsigned i, j;
    memset(run, 0, sizeof(*run));
    run->version = ROGUE_SNAPSHOT_VERSION; run->ruleset_version = 1;
    run->run_id=1; /* Deterministic fixture identity; director allocates live IDs. */
    run->catalog_version=ROGUE_CATALOG_VERSION;
    run->score_policy_version=ROGUE_SCORE_POLICY_VERSION;
    run->event_policy_version=ROGUE_EVENT_POLICY_VERSION;
    run->reward_policy_version=ROGUE_REWARD_POLICY_VERSION;
    reward_source(run,ROGUE_REWARD_STARTER,0);
    RogueRng_Derive(&run->proc_rng, seed, ROGUE_STREAM_PROC, 1, 0); run->seed = seed; run->character = character % 26;
    run->starting_stocks = 3; run->difficulty = 2;
    run->act = 1; run->gold = ROGUE_STARTING_GOLD;
    RogueRng_Init(&run->route_rng, seed, 0);
    RogueRng_Init(&run->encounter_rng, seed, 1);
    RogueRng_Init(&run->reward_rng, seed, 2);
    RogueRng_Init(&run->shop_rng, seed, 3);
    for (i = 0; i < ROGUE_FLOORS; ++i) for (j = 0; j < 2; ++j) {
        unsigned roll = RogueRng_Bounded(&run->route_rng, 10);
        run->route[i][j] = i % 5 == 4 ? ROGUE_BOSS : roll < 5 ? ROGUE_NORMAL : roll < 8 ? ROGUE_ELITE : roll == 8 ? ROGUE_NODE_SHOP : ROGUE_NODE_REST;
    }
    /* First floor always supports a combat introduction. */
    run->route[0][0] = run->route[0][1] = ROGUE_NORMAL;
    previews(run); RogueRun_Reward(run);
}
int RogueRun_SetAttemptId(RogueRun* run, RogueSeed run_id)
{
    if(!run || !run_id || run->floor || run->history_count || run->legacy_identity ||
        run->phase!=ROGUE_REWARD || run->reward.claimed || run->camp.active) return 0;
    run->run_id=run_id; run->reward_source.run_id=run_id;
    return 1;
}
int RogueRun_PlanEvents(RogueRun* run, const RogueEventAvailability available[7],
                        const unsigned combat_stages[6])
{
    RogueEventOpportunity plan[3];
    unsigned i;
    if (!run || run->events_planned || run->floor || run->history_count ||
        run->phase != ROGUE_REWARD || run->reward.claimed) return 0;
    if (!RogueEventRoute_Build(run->seed, run->ruleset_version, run->character,
                             run->route, available, combat_stages, plan)) return 0;
    memcpy(run->event_opportunities, plan, sizeof(plan));
    for (i = 0; i < 3; ++i) if (plan[i].present) {
        run->route[plan[i].floor][plan[i].side] = ROGUE_NODE_EVENT;
        run->route[plan[i].floor][1 - plan[i].side] = plan[i].alternative_kind;
    }
    run->events_planned = 1;
    return 1; /* Floor one remains the already-frozen combat introduction. */
}

int RogueRun_Configure(RogueRun* run, unsigned stocks, unsigned difficulty, unsigned costume)
{
    if (!run || run->floor || run->phase != ROGUE_REWARD ||
        run->reward.claimed || run->history_count || stocks < 1 || stocks > 5 ||
        difficulty > 4 || costume > 5) return 0;
    run->starting_stocks = stocks;
    run->difficulty = difficulty;
    run->costume = costume;
    return 1;
}

unsigned RogueRun_CpuLevel(const RogueRun* run)
{
    /* Normal retains the baseline 5/7/9 CPU progression. Adjacent native
     * difficulty selections adjust one CPU level, bounded by engine limits. */
    unsigned level = 1 + run->act * 2 + run->difficulty;
    return level > 9 ? 9 : level;
}

int RogueRun_ChooseUpgrade(RogueRun* run, unsigned slot)
{
    unsigned id;
    if (run->phase != ROGUE_REWARD || slot >= 3 || run->reward.claimed) return 0;
    id = run->reward.ids[slot];
    if (!RogueOffer_Apply(run, id)) return 0;
    /* Exhausted pools can have repeated gold cards; retain the clicked slot. */
    run->history[(run->history_count-1)%ROGUE_HISTORY].after[1]=slot;
    return 1;
}
int RogueRun_SelectEventKit(RogueRun* run, unsigned side, unsigned native_moves)
{
    RogueNodeDesc* node;
    RogueEventOpportunity* op;
    if(!run || run->phase!=ROGUE_ROUTE || side>1 || native_moves>1 ||
        run->floor>=ROGUE_FLOORS || !run->events_planned) return 0;
    node=&run->preview[side];op=&run->event_opportunities[run->floor/5];
    if(node->kind!=ROGUE_NODE_EVENT || !RogueEvent_HasNativeKit(node->payload.event.template_id) ||
        !op->present || op->floor!=run->floor || op->side!=side ||
        node->node_id!=op->event.node_id || run->receipts[run->floor].committed ||
        memcmp(&node->payload.event,&op->event,sizeof(op->event))) return 0;
    /* No route/reward RNG or equipment changes. Freeze selection at entry. */
    node->payload.event.native_kit_assist=native_moves;
    op->event.native_kit_assist=native_moves;
    return 1;
}

int RogueRun_ChooseRoute(RogueRun* run, unsigned slot)
{
    unsigned type;
    if (run->phase != ROGUE_ROUTE || slot >= 2 || run->floor >= ROGUE_FLOORS) return 0;
    if(run->preview[slot].kind==ROGUE_NODE_EVENT &&
       RoguePlay_EventKit(run,&run->preview[slot].payload.event)==ROGUE_EVENT_KIT_INVALID) return 0;
    run->selected_route[run->floor] = slot;
    run->current = run->preview[slot];
    if (run->current.kind == ROGUE_NODE_EVENT)
        history(run,4,0,run->current.payload.event.template_id,ROGUE_NODE_EVENT,run->current.node_id);
    else history(run,4,0,run->current.payload.combat.recipe,run->current.payload.combat.tags,run->current.payload.combat.stage);
    type = run->route[run->floor][slot];
    if (type == ROGUE_NODE_SHOP) RogueRun_Shop(run);
    else {
        if(type==ROGUE_NODE_REST) camp_begin(run,ROGUE_NODE_REST);
        run->phase = type == ROGUE_NODE_EVENT ? ROGUE_EVENT : type == ROGUE_NODE_REST ? ROGUE_REST : ROGUE_FIGHT;
    }
    return 1;
}
static void advance(RogueRun* run)
{
    run->floor++;
    if (run->floor >= ROGUE_FLOORS) { run->phase = ROGUE_COMPLETE; return; }
    run->act = run->floor / 5 + 1;
    previews(run); run->phase=ROGUE_ROUTE;
}
unsigned RogueRun_Resolve(RogueRun* run, const RogueNodeResult* result)
{
    RogueScoreReceipt receipt;
    RogueScoreModifiers modifiers;
    RogueScoreTotals totals;
    RoguePoints score, rogue_added;
    unsigned floor, won, event_node;
    if(!run || !result || result->run_id!=run->run_id || !run->run_id ||
        !result->node_id || result->node_id>30) return ROGUE_SETTLEMENT_INVALID;
    floor=(result->node_id-1)/2;
    if(run->settled_nodes & (1U<<(result->node_id-1))) {
        const RogueScoreReceipt* saved=&run->receipts[floor];
        if(!saved->committed || !RogueSettlement_Calculate(result,&saved->modifiers,&receipt))
            return ROGUE_SETTLEMENT_INVALID;
        receipt.committed=1;
        return !memcmp(saved,&receipt,sizeof(receipt)) ? ROGUE_SETTLEMENT_ALREADY_COMMITTED : ROGUE_SETTLEMENT_INVALID;
    }
    if(run->legacy_active_incompatible || run->score_policy_version!=ROGUE_SCORE_RULES_VERSION ||
        run->floor!=floor || run->current.node_id!=result->node_id || run->receipts[floor].committed ||
        run->receipt_count>=ROGUE_FLOORS || run->nodes_resolved>=ROGUE_FLOORS) return ROGUE_SETTLEMENT_INVALID;
    event_node=run->current.kind==ROGUE_NODE_EVENT;
    if(result->race.present && (!event_node ||
        run->current.payload.event.template_id!=ROGUE_EVENT_RACE ||
        result->race.base_character!=run->character ||
        result->race.native_stage!=run->current.payload.event.native_stage ||
        result->race.limit_frames!=run->current.payload.event.timer_frames ||
        result->race.native_kit_assist!=run->current.payload.event.native_kit_assist))
        return ROGUE_SETTLEMENT_INVALID;
    if(result->targets.present && (!event_node ||
        run->current.payload.event.template_id!=ROGUE_EVENT_TARGETS ||
        result->targets.base_character!=run->character ||
        result->targets.native_stage!=run->current.payload.event.native_stage ||
        result->targets.native_kit_assist!=run->current.payload.event.native_kit_assist))
        return ROGUE_SETTLEMENT_INVALID;
    if(result->event_end && !event_node) return ROGUE_SETTLEMENT_INVALID;
    if(event_node) {
        unsigned kind=run->current.payload.event.template_id==ROGUE_EVENT_GAUNTLET ? ROGUE_RESULT_GAUNTLET : ROGUE_RESULT_SKILL;
        if(run->phase!=ROGUE_EVENT || result->kind!=kind || run->events_attempted>=3) return ROGUE_SETTLEMENT_INVALID;
        if((result->home_run.present || (result->event_end && result->kind==ROGUE_RESULT_SKILL)) && ((result->home_run.present && run->current.payload.event.template_id!=ROGUE_EVENT_HOME_RUN) ||
            result->carried_percent!=run->carried_percent)) return ROGUE_SETTLEMENT_INVALID;
    } else if(run->phase!=ROGUE_FIGHT || run->current.kind>ROGUE_BOSS || result->kind!=run->current.kind)
        return ROGUE_SETTLEMENT_INVALID;
    modifiers.showmanship_rank=run->stacks[RogueUpgrade_StackIndex(ROGUE_UPGRADE_SCORE)];
    modifiers.bounty_rank=run->stacks[RogueUpgrade_StackIndex(ROGUE_UPGRADE_GOLD)];
    modifiers.patron_rank=0; /* Set from the authored handler when WP10 installs Patron. */
    if(!RogueSettlement_Calculate(result,&modifiers,&receipt) || run->gold>ROGUE_WALLET_LIMIT ||
        receipt.total_gold>ROGUE_WALLET_LIMIT-run->gold ||
        !RoguePoints_Add(run->score,receipt.run_score_delta,&score)) return ROGUE_SETTLEMENT_INVALID;
    totals=run->score_totals;
    if(!RoguePoints_Add(totals.native_raw,result->native_stage_delta,&totals.native_raw) ||
        !RoguePoints_Add(totals.native_counted,receipt.native_counted_delta,&totals.native_counted) ||
        !RoguePoints_Add(totals.objective,result->objective_points,&totals.objective) ||
        !RoguePoints_Add(receipt.rogue_clear_points,result->upgrade_style_points,&rogue_added) ||
        !RoguePoints_Add(rogue_added,receipt.showmanship_bonus,&rogue_added) ||
        !RoguePoints_Add(totals.rogue_added,rogue_added,&totals.rogue_added) ||
        totals.clear_gold>~0U-receipt.base_gold || totals.performance_gold>~0U-receipt.performance_gold ||
        totals.effect_gold>~0U-receipt.effect_gold || run->submatches_played>21-result->submatch_count)
        return ROGUE_SETTLEMENT_INVALID;
    totals.clear_gold+=receipt.base_gold; totals.performance_gold+=receipt.performance_gold;
    totals.effect_gold+=receipt.effect_gold;
    /* No fallible operations after this point. One receipt owns score, wallet,
     * provenance and the resolved marker before any next preview is generated. */
    receipt.committed=1; run->receipts[floor]=receipt; run->receipt_count++;
    run->settled_nodes|=1U<<(result->node_id-1);
    run->score_totals=totals; run->score=score; run->gold+=receipt.total_gold;
    run->native_score=result->native_stage_delta; run->carried_percent=result->carried_percent;
    run->nodes_resolved++; run->submatches_played+=result->submatch_count;
    won=result->outcome==ROGUE_RESULT_WIN;
    if(event_node) { run->events_attempted++; if(won) run->events_completed++; }
    if(won && result->kind!=ROGUE_RESULT_SKILL) run->combat_nodes_won++;
    reward_source(run,result->kind==ROGUE_RESULT_SKILL ? ROGUE_REWARD_SKILL :
        result->kind==ROGUE_RESULT_GAUNTLET ? ROGUE_REWARD_GAUNTLET : ROGUE_REWARD_COMBAT,won);
    run->reward_source.receipt_id=floor+1;
    run->reward_source.provenance_verified=result->provenance_verified;
    run->reward_source.donor_count=result->donor_count;
    memcpy(run->reward_source.donors,receipt.result.donors,sizeof(run->reward_source.donors));
    history(run,5,0,won,(unsigned)result->native_stage_delta,run->carried_percent);
    if(result->outcome==ROGUE_RESULT_QUIT || (!won && result->kind!=ROGUE_RESULT_SKILL)) {
        run->death_reason=result->outcome==ROGUE_RESULT_QUIT ? 2 : 1; run->phase=ROGUE_DEAD;
    } else {
        advance(run);
        if(run->phase==ROGUE_ROUTE) RogueRun_Reward(run);
    }
    return ROGUE_SETTLEMENT_COMMITTED;
}
int RogueRun_MatchEnd(RogueRun* run, int won, unsigned native_score)
{
    RogueNodeResult result;
    if(!run || run->phase!=ROGUE_FIGHT) return 0;
    memset(&result,0,sizeof(result));
    result.run_id=run->run_id; result.node_id=run->current.node_id;
    result.kind=run->current.kind; result.submatch_count=1;
    result.outcome=won ? ROGUE_RESULT_WIN : ROGUE_RESULT_FAILURE;
    result.measured=1; result.native_output=native_score;
    result.native_stage_delta=native_score; result.native_display_output=native_score;
    result.carried_percent=run->carried_percent;
    result.row_count=1; result.rows[0].occurrences=1;
    result.rows[0].source=ROGUE_SOURCE_STYLE; result.rows[0].points=native_score;
    /* Compatibility entry for measured host fixtures. Native adapters submit
     * complete captured results directly to Resolve, never this shortcut. */
    return RogueRun_Resolve(run,&result)==ROGUE_SETTLEMENT_COMMITTED;
}
int RogueRun_HealCamp(RogueRun* run)
{
    unsigned recovery;
    /* The visit owns healing, including calls from the native room. An old
     * visual marker or a repeated confirm cannot heal a different visit. */
    if(!RogueCamp_Active(run) || run->phase!=ROGUE_REST || run->camp.heal_used) return 0;
    recovery=20+10*run->stacks[13];
    run->carried_percent=run->carried_percent>recovery ? run->carried_percent-recovery : 0;
    history(run,7,0,recovery,run->carried_percent,0);
    run->camp.heal_used=1;
    return 1;
}
int RogueRun_ExitCamp(RogueRun* run)
{
    if(!RogueCamp_Active(run)) return 0;
    run->nodes_resolved++;
    run->camp.exit_committed=1; run->camp.active=0;
    reward_source(run,ROGUE_REWARD_NONE,0);
    advance(run);
    memset(&run->reward,0,sizeof(run->reward));
    run->reward.claimed=1;
    return 1;
}
int RogueRun_LeaveService(RogueRun* run)
{
    /* Compatibility for old synthetic fixtures. Physical room interactions
     * call HealCamp and ExitCamp separately, never grant healing on exit. */
    if (run->phase != ROGUE_SHOP && run->phase != ROGUE_REST) return 0;
    if(run->camp.exit_committed && run->camp.node_id==current_node_id(run)) return 0;
    if(!run->camp.active) camp_begin(run,run->phase==ROGUE_SHOP ? ROGUE_NODE_SHOP : ROGUE_NODE_REST);
    if(!RogueCamp_Active(run)) return 0;
    if(run->phase==ROGUE_REST) RogueRun_HealCamp(run);
    return RogueRun_ExitCamp(run);
}
unsigned RogueRun_RerollCost(const RogueRun* run, int shop)
{
    unsigned uses = shop ? run->camp.general.rerolls : run->reward.rerolls;
    unsigned base = shop ? ROGUE_SHOP_REROLL_BASE : ROGUE_UPGRADE_REROLL_BASE;
    unsigned step = shop ? ROGUE_SHOP_REROLL_STEP : ROGUE_UPGRADE_REROLL_STEP;
    if (uses > (0xffffffffU - base) / step) return 0xffffffffU;
    return base + step * uses;
}
int RogueRun_Reroll(RogueRun* run, int shop)
{
    RogueOfferState* state = shop ? &run->camp.general : &run->reward;
    RogueHistoryEvent* event;
    unsigned cost = RogueRun_RerollCost(run, shop), i, available = 0;
    if (run->phase != (unsigned) (shop ? ROGUE_SHOP : ROGUE_REWARD) || state->claimed || run->gold < cost) return 0;
    if(!shop && !RogueReward_Validate(run,state)) return 0;
    if(shop && !RogueCamp_Active(run)) return 0;
    for (i = 0; i < 3; ++i) if (!state->sold[i]) available++;
    if (!available) return 0;
    event = &run->history[run->history_count % ROGUE_HISTORY];
    memset(event, 0, sizeof(*event));
    event->kind = shop ? 2 : 1; event->floor = run->floor; event->cost = cost;
    memcpy(event->before, state->ids, sizeof(event->before));
    if(shop) RogueCamp_GeneralOffers(run,state,&run->shop_rng,1);
    else if(!RogueReward_Generate(run,&run->reward_rng,state,state,0)) return 0;
    run->gold -= cost; run->gold_spent += cost; run->rerolls_used++;
    run->reroll_gold_spent += cost;
    state->rerolls++; state->generation++;
    if(shop) RogueCamp_Prices(run);
    event->generation = state->generation;
    memcpy(event->after, state->ids, sizeof(event->after));
    run->history_count++;
    if (observer) observer(run, 2, shop != 0, cost, state->generation);
    return 1;
}
int RogueRun_Buy(RogueRun* run, unsigned slot)
{
    return RogueOffer_BuyGeneral(run,slot);
}
