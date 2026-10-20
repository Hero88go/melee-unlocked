#include "run.h"
#include "offers.h"
#include "reward_policy.h"
#include "camp_vendor.h"
#include "offer_identity.h"
#include "event_metrics.h"
#include "../upgrades/upgrade_registry.h"
#include <string.h>

typedef struct Codec { unsigned char* out; const unsigned char* in; unsigned pos, size, ok; } Codec;
static void word(Codec* c, unsigned* value)
{
    unsigned i, v = *value;
    if (c->pos + 4 > c->size) { c->ok = 0; return; }
    if (c->out) for (i = 0; i < 4; ++i) c->out[c->pos + i] = (unsigned char) (v >> (24 - 8 * i));
    else { v = 0; for (i = 0; i < 4; ++i) v = (v << 8) | c->in[c->pos + i]; *value = v; }
    c->pos += 4;
}
static void array(Codec* c, unsigned* p, unsigned count)
{
    unsigned i; for (i = 0; i < count; ++i) word(c, &p[i]);
}
static void wide(Codec* c, RogueSeed* value)
{
    unsigned hi=(unsigned)(*value>>32), lo=(unsigned)*value;
    word(c,&hi); word(c,&lo);
    if(c->in) *value=((RogueSeed)hi<<32)|lo;
}
static void points(Codec* c, RoguePoints* value)
{
    RogueSeed bits=(RogueSeed)*value;
    wide(c,&bits);
    if(c->in) *value=(bits>>63) ? -1-(RoguePoints)(~bits) : (RoguePoints)bits;
}
static void legacy_points(Codec* c, RoguePoints* value, unsigned version)
{
    unsigned v=0;
    if(c->out) {
        if(*value<0 || *value>0xffffffffULL) { if(version<9) c->ok=0; }
        else v=(unsigned)*value;
    }
    word(c,&v);
    if(c->in) *value=v;
}
static void offer_id(Codec* c, unsigned* id, unsigned version)
{
    unsigned wire = *id;
    if (c->out) {
        RogueOfferRef ref = RogueOffer_Decode(*id);
        if (!RogueOfferId_Valid(*id)) { c->ok=0; return; }
        wire = version < 7 ? RogueOffer_ToLegacyV4(ref) : RogueOffer_Encode(ref);
        /* Never silently discard a new identity when exporting an old schema. */
        if (*id && !wire) { c->ok=0; return; }
    }
    word(c,&wire);
    if (c->in) {
        if (version < 7) {
            if (wire > 258) { c->ok=0; return; }
            *id=RogueOffer_Encode(RogueOffer_FromLegacyV4(wire));
        } else {
            if ((wire && !(wire >> 24)) || !RogueOfferId_Valid(wire)) c->ok=0;
            *id=wire;
        }
    }
}
static void offer(Codec* c, RogueOfferState* s, unsigned version)
{
    unsigned i;
    for(i=0;i<3;++i) offer_id(c,&s->ids[i],version);
    array(c,s->sold,3);
    word(c,&s->generation); word(c,&s->rerolls); word(c,&s->claimed);
}
static void history_values(Codec* c, RogueHistoryEvent* e, unsigned version)
{
    unsigned j;
    for(j=0;j<3;++j) {
        if(e->kind==1 || e->kind==2 || e->kind==11) offer_id(c,&e->before[j],version);
        else word(c,&e->before[j]);
    }
    for(j=0;j<3;++j) {
        if(e->kind==1 || e->kind==2 || e->kind==11 || ((e->kind==3 || e->kind==6 || e->kind==10) && !j))
            offer_id(c,&e->after[j],version);
        else word(c,&e->after[j]);
    }
}
static void encounter(Codec* c, RogueEncounter* e)
{
    word(c,&e->recipe); word(c,&e->stage); word(c,&e->enemy_count);
    array(c,e->fighters,3); array(c,e->stocks,3);
    word(c,&e->tags); word(c,&e->damage); word(c,&e->defense);
    word(c,&e->speed); word(c,&e->scale); word(c,&e->starting_percent);
}
static void event(Codec* c, RogueEventDesc* e)
{
    word(c,&e->template_id); word(c,&e->node_id); word(c,&e->ruleset_version);
    word(c,&e->base_character); word(c,&e->native_stage); word(c,&e->course_id);
    word(c,&e->timer_frames); word(c,&e->dangerous); word(c,&e->native_kit_assist);
    array(c,e->opponents,3); array(c,e->combat_stages,3);
}
static void node_metadata(Codec* c, RogueNodeDesc* n)
{
    RogueEventDesc e;
    memset(&e,0,sizeof(e));
    word(c,&n->kind); word(c,&n->node_id); word(c,&n->ruleset_version);
    if (c->out && n->kind == ROGUE_NODE_EVENT) e = n->payload.event;
    event(c,&e);
    if (c->in && n->kind == ROGUE_NODE_EVENT) n->payload.event = e;
}
static void legacy_node_metadata(RogueRun* r)
{
    unsigned i, floor = r->floor < 15 ? r->floor : 14;
    for (i=0;i<2;++i) {
        r->preview[i].kind=r->route[floor][i];
        r->preview[i].node_id=1+floor*2+i;
        r->preview[i].ruleset_version=r->ruleset_version;
    }
    if (r->current.payload.combat.recipe || r->floor || r->phase==ROGUE_SHOP || r->phase==ROGUE_REST) {
        floor=r->floor;
        if (floor && r->phase!=ROGUE_FIGHT && r->phase!=ROGUE_SHOP &&
            r->phase!=ROGUE_REST && r->phase!=ROGUE_DEAD) --floor;
        if (floor>=15) floor=14;
        i=r->selected_route[floor];
        if (i>1) return;
        r->current.kind=r->route[floor][i];
        r->current.node_id=1+floor*2+i;
        r->current.ruleset_version=r->ruleset_version;
    }
}
static void transaction_metadata(Codec* c, RogueRun* r)
{
    RogueRewardSource* s=&r->reward_source;
    RogueCampVisitState* v=&r->camp;
    unsigned i;
    wide(c,&r->run_id);
    word(c,&r->catalog_version); word(c,&r->score_policy_version);
    word(c,&r->event_policy_version); word(c,&r->reward_policy_version);
    word(c,&r->legacy_identity);
    wide(c,&s->run_id); word(c,&s->kind); word(c,&s->node_id);
    word(c,&s->receipt_id); word(c,&s->policy_version); word(c,&s->won);
    word(c,&s->provenance_verified); word(c,&s->donor_count);
    array(c,s->donors,ROGUE_REWARD_DONORS);
    if(r->version>=10) { word(c,&s->generated); word(c,&s->claimed); }
    wide(c,&v->run_id); word(c,&v->node_id); word(c,&v->kind);
    word(c,&v->active); word(c,&v->heal_used); word(c,&v->exit_committed);
    /* General inventory already occupies its frozen prefix position. */
    array(c,v->general_prices,3);
    for(i=0;i<ROGUE_AERIAL_SLOTS;++i) {
        offer(c,&v->aerial[i],r->version); array(c,v->aerial_prices[i],3);
    }
    word(c,&v->pending_special_slots); word(c,&v->pending_aerial_slots);
}
static void extra_stacks(Codec* c, RogueRun* r)
{
    unsigned count=ROGUE_UPGRADES-24, i;
    unsigned char seen[ROGUE_UPGRADES];
    memset(seen,0,sizeof(seen));
    word(c,&count);
    if(count>ROGUE_UPGRADES-24) { c->ok=0; return; }
    for(i=0;i<count;++i) {
        unsigned id=RogueUpgrade_StackId(i+24), rank=r->stacks[i+24], index;
        word(c,&id); word(c,&rank);
        index=RogueUpgrade_StackIndex(id);
        if(index<24 || index>=ROGUE_UPGRADES || seen[index]) { c->ok=0; return; }
        seen[index]=1;
        if(c->in) r->stacks[index]=rank;
    }
}
static void receipt_codec(Codec* c, RogueScoreReceipt* p)
{
    RogueNodeResult* r=&p->result;
    unsigned i;
    word(c,&p->committed);
    if(!p->committed) return;
    wide(c,&r->run_id);
#define RW(name) word(c,&r->name)
    RW(node_id); RW(submatch_id); RW(submatch_count); RW(kind); RW(outcome); RW(measured); RW(debug_forced);
    RW(native_format); RW(display_capped); RW(row_count);
    if(r->row_count>ROGUE_NATIVE_ROWS) { c->ok=0; return; }
    points(c,&r->native_input); points(c,&r->native_output); points(c,&r->native_stage_delta);
    points(c,&r->native_display_output); points(c,&r->objective_points); points(c,&r->upgrade_style_points);
    RW(effect_gold); RW(carried_percent); RW(provenance_verified); RW(donor_count); array(c,r->donors,3);
#undef RW
    for(i=0;i<r->row_count;++i) {
        RogueNativeRow* row=&r->rows[i];
        word(c,&row->native_id); word(c,&row->occurrences); word(c,&row->source); word(c,&row->exclusion);
        points(c,&row->points);
    }
    word(c,&p->modifiers.showmanship_rank); word(c,&p->modifiers.bounty_rank); word(c,&p->modifiers.patron_rank);
    word(c,&p->policy_version);
#define PW(name) points(c,&p->name)
    PW(native_counted_delta); PW(eligible_native_points); PW(rogue_clear_points); PW(showmanship_bonus);
    PW(run_score_delta); PW(performance_basis); PW(boosted_performance); PW(uncapped_performance_gold);
    PW(adjusted_performance_gold);
#undef PW
    word(c,&p->performance_cap); word(c,&p->effect_cap); word(c,&p->base_gold);
    word(c,&p->performance_gold); word(c,&p->effect_gold); word(c,&p->total_gold);
}
static void ledger_codec(Codec* c, RogueRun* r)
{
    unsigned i;
    points(c,&r->score); points(c,&r->native_score);
    points(c,&r->legacy_score_base);
    points(c,&r->score_totals.native_raw); points(c,&r->score_totals.native_counted);
    points(c,&r->score_totals.objective); points(c,&r->score_totals.rogue_added);
    word(c,&r->score_totals.clear_gold); word(c,&r->score_totals.performance_gold); word(c,&r->score_totals.effect_gold);
    word(c,&r->receipt_count); word(c,&r->settled_nodes);
    for(i=0;i<ROGUE_FLOORS;++i) receipt_codec(c,&r->receipts[i]);
}
static void home_run_codec(Codec* c, RogueRun* r)
{
    unsigned i;
    for(i=0;i<ROGUE_FLOORS;++i) {
        RogueHomeRunMetric* m=&r->receipts[i].result.home_run;
        word(c,&m->present); word(c,&m->conversion_version); word(c,&m->native_mode);
        word(c,&m->display_unit); word(c,&m->display_value);
        points(c,&m->raw_centimeters); points(c,&m->distance_mm);
    }
}
static void target_codec(Codec* c, RogueRun* r)
{
    unsigned i;
    for(i=0;i<ROGUE_FLOORS;++i) {
        RogueTargetMetric* m=&r->receipts[i].result.targets;
        word(c,&m->present);word(c,&m->base_character);word(c,&m->native_stage);
        word(c,&m->total);word(c,&m->remaining);word(c,&m->remaining_frames);
        word(c,&m->native_goal);word(c,&m->native_outcome);word(c,&m->native_kit_assist);
    }
}
static void race_codec(Codec* c, RogueRun* r)
{
    unsigned i;
    for(i=0;i<ROGUE_FLOORS;++i) {
        RogueNodeResult* v=&r->receipts[i].result;
        word(c,&v->race.present);word(c,&v->race.base_character);word(c,&v->race.native_stage);
        word(c,&v->race.native_kit_assist);word(c,&v->race.limit_frames);word(c,&v->race.remaining_frames);
        word(c,&v->race.checkpoints);word(c,&v->race.native_goal);word(c,&v->race.native_outcome);
        word(c,&v->race.native_goal_score);points(c,&v->race.checkpoint_points);
        points(c,&v->race.goal_points);points(c,&v->race.time_points);
    }
}
static void choices_codec(Codec* c, RogueRun* r)
{
    unsigned i,count=0,id,value,index;
    if(c->out) for(i=0;i<ROGUE_UPGRADES;++i) if(r->upgrade_choices[i]) count++;
    word(c,&count);
    if(count>ROGUE_UPGRADES) { c->ok=0; return; }
    for(i=0;i<(c->out ? ROGUE_UPGRADES : count);++i) {
        if(c->out && !r->upgrade_choices[i]) continue;
        id=c->out ? RogueUpgrade_StackId(i) : 0;
        value=c->out ? r->upgrade_choices[i] : 0;
        word(c,&id); word(c,&value);
        index=RogueUpgrade_StackIndex(id);
        if(index>=ROGUE_UPGRADES || !value || value>4) { c->ok=0; return; }
        if(c->in) {
            if(r->upgrade_choices[index]) { c->ok=0; return; }
            r->upgrade_choices[index]=value;
        }
    }
}
static void legacy_transaction_metadata(RogueRun* r)
{
    r->catalog_version=ROGUE_CATALOG_VERSION;
    r->score_policy_version=ROGUE_SCORE_POLICY_VERSION;
    r->event_policy_version=ROGUE_EVENT_POLICY_VERSION;
    r->reward_policy_version=0;
    r->legacy_identity=1; r->legacy_score_unverified=1;
    r->reward_source.kind=ROGUE_REWARD_LEGACY;
    r->reward_source.policy_version=r->reward_policy_version;
    /* No attempt ID, receipt or defeated donors existed: do not invent them. */
    if(r->phase==ROGUE_SHOP || r->phase==ROGUE_REST) {
        unsigned i;
        r->camp.node_id=r->current.node_id;
        r->camp.kind=r->phase==ROGUE_SHOP ? ROGUE_NODE_SHOP : ROGUE_NODE_REST;
        r->camp.active=1;
        for(i=0;i<3;++i) r->camp.general_prices[i]=RogueOffer_Price(r,r->camp.general.ids[i]);
    }
}
static void codec(Codec* c, RogueRun* r)
{
    unsigned i, high = (unsigned) (r->seed >> 32), low = (unsigned) r->seed;
#define FIELD(name) word(c, &r->name)
    FIELD(version); word(c,&high); word(c,&low);
    if (c->in) r->seed = ((RogueSeed) high << 32) | low;
    FIELD(character); FIELD(act); FIELD(floor);
    FIELD(phase); FIELD(gold); legacy_points(c,&r->score,r->version);
    for (i=0;i<ROGUE_FLOORS;++i) array(c,r->route[i],2);
    array(c,r->selected_route,ROGUE_FLOORS); array(c,r->stacks,24) /* Frozen v4/v5 prefix */;
    array(c,r->specials,4); array(c,r->aerials,ROGUE_AERIAL_SLOTS); array(c,r->recent_recipes,4); array(c,r->recent_stages,3);
    FIELD(encounters_offered); FIELD(gold_spent); FIELD(rerolls_used); FIELD(history_count); FIELD(carried_percent);
    FIELD(combat_nodes_won); FIELD(death_reason); legacy_points(c,&r->native_score,r->version); FIELD(reroll_gold_spent);
    FIELD(route_rng.state); FIELD(route_rng.draws); FIELD(encounter_rng.state); FIELD(encounter_rng.draws);
    FIELD(reward_rng.state); FIELD(reward_rng.draws); FIELD(shop_rng.state); FIELD(shop_rng.draws);
    offer(c,&r->reward,r->version); offer(c,&r->camp.general,r->version);
    encounter(c,&r->preview[0].payload.combat); encounter(c,&r->preview[1].payload.combat); encounter(c,&r->current.payload.combat);
    for (i=0;i<ROGUE_HISTORY;++i) {
        FIELD(history[i].kind); FIELD(history[i].floor); FIELD(history[i].cost); FIELD(history[i].generation);
        history_values(c,&r->history[i],r->version);
    }
    if (r->version >= 5) {
        FIELD(starting_stocks); FIELD(difficulty); FIELD(costume);
    } else if (c->in) {
        /* v4 ignored CSS settings and always spawned three stocks. */
        r->starting_stocks = 3; r->difficulty = 2; r->costume = 0;
    }
    if (r->version >= 6) {
        FIELD(ruleset_version); FIELD(events_planned);
        FIELD(nodes_resolved); FIELD(events_attempted); FIELD(events_completed); FIELD(submatches_played);
        FIELD(legacy_score_unverified); FIELD(legacy_active_incompatible);
        FIELD(proc_rng.state); FIELD(proc_rng.draws);
        node_metadata(c,&r->preview[0]); node_metadata(c,&r->preview[1]); node_metadata(c,&r->current);
        for (i=0;i<3;++i) {
            RogueEventOpportunity* op=&r->event_opportunities[i];
            word(c,&op->present); word(c,&op->floor); word(c,&op->side); word(c,&op->alternative_kind);
            event(c,&op->event);
        }
    } else if (c->in) {
        r->ruleset_version=1;
        r->legacy_score_unverified=1;
        r->legacy_active_incompatible=r->phase==ROGUE_FIGHT;
        RogueRng_Derive(&r->proc_rng,r->seed,ROGUE_STREAM_PROC,1,0);
        legacy_node_metadata(r);
    }
    if(r->version>=8) { transaction_metadata(c,r); extra_stacks(c,r); }
    else if(c->in) legacy_transaction_metadata(r);
    else for(i=24;i<ROGUE_UPGRADES;++i) if(r->stacks[i]) c->ok=0;
    if(r->version>=9) ledger_codec(c,r);
    else if(c->in) {
        r->score_policy_version=ROGUE_SCORE_POLICY_VERSION;
        r->legacy_score_unverified=1;
        r->legacy_score_base=r->score;
        if(r->phase==ROGUE_FIGHT || r->phase==ROGUE_EVENT) r->legacy_active_incompatible=1;
        memset(&r->reward_source,0,sizeof(r->reward_source));
        r->reward_source.run_id=r->run_id; r->reward_source.kind=ROGUE_REWARD_LEGACY;
        r->reward_source.policy_version=r->reward_policy_version;
    }
    if(r->version>=12) choices_codec(c,r);
    else if(c->out) for(i=0;i<ROGUE_UPGRADES;++i) if(r->upgrade_choices[i]) c->ok=0;
    if(r->version>=13) home_run_codec(c,r);
    else if(c->out) {
        /* Never silently downgrade away measured event provenance. */
        for(i=0;i<ROGUE_FLOORS;++i) if(r->receipts[i].result.home_run.present ||
            !RogueEvent_HomeRunResultValid(&r->receipts[i].result)) c->ok=0;
    }
    if(r->version>=14) {
        for(i=0;i<ROGUE_FLOORS;++i) word(c,&r->receipts[i].result.event_end);
    } else if(c->out) {
        for(i=0;i<ROGUE_FLOORS;++i) if(r->receipts[i].result.event_end) c->ok=0;
    }
    if(r->version>=15) target_codec(c,r);
    else if(c->out) {
        for(i=0;i<ROGUE_FLOORS;++i)
            if(r->receipts[i].result.targets.present || !RogueTarget_ResultValid(&r->receipts[i].result)) c->ok=0;
        /* Before v15 this flag described availability, not a user choice. */
        for(i=0;i<3;++i) if(r->event_opportunities[i].event.native_kit_assist) c->ok=0;
    }
    if(r->version>=16) race_codec(c,r);
    else if(c->out) for(i=0;i<ROGUE_FLOORS;++i)
        if(r->receipts[i].result.race.present || !RogueRace_ResultValid(&r->receipts[i].result)) c->ok=0;
#undef FIELD
}

static int valid_transactions(const RogueRun* r)
{
    const RogueRewardSource* s=&r->reward_source;
    const RogueCampVisitState* v=&r->camp;
    unsigned i,j;
    if(r->catalog_version!=ROGUE_CATALOG_VERSION ||
        r->score_policy_version!=ROGUE_SCORE_POLICY_VERSION ||
        r->event_policy_version!=ROGUE_EVENT_POLICY_VERSION ||
        r->reward_policy_version>ROGUE_REWARD_POLICY_VERSION ||
        r->legacy_identity>1 || (!r->run_id)!=(int)r->legacy_identity) return 0;
    if(s->run_id!=r->run_id || s->kind>ROGUE_REWARD_LEGACY || s->node_id>30 ||
        s->receipt_id>30 || s->policy_version!=r->reward_policy_version ||
        s->won>1 || s->provenance_verified>1 || s->donor_count>ROGUE_REWARD_DONORS) return 0;
    if(s->generated>1 || s->claimed>1 || (s->claimed && !s->generated)) return 0;
    if((s->kind==ROGUE_REWARD_COMBAT || s->kind==ROGUE_REWARD_SKILL ||
        s->kind==ROGUE_REWARD_GAUNTLET) && !s->node_id) return 0;
    if(!s->provenance_verified && s->donor_count) return 0;
    if(s->provenance_verified && !s->receipt_id) return 0;
    for(i=0;i<s->donor_count;++i) {
        if(s->donors[i]>=26) return 0;
        for(j=0;j<i;++j) if(s->donors[i]==s->donors[j]) return 0;
    }
    if(v->active>1 || v->heal_used>1 || v->exit_committed>1 ||
        (v->active && v->exit_committed) || v->node_id>30 ||
        v->pending_special_slots>15 || v->pending_aerial_slots>31) return 0;
    if(v->node_id) {
        if(v->run_id!=r->run_id || (v->kind!=ROGUE_NODE_SHOP && v->kind!=ROGUE_NODE_REST)) return 0;
    } else if(v->active || v->heal_used || v->exit_committed || v->run_id) return 0;
    if(v->heal_used && v->kind!=ROGUE_NODE_REST) return 0;
    for(i=0;i<3;++i) if(v->general_prices[i]>1000000) return 0;
    for(i=0;i<ROGUE_AERIAL_SLOTS;++i) {
        if(v->aerial[i].claimed>1) return 0;
        for(j=0;j<3;++j) {
            RogueOfferRef ref=RogueOffer_Decode(v->aerial[i].ids[j]);
            if(!RogueOfferId_Valid(v->aerial[i].ids[j]) || v->aerial[i].sold[j]>1 ||
                v->aerial_prices[i][j]>1000000) return 0;
            if(ref.kind!=ROGUE_OFFER_NONE && (ref.kind!=ROGUE_OFFER_AERIAL ||
                RogueAerial_Find(ref.stable_id)->slot!=i)) return 0;
        }
    }
    return 1;
}

static int valid_encounter(const RogueEncounter* e)
{
    unsigned i;
    if (!e->recipe) return e->enemy_count == 0 && e->stage == 0;
    if (e->recipe > ROGUE_RECIPES || e->stage < 1 || e->stage > 6 ||
        e->enemy_count < 1 || e->enemy_count > 3 || e->damage < 1 || e->damage > 500 ||
        e->defense < 1 || e->defense > 500 || e->speed < 1 || e->speed > 200 ||
        e->scale < 25 || e->scale > 200 || e->starting_percent > 999) return 0;
    for (i = 0; i < e->enemy_count; ++i)
        if (e->fighters[i] >= 26 || e->stocks[i] < 1 || e->stocks[i] > 9) return 0;
    return 1;
}
static int valid_event(const RogueEventDesc* e, const RogueRun* r)
{
    unsigned i;
    if (e->template_id<1 || e->template_id>7 || e->node_id<1 || e->node_id>30 ||
        e->ruleset_version!=r->ruleset_version || e->base_character!=r->character ||
        e->dangerous!=(unsigned)(e->template_id==ROGUE_EVENT_GAUNTLET) || e->native_kit_assist>1) return 0;
    if (!e->dangerous && !e->timer_frames) return 0;
    if (e->dangerous) {
        for(i=0;i<3;++i) if(e->opponents[i]>=26) return 0;
        if(e->opponents[0]==e->opponents[1] || e->opponents[0]==e->opponents[2] || e->opponents[1]==e->opponents[2]) return 0;
    }
    return 1;
}
static int valid_node(const RogueNodeDesc* n, const RogueRun* r)
{
    if(n->kind>ROGUE_NODE_EVENT || n->node_id>30) return 0;
    if(n->node_id && n->ruleset_version!=r->ruleset_version) return 0;
    if(n->kind==ROGUE_NODE_EVENT) {
        const RogueEventOpportunity* op;
        if(!n->node_id || !valid_event(&n->payload.event,r) || n->node_id!=n->payload.event.node_id) return 0;
        op=&r->event_opportunities[(n->node_id-1)/10];
        return op->present && !memcmp(&op->event,&n->payload.event,sizeof(op->event));
    }
    return valid_encounter(&n->payload.combat);
}
static int valid_event_plan(const RogueRun* r)
{
    unsigned act, floor, side, used=0;
    if(r->ruleset_version!=1 || r->events_planned>1 || !r->proc_rng.state) return 0;
    for(act=0;act<3;++act) {
        const RogueEventOpportunity* op=&r->event_opportunities[act];
        if(op->present>1) return 0;
        if(!op->present) continue;
        if(!r->events_planned || op->floor<act*5+(act?0:1) || op->floor>=act*5+4 ||
            op->side>1 || op->alternative_kind>ROGUE_ELITE || !valid_event(&op->event,r)) return 0;
        if(!act && (op->event.template_id==ROGUE_EVENT_ESCAPE || op->event.template_id==ROGUE_EVENT_GAUNTLET)) return 0;
        if(op->event.node_id!=1+op->floor*2+op->side ||
            r->route[op->floor][op->side]!=ROGUE_NODE_EVENT ||
            r->route[op->floor][1-op->side]!=op->alternative_kind) return 0;
        if(used & (1U<<op->event.template_id)) return 0;
        used|=1U<<op->event.template_id;
    }
    for(floor=0;floor<15;++floor) for(side=0;side<2;++side) if(r->route[floor][side]==ROGUE_NODE_EVENT) {
        const RogueEventOpportunity* op=&r->event_opportunities[floor/5];
        if(!op->present || op->floor!=floor || op->side!=side) return 0;
    }
    return 1;
}
unsigned RogueRun_Serialize(const RogueRun* run, unsigned char* out, unsigned capacity)
{
    Codec c;
    if (!run || !out) return 0;
    c.out=out; c.in=0; c.pos=0; c.size=capacity; c.ok=1;
    /* Every output branch reads only; avoid a 100KB native stack copy. */
    codec(&c,(RogueRun*)run);
    return c.ok ? c.pos : 0;
}
static int valid_ledger(const RogueRun* r)
{
    RogueScoreReceipt expected;
    RogueScoreTotals totals;
    RoguePoints score=r->legacy_score_base, extra;
    unsigned i,count=0,mask=0;
    if(!r->legacy_score_unverified && r->legacy_score_base) return 0;
    memset(&totals,0,sizeof(totals));
    for(i=0;i<ROGUE_FLOORS;++i) {
        const RogueScoreReceipt* p=&r->receipts[i];
        if(!p->committed) {
            if(p->result.event_end || p->result.home_run.present || p->result.targets.present || p->result.race.present ||
                !RogueEvent_HomeRunResultValid(&p->result) || !RogueTarget_ResultValid(&p->result) || !RogueRace_ResultValid(&p->result)) return 0;
            continue;
        }
        if(p->committed!=1 || p->result.run_id!=r->run_id ||
            p->result.node_id!=1+i*2+r->selected_route[i] ||
            !RogueSettlement_Calculate(&p->result,&p->modifiers,&expected)) return 0;
        if(p->result.home_run.present || p->result.event_end) {
            const RogueEventOpportunity* op=&r->event_opportunities[i/5];
            if(!op->present || (p->result.home_run.present && op->event.template_id!=ROGUE_EVENT_HOME_RUN) ||
                op->event.node_id!=p->result.node_id) return 0;
        }
        if(p->result.targets.present) {
            const RogueEventDesc* e=&r->event_opportunities[i/5].event;
            if(e->template_id!=ROGUE_EVENT_TARGETS || e->base_character!=p->result.targets.base_character ||
                e->native_stage!=p->result.targets.native_stage || e->native_kit_assist!=p->result.targets.native_kit_assist) return 0;
        }
        if(p->result.race.present) {
            const RogueEventDesc* e=&r->event_opportunities[i/5].event;
            if(e->template_id!=ROGUE_EVENT_RACE || e->base_character!=p->result.race.base_character ||
                e->native_stage!=p->result.race.native_stage || e->native_kit_assist!=p->result.race.native_kit_assist ||
                e->timer_frames!=p->result.race.limit_frames) return 0;
        }
        expected.committed=1;
        if(memcmp(p,&expected,sizeof(expected))) return 0;
        count++; mask|=1U<<(p->result.node_id-1);
        if(!RoguePoints_Add(score,p->run_score_delta,&score) ||
            !RoguePoints_Add(totals.native_raw,p->result.native_stage_delta,&totals.native_raw) ||
            !RoguePoints_Add(totals.native_counted,p->native_counted_delta,&totals.native_counted) ||
            !RoguePoints_Add(totals.objective,p->result.objective_points,&totals.objective) ||
            !RoguePoints_Add(p->rogue_clear_points,p->result.upgrade_style_points,&extra) ||
            !RoguePoints_Add(extra,p->showmanship_bonus,&extra) ||
            !RoguePoints_Add(totals.rogue_added,extra,&totals.rogue_added)) return 0;
        totals.clear_gold+=p->base_gold; totals.performance_gold+=p->performance_gold;
        totals.effect_gold+=p->effect_gold;
    }
    return count==r->receipt_count && mask==r->settled_nodes && count<=r->nodes_resolved &&
        score==r->score && !memcmp(&totals,&r->score_totals,sizeof(totals));
}
/* Single guarded decode workspace: no native heap pointers enter a snapshot,
 * and a failed decode never modifies the caller's run. */
static RogueRun copy;
static unsigned decoding;
static unsigned decode_snapshot(const unsigned char* data, unsigned size)
{
    Codec c;
    unsigned i,j;
    if (!data) return 0;
    memset(&copy,0,sizeof(copy));
    c.out=0; c.in=data; c.pos=0; c.size=size; c.ok=1;
    codec(&c,&copy);
    if (!c.ok || c.pos != size || (copy.version < 4 || copy.version > ROGUE_SNAPSHOT_VERSION) || copy.character >= 26 || copy.carried_percent > 999 ||
        copy.act < 1 || copy.act > 3 || copy.floor > ROGUE_FLOORS || copy.phase > ROGUE_EVENT ||
        (copy.floor == ROGUE_FLOORS && copy.phase != ROGUE_COMPLETE)) return 0;
    for(i=0;i<ROGUE_FLOORS;++i) {
        if(copy.selected_route[i]>1) return 0;
        for(j=0;j<2;++j) if(copy.route[i][j]>(unsigned)(copy.version>=6 ? ROGUE_NODE_EVENT : ROGUE_NODE_REST)) return 0;
    }
    if (copy.combat_nodes_won > ROGUE_FLOORS || copy.death_reason > 2 ||
        copy.reroll_gold_spent > copy.gold_spent ||
        copy.gold > ROGUE_WALLET_LIMIT || (copy.version<9 && copy.score > 100000000) ||
        copy.reward.claimed > 1 || copy.camp.general.claimed > 1) return 0;
    for (i = 0; i < ROGUE_UPGRADES; ++i) {
        const RogueUpgradeDef* def=RogueUpgrade_Find(RogueUpgrade_StackId(i));
        if(copy.upgrade_choices[i] && (!def || def->choice!=ROGUE_CHOICE_SPECIAL_SLOT ||
            !copy.stacks[i] || copy.upgrade_choices[i]>4)) return 0;
        if(def && def->choice==ROGUE_CHOICE_SPECIAL_SLOT && copy.stacks[i] &&
            !copy.upgrade_choices[i]) return 0;
        if ((!def && copy.stacks[i]) || (def && copy.stacks[i] > def->max_stacks)) return 0;
    }
    /* Old wire IDs were validated and converted at the codec boundary. */
    for(i=0;i<3;++i) if(!RogueOfferId_Valid(copy.reward.ids[i]) ||
        !RogueOfferId_Valid(copy.camp.general.ids[i]) ||
        copy.camp.general.sold[i]>1 || copy.reward.sold[i]>1) return 0;
    if (!valid_node(&copy.current, &copy) || !valid_node(&copy.preview[0], &copy) ||
        !valid_node(&copy.preview[1], &copy) || !valid_event_plan(&copy)) return 0;
    for(i=0;i<4;++i) if(copy.specials[i]) {
        const RogueSpecialDef* special = RogueSpecial_Find(copy.specials[i]);
        if (!special || special->slot != i) return 0;
    }
    for (i=0;i<ROGUE_AERIAL_SLOTS;++i) if (copy.aerials[i]) {
        const RogueAerialDef* aerial=RogueAerial_Find(copy.aerials[i]);
        if (!aerial || aerial->slot != i) return 0;
    }
    if(!copy.route_rng.state || !copy.encounter_rng.state || !copy.reward_rng.state || !copy.shop_rng.state) return 0;
    if (copy.starting_stocks < 1 || copy.starting_stocks > 5 || copy.difficulty > 4 || copy.costume > 5) return 0;
    if(copy.nodes_resolved>15 || copy.events_attempted>3 || copy.events_completed>copy.events_attempted ||
        copy.submatches_played>21) return 0;
    if(copy.phase==ROGUE_EVENT && copy.current.kind!=ROGUE_NODE_EVENT) return 0;
    if(copy.legacy_score_unverified>1 || copy.legacy_active_incompatible>1) return 0;
    if(!valid_transactions(&copy)) return 0;
    if(!valid_ledger(&copy)) return 0;
    if(copy.version<15) {
        /* Validate the old snapshot first. Capability must not migrate into
         * consent to change the event kit. No old metric claims are invented. */
        for(i=0;i<3;++i) copy.event_opportunities[i].event.native_kit_assist=0;
        if(copy.current.kind==ROGUE_NODE_EVENT) copy.current.payload.event.native_kit_assist=0;
        for(i=0;i<2;++i) if(copy.preview[i].kind==ROGUE_NODE_EVENT)
            copy.preview[i].payload.event.native_kit_assist=0;
    }
    if(copy.reward_policy_version<ROGUE_REWARD_POLICY_VERSION || copy.version<10) {
        copy.reward_policy_version=ROGUE_REWARD_POLICY_VERSION;
        copy.reward_source.policy_version=ROGUE_REWARD_POLICY_VERSION;
        if(copy.phase==ROGUE_REWARD && !copy.reward.claimed) {
            RogueRng migration;
            RogueOfferState next;
            RogueHistoryEvent* event;
            unsigned reason;
            /* Missing legacy provenance never becomes an upcoming donor. */
            if(!RogueReward_Entitled(&copy)) {
                memset(&copy.reward_source,0,sizeof(copy.reward_source));
                copy.reward_source.run_id=copy.run_id;
                copy.reward_source.kind=ROGUE_REWARD_LEGACY;
                copy.reward_source.policy_version=ROGUE_REWARD_POLICY_VERSION;
            }
            RogueRng_Init(&migration,copy.seed,0x524557U);
            if(!RogueReward_Generate(&copy,&migration,&copy.reward,&next,&reason)) return 0;
            copy.reward=next;
            copy.reward_source.generated=1;
            event=&copy.history[copy.history_count%ROGUE_HISTORY];
            memset(event,0,sizeof(*event));
            event->kind=9; event->floor=copy.floor; event->generation=copy.history_count;
            event->after[0]=reason;
            copy.history_count++;
        }
        else if(copy.reward.claimed && copy.reward_source.kind!=ROGUE_REWARD_NONE) {
            copy.reward_source.generated=copy.reward_source.claimed=1;
        }
    }
    if(copy.phase==ROGUE_REWARD && !copy.reward.claimed &&
       !RogueReward_Validate(&copy,&copy.reward)) return 0;
    if(copy.version<11 && copy.camp.active) {
        unsigned illegal=0;
        for(i=0;i<3;++i) if(!copy.camp.general.sold[i] &&
            RogueOffer_Decode(copy.camp.general.ids[i]).kind==ROGUE_OFFER_AERIAL) illegal=1;
        if(illegal) {
            RogueRng migration;
            RogueRng_Derive(&migration,copy.seed,ROGUE_STREAM_SHOP,11,copy.camp.node_id);
            RogueCamp_GeneralOffers(&copy,&copy.camp.general,&migration,1);
        }
        RogueCamp_PrepareAerials(&copy);
        {
            RogueHistoryEvent* event=&copy.history[copy.history_count%ROGUE_HISTORY];
            memset(event,0,sizeof(*event));
            event->kind=12; event->floor=copy.floor; event->generation=copy.history_count++;
            event->after[0]=11; event->after[1]=illegal;
        }
    }
    if(!RogueCamp_ValidateInventory(&copy)) return 0;
    copy.version = ROGUE_SNAPSHOT_VERSION;
    return copy.legacy_active_incompatible ? ROGUE_IMPORT_INCOMPATIBLE_ACTIVE : ROGUE_IMPORT_READY;
}
static unsigned import_snapshot(RogueRun* run, const unsigned char* data, unsigned size, int archive)
{
    unsigned result;
    if(!run || decoding) return ROGUE_IMPORT_INVALID;
    decoding=1;
    result=decode_snapshot(data,size);
    if(result==ROGUE_IMPORT_READY || (archive && result==ROGUE_IMPORT_INCOMPATIBLE_ACTIVE)) *run=copy;
    decoding=0;
    return result;
}
unsigned RogueRun_ImportSnapshot(RogueRun* run, const unsigned char* data, unsigned size)
{ return import_snapshot(run,data,size,1); }
int RogueRun_Deserialize(RogueRun* run, const unsigned char* data, unsigned size)
{ return import_snapshot(run,data,size,0)==ROGUE_IMPORT_READY; }
