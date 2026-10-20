#ifndef ROGUE_REWARD_POLICY_H
#define ROGUE_REWARD_POLICY_H
#include "run.h"

enum RogueRewardFallback { ROGUE_REWARD_SPECIAL_PRESENT,
    ROGUE_REWARD_GENERIC_CONTEXT, ROGUE_REWARD_NO_VERIFIED_DONOR,
    ROGUE_REWARD_NO_ELIGIBLE_SPECIAL };
/* Receipt-backed entitlement. Never reads current/preview fighter identities. */
int RogueReward_Entitled(const RogueRun* run);
int RogueReward_Eligible(const RogueRun* run, RogueOfferRef ref);
/* Builds a value-only draft; rejects invalid source without touching RNG/out. */
int RogueReward_Generate(const RogueRun* run, RogueRng* rng,
                         const RogueOfferState* prior, RogueOfferState* out,
                         unsigned* fallback);
int RogueReward_Validate(const RogueRun* run, const RogueOfferState* draft);
#endif
