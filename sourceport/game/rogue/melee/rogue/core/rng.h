#ifndef ROGUE_RNG_H
#define ROGUE_RNG_H
typedef struct RogueRng { unsigned state, draws; } RogueRng;
typedef unsigned long long RogueSeed;
enum RogueRngStream { ROGUE_STREAM_ROUTE = 0, ROGUE_STREAM_ENCOUNTER = 1,
    ROGUE_STREAM_REWARD = 2, ROGUE_STREAM_SHOP = 3,
    ROGUE_STREAM_EVENT_LAYOUT = 4, ROGUE_STREAM_EVENT_PARAMS = 5,
    ROGUE_STREAM_EVENT_ALTERNATIVE = 6, ROGUE_STREAM_PROC = 7,
    ROGUE_STREAM_AERIAL_VENDOR = 8 };
void RogueRng_Init(RogueRng* rng, RogueSeed seed, unsigned stream);
void RogueRng_Derive(RogueRng* rng, RogueSeed seed, unsigned stream,
                     unsigned ruleset, unsigned node);
unsigned RogueRng_Next(RogueRng* rng);
unsigned RogueRng_Bounded(RogueRng* rng, unsigned bound);
#endif
