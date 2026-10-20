#ifndef ROGUE_OFFERS_H
#define ROGUE_OFFERS_H
#include "run.h"
#include "offer_identity.h"
#include "../combat/specials/special_catalog.h"
#define ROGUE_AERIAL_OFFER_BASE 128U /* Frozen v4 compatibility only. */
#define ROGUE_OFFERS (ROGUE_UPGRADES + ROGUE_SPECIALS + ROGUE_AERIALS)
int RogueOfferRef_Eligible(const RogueRun* run, RogueOfferRef ref);
int RogueOfferRef_Apply(RogueRun* run, RogueOfferRef ref);
/* Local modal state, never a fighter pointer or a committed acquisition. */
typedef struct RogueAcquireChoice {
    RogueSeed run_id;
    unsigned active, phase, card, node, price, history_count;
    RogueOfferState offers;
    RogueRewardSource source;
} RogueAcquireChoice;
int RogueOffer_ChoiceBegin(const RogueRun* run, unsigned card, RogueAcquireChoice* choice);
void RogueOffer_ChoiceCancel(RogueAcquireChoice* choice);
int RogueOffer_ChoiceCommit(RogueRun* run, RogueAcquireChoice* choice, unsigned special_slot);
int RogueOffer_BuyGeneral(RogueRun* run, unsigned slot);
/* Internal pool primitive. Callers supply validated same-category candidates. */
RogueOfferId RogueOffer_Pick(RogueRng* rng, RogueOfferId* ids, unsigned* count,
                             const RogueOfferState* prior);
unsigned RogueOfferRef_Price(const RogueRun* run, RogueOfferRef ref);
const char* RogueOfferRef_Name(RogueOfferRef ref);
const char* RogueOfferRef_Description(RogueOfferRef ref);
int RogueOffer_Eligible(const RogueRun* run, unsigned id);
int RogueOffer_Apply(RogueRun* run, unsigned id);
unsigned RogueOffer_Price(const RogueRun* run, unsigned id);
const char* RogueOffer_Name(unsigned id);
const char* RogueOffer_Description(unsigned id);
#endif
