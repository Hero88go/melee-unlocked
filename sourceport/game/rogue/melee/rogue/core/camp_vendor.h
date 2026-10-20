#ifndef ROGUE_CAMP_VENDOR_H
#define ROGUE_CAMP_VENDOR_H
#include "run.h"
void RogueCamp_GeneralOffers(RogueRun* run, RogueOfferState* state, RogueRng* rng, int reroll);
void RogueCamp_PrepareAerials(RogueRun* run);
void RogueCamp_Prices(RogueRun* run);
int RogueCamp_Active(const RogueRun* run);
int RogueCamp_ValidateInventory(const RogueRun* run);
/* Aerial filters reveal persisted inventories. They never generate stock. */
const RogueOfferState* RogueRun_AerialOffers(const RogueRun* run, unsigned aerial_slot);
unsigned RogueRun_AerialRerollCost(const RogueRun* run);
int RogueRun_RerollAerial(RogueRun* run, unsigned aerial_slot);
int RogueRun_BuyAerial(RogueRun* run, unsigned aerial_slot, unsigned card);
#endif
