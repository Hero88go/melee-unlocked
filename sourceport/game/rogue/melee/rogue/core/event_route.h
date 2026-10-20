#ifndef ROGUE_EVENT_ROUTE_H
#define ROGUE_EVENT_ROUTE_H
#include "rng.h"

enum RogueEventId { ROGUE_EVENT_NONE, ROGUE_EVENT_HOME_RUN,
    ROGUE_EVENT_TARGETS, ROGUE_EVENT_RACE, ROGUE_EVENT_DASH,
    ROGUE_EVENT_ESCAPE, ROGUE_EVENT_GAUNTLET, ROGUE_EVENT_SANDBAG };
#define ROGUE_EVENT_TEMPLATES 7
#define ROGUE_EVENT_OPPORTUNITIES 3
int RogueEvent_HasNativeKit(unsigned template_id);

/* Native adapters supply resolved, base-character-specific values. Unknown
 * timers/course mappings are not invented by the route generator. */
typedef struct RogueEventAvailability {
    unsigned supported, native_stage, course_id, timer_frames;
} RogueEventAvailability;

typedef struct RogueEventDesc {
    unsigned template_id, node_id, ruleset_version, base_character;
    /* native_kit_assist records the chosen event-local policy. */
    unsigned native_stage, course_id, timer_frames, dangerous, native_kit_assist;
    unsigned opponents[3], combat_stages[3];
} RogueEventDesc;

typedef struct RogueEventOpportunity {
    unsigned present, floor, side, alternative_kind;
    RogueEventDesc event;
} RogueEventOpportunity;

/* Pure construction. Caller owns/finalizes the result once per run; rendering
 * only reads it. No run RNG, equipment, cooldown or played counter is mutated. */
int RogueEventRoute_Build(RogueSeed seed, unsigned ruleset, unsigned character,
    const unsigned route[15][2], const RogueEventAvailability available[7],
    const unsigned combat_stages[6], RogueEventOpportunity out[3]);
#endif
