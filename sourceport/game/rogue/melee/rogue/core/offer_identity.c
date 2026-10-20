#include "offer_identity.h"
#include "../upgrades/upgrade_registry.h"
#include "../combat/specials/special_catalog.h"
#include "../combat/aerials/aerial_catalog.h"
#include "legacy_v4_offers.inc"

RogueOfferId RogueOffer_Encode(RogueOfferRef ref)
{
    if (!RogueOfferRef_Valid(ref) || ref.stable_id > 0x00ffffffU) return 0;
    return (ref.kind << 24) | ref.stable_id;
}

RogueOfferRef RogueOffer_Decode(RogueOfferId id)
{
    RogueOfferRef ref;
    /* Explicit compatibility boundary for old callers, never count offsets. */
    if (id <= 258) return RogueOffer_FromLegacyV4(id);
    ref.kind = id >> 24; ref.stable_id = id & 0x00ffffffU;
    return ref;
}

int RogueOfferId_Valid(RogueOfferId id)
{
    RogueOfferRef ref = RogueOffer_Decode(id);
    return RogueOfferRef_Valid(ref) && (!id || ref.kind != ROGUE_OFFER_NONE);
}

RogueOfferId RogueOffer_At(unsigned index)
{
    RogueOfferRef ref;
    if (index < sizeof(rogue_upgrades) / sizeof(rogue_upgrades[0])) {
        ref.kind = ROGUE_OFFER_UPGRADE; ref.stable_id = rogue_upgrades[index].id;
    } else {
        index -= sizeof(rogue_upgrades) / sizeof(rogue_upgrades[0]);
        if (index < ROGUE_SPECIALS) {
            ref.kind = ROGUE_OFFER_SPECIAL; ref.stable_id = rogue_specials[index].id;
        } else {
            index -= ROGUE_SPECIALS;
            if (index >= ROGUE_AERIALS) return 0;
            ref.kind = ROGUE_OFFER_AERIAL; ref.stable_id = rogue_aerials[index].id;
        }
    }
    return RogueOffer_Encode(ref);
}

RogueOfferRef RogueOffer_FromLegacyV4(unsigned wire_id)
{
    RogueOfferRef none = { ROGUE_OFFER_NONE, 0 };
    if (!wire_id || wire_id > 258) return none;
    return rogue_legacy_v4_offers[wire_id - 1];
}

int RogueOfferRef_Valid(RogueOfferRef ref)
{
    if (!ref.stable_id) return ref.kind == ROGUE_OFFER_NONE;
    switch (ref.kind) {
    case ROGUE_OFFER_UPGRADE: return RogueUpgrade_Find(ref.stable_id) != 0;
    case ROGUE_OFFER_SPECIAL: return RogueSpecial_Find(ref.stable_id) != 0;
    case ROGUE_OFFER_AERIAL: return RogueAerial_Find(ref.stable_id) != 0;
    case ROGUE_OFFER_GOLD: return ref.stable_id == 10;
    }
    return 0;
}

unsigned RogueOffer_ToLegacyV4(RogueOfferRef ref)
{
    unsigned i;
    for (i = 0; i < 258; ++i)
        if (rogue_legacy_v4_offers[i].kind == ref.kind &&
            rogue_legacy_v4_offers[i].stable_id == ref.stable_id) return i + 1;
    return 0; /* New catalog entries have no old wire representation. */
}
