#ifndef ROGUE_OFFER_IDENTITY_H
#define ROGUE_OFFER_IDENTITY_H

enum RogueOfferKind { ROGUE_OFFER_NONE, ROGUE_OFFER_UPGRADE,
                      ROGUE_OFFER_SPECIAL, ROGUE_OFFER_AERIAL, ROGUE_OFFER_GOLD };
/* Value-only persistent identity: catalog position is never a stable ID. */
typedef struct RogueOfferRef { unsigned kind, stable_id; } RogueOfferRef;
/* High byte is the kind; low 24 bits are the authored stable ID. */
typedef unsigned RogueOfferId;
RogueOfferId RogueOffer_Encode(RogueOfferRef ref);
RogueOfferRef RogueOffer_Decode(RogueOfferId id);
int RogueOfferId_Valid(RogueOfferId id);
/* Catalog iteration is transient; the returned identity is never an index. */
RogueOfferId RogueOffer_At(unsigned index);
RogueOfferRef RogueOffer_FromLegacyV4(unsigned wire_id);
int RogueOfferRef_Valid(RogueOfferRef ref);
unsigned RogueOffer_ToLegacyV4(RogueOfferRef ref);
#endif
