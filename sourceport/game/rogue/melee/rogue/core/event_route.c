#include "event_route.h"
#include "run.h"
#include <string.h>

int RogueEvent_HasNativeKit(unsigned id)
{
    return id==ROGUE_EVENT_TARGETS || id==ROGUE_EVENT_RACE ||
        id==ROGUE_EVENT_DASH || id==ROGUE_EVENT_ESCAPE;
}

int RogueEventRoute_Build(RogueSeed seed, unsigned ruleset, unsigned character,
    const unsigned route[15][2], const RogueEventAvailability available[7],
    const unsigned combat_stages[6], RogueEventOpportunity out[3])
{
    static const unsigned weights[7] = { 20, 25, 15, 12, 8, 8, 12 };
    RogueEventOpportunity result[3];
    unsigned act, i, side, used = 0;
    if (!route || !available || !combat_stages || !out || !ruleset || character >= 26) return 0;
    for (i = 0; i < 15; ++i) for (side = 0; side < 2; ++side) {
        if (route[i][side] > ROGUE_NODE_REST) return 0;
        if (i % 5 == 4 && route[i][side] != ROGUE_BOSS) return 0;
        if (!i && route[i][side] != ROGUE_NORMAL) return 0;
    }
    for (i = 0; i < 7; ++i) {
        if (available[i].supported > 1) return 0;
        if (available[i].supported && i != ROGUE_EVENT_GAUNTLET - 1 &&
            !available[i].timer_frames) return 0;
    }
    memset(result, 0, sizeof(result));
    for (act = 0; act < 3; ++act) {
        RogueRng layout, params, alternative;
        RogueEventOpportunity* opportunity = &result[act];
        RogueEventDesc* event = &opportunity->event;
        unsigned total = 0, pick, id, pool[7], count = 0;
        RogueRng_Derive(&layout, seed, ROGUE_STREAM_EVENT_LAYOUT, ruleset, act);
        if (RogueRng_Bounded(&layout, 100) >= 80) continue;
        opportunity->floor = act * 5 + (act ? 0 : 1) +
            RogueRng_Bounded(&layout, act ? 4 : 3);
        opportunity->side = RogueRng_Bounded(&layout, 2);
        for (i = 0; i < 7; ++i) {
            if (!available[i].supported || (used & (1U << i))) continue;
            if (!act && (i == ROGUE_EVENT_ESCAPE - 1 || i == ROGUE_EVENT_GAUNTLET - 1)) continue;
            pool[count++] = i; total += weights[i];
        }
        if (!count) { memset(opportunity, 0, sizeof(*opportunity)); continue; }
        pick = RogueRng_Bounded(&layout, total);
        for (i = 0; i + 1 < count && pick >= weights[pool[i]]; ++i) pick -= weights[pool[i]];
        id = pool[i]; used |= 1U << id;
        opportunity->present = 1;
        event->template_id = id + 1;
        event->node_id = 1 + opportunity->floor * 2 + opportunity->side;
        event->ruleset_version = ruleset; event->base_character = character;
        event->native_stage = available[id].native_stage;
        event->course_id = available[id].course_id;
        event->timer_frames = available[id].timer_frames;
        event->dangerous = event->template_id == ROGUE_EVENT_GAUNTLET;
        /* This records the player's selection, not the template's capability.
         * Equipped moves are the default; entry UI may opt into native moves. */
        event->native_kit_assist = 0;
        opportunity->alternative_kind = route[opportunity->floor][1 - opportunity->side];
        if (opportunity->alternative_kind != ROGUE_NORMAL && opportunity->alternative_kind != ROGUE_ELITE) {
            RogueRng_Derive(&alternative, seed, ROGUE_STREAM_EVENT_ALTERNATIVE, ruleset, event->node_id);
            opportunity->alternative_kind = RogueRng_Bounded(&alternative, 8) < 5 ? ROGUE_NORMAL : ROGUE_ELITE;
        }
        RogueRng_Derive(&params, seed, ROGUE_STREAM_EVENT_PARAMS, ruleset, event->node_id);
        if (event->dangerous) {
            unsigned fighters[26], j;
            for (j = 0; j < 26; ++j) fighters[j] = j;
            for (j = 0; j < 3; ++j) {
                pick = RogueRng_Bounded(&params, 26 - j);
                event->opponents[j] = fighters[pick];
                fighters[pick] = fighters[25 - j];
                event->combat_stages[j] = combat_stages[RogueRng_Bounded(&params, 6)];
            }
        }
    }
    memcpy(out, result, sizeof(result));
    return 1;
}
