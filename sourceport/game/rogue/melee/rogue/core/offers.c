#include "offers.h"
#include "../upgrades/upgrade_registry.h"
#include "reward_policy.h"
#include "camp_vendor.h"
#include <string.h>

static const RogueUpgradeDef* upgrade(RogueOfferRef ref)
{
    if (ref.kind != ROGUE_OFFER_UPGRADE || !ref.stable_id ||
        RogueUpgrade_StackIndex(ref.stable_id) >= ROGUE_UPGRADES) return 0;
    return RogueUpgrade_Find(ref.stable_id);
}

int RogueOfferRef_Eligible(const RogueRun* run, RogueOfferRef ref)
{
    if (!run || !ref.stable_id || !RogueOfferRef_Valid(ref)) return 0;
    if (ref.kind == ROGUE_OFFER_UPGRADE) {
        const RogueUpgradeDef* def = upgrade(ref);
        return def && run->stacks[RogueUpgrade_StackIndex(ref.stable_id)] < def->max_stacks;
    }
    if (ref.kind == ROGUE_OFFER_SPECIAL) {
        const RogueSpecialDef* def = RogueSpecial_Find(ref.stable_id);
        return def->character != run->character && run->specials[def->slot] != def->id;
    }
#if ROGUE_ENABLE_AERIALS
    if (ref.kind == ROGUE_OFFER_AERIAL) {
        const RogueAerialDef* def = RogueAerial_Find(ref.stable_id);
        return def->character != run->character && run->aerials[def->slot] != def->id;
    }
#endif
    return 0;
}

static void reward_commit(RogueRun* run, RogueOfferRef ref, unsigned slot)
{
    RogueHistoryEvent* event;
    if(run->phase!=ROGUE_REWARD) return;
    event=&run->history[run->history_count%ROGUE_HISTORY];
    /* The public apply path and UI claim share the same one-choice commit. */
    event->kind=3; event->floor=run->floor; event->cost=0;
    event->generation=run->history_count++;
    event->before[0]=event->before[1]=event->before[2]=0;
    event->after[0]=RogueOffer_Encode(ref); event->after[1]=slot; event->after[2]=0;
    run->reward.claimed=run->reward_source.claimed=1;
    run->phase=ROGUE_ROUTE;
}
/* Kept private: callers must pass a complete reward or purchase transaction. */
static int choice_valid(RogueOfferRef ref, unsigned choice)
{
    const RogueUpgradeDef* def=upgrade(ref);
    if(def && def->choice==ROGUE_CHOICE_SPECIAL_SLOT) return choice>=1 && choice<=4;
    return !choice;
}
static void apply_owned(RogueRun* run, RogueOfferRef ref, unsigned choice)
{
    if(ref.kind==ROGUE_OFFER_UPGRADE) {
        unsigned index=RogueUpgrade_StackIndex(ref.stable_id);
        run->stacks[index]++;
        run->upgrade_choices[index]=choice;
    }
    else {
        const RogueSpecialDef* def=RogueSpecial_Find(ref.stable_id);
        run->specials[def->slot]=def->id;
    }
}
static int buy_general(RogueRun* run, unsigned slot, unsigned choice)
{
    RogueOfferRef ref;
    RogueHistoryEvent* event;
    unsigned price;
    if(!RogueCamp_Active(run) || run->phase!=ROGUE_SHOP || slot>=3 ||
       run->camp.general.claimed || run->camp.general.sold[slot]) return 0;
    ref=RogueOffer_Decode(run->camp.general.ids[slot]);
    if((ref.kind!=ROGUE_OFFER_UPGRADE && ref.kind!=ROGUE_OFFER_SPECIAL) ||
       !RogueOfferRef_Eligible(run,ref)) return 0;
    price=run->camp.general_prices[slot];
    if(!price || price!=RogueOfferRef_Price(run,ref) || run->gold<price) return 0;
    if(!choice_valid(ref,choice)) return 0;
    apply_owned(run,ref,choice);
    run->gold-=price; run->gold_spent+=price;
    run->camp.general.sold[slot]=1;
    if(ref.kind==ROGUE_OFFER_SPECIAL)
        run->camp.pending_special_slots|=1U<<RogueSpecial_Find(ref.stable_id)->slot;
    event=&run->history[run->history_count%ROGUE_HISTORY];
    event->kind=6; event->floor=run->floor; event->cost=price;
    event->generation=run->history_count++;
    event->before[0]=event->before[1]=event->before[2]=0;
    event->after[0]=RogueOffer_Encode(ref); event->after[1]=slot; event->after[2]=choice;
    RogueCamp_Prices(run);
    return 1;
}
static int apply_offer(RogueRun* run, RogueOfferRef ref, unsigned choice)
{
    unsigned i=0;
    if(!run || (run->phase!=ROGUE_REWARD && run->phase!=ROGUE_SHOP)) return 0;
    if(run->phase==ROGUE_SHOP) {
        if(ref.kind!=ROGUE_OFFER_UPGRADE && ref.kind!=ROGUE_OFFER_SPECIAL) return 0;
        for(i=0;i<3;++i) if(!run->camp.general.sold[i] && RogueOffer_Encode(ref)==
            RogueOffer_Encode(RogueOffer_Decode(run->camp.general.ids[i])))
            return buy_general(run,i,choice);
        return 0;
    }
    if(run->phase==ROGUE_REWARD &&
       (!RogueReward_Validate(run,&run->reward) || !RogueReward_Eligible(run,ref))) return 0;
    if(run->phase==ROGUE_REWARD) {
        for(i=0;i<3;++i) if(RogueOffer_Encode(ref)==
            RogueOffer_Encode(RogueOffer_Decode(run->reward.ids[i]))) break;
        if(i==3) return 0;
    }
    if(ref.kind==ROGUE_OFFER_GOLD) {
        unsigned gain;
        if(run->phase!=ROGUE_REWARD || ref.stable_id!=10 || run->gold>ROGUE_WALLET_LIMIT) return 0;
        gain=ROGUE_WALLET_LIMIT-run->gold;
        if(gain>10) gain=10;
        run->gold+=gain;
        reward_commit(run,ref,i);
        run->history[(run->history_count-1)%ROGUE_HISTORY].after[2]=gain;
        return 1;
    }
    /* Aerial selection is committed only by its priced camp service. */
    if(ref.kind==ROGUE_OFFER_AERIAL) return 0;
    if (!RogueOfferRef_Eligible(run, ref)) return 0;
    if(ref.kind!=ROGUE_OFFER_UPGRADE && ref.kind!=ROGUE_OFFER_SPECIAL) return 0;
    if(!choice_valid(ref,choice)) return 0;
    apply_owned(run,ref,choice);
    reward_commit(run,ref,i);
    run->history[(run->history_count-1)%ROGUE_HISTORY].after[2]=choice;
    return 1;
}

int RogueOffer_BuyGeneral(RogueRun* run, unsigned slot)
{ return buy_general(run,slot,0); }
int RogueOfferRef_Apply(RogueRun* run, RogueOfferRef ref)
{ return apply_offer(run,ref,0); }

int RogueOffer_ChoiceBegin(const RogueRun* run, unsigned card, RogueAcquireChoice* choice)
{
    const RogueOfferState* offers;
    const RogueUpgradeDef* def;
    RogueOfferRef ref;
    RogueAcquireChoice pending;
    if(!run || !choice || choice->active || card>=3) return 0;
    memset(&pending,0,sizeof(pending));
    if(run->phase==ROGUE_REWARD) {
        if(!RogueReward_Validate(run,&run->reward)) return 0;
        offers=&run->reward;
        pending.node=run->reward_source.node_id;
        pending.source=run->reward_source;
    } else if(run->phase==ROGUE_SHOP) {
        if(!RogueCamp_Active(run) || run->camp.general.claimed || run->camp.general.sold[card]) return 0;
        offers=&run->camp.general;
        pending.node=run->camp.node_id;
        pending.price=run->camp.general_prices[card];
        if(!pending.price || run->gold<pending.price ||
            pending.price!=RogueOffer_Price(run,offers->ids[card])) return 0;
    } else return 0;
    ref=RogueOffer_Decode(offers->ids[card]);
    def=upgrade(ref);
    if(!def || def->choice!=ROGUE_CHOICE_SPECIAL_SLOT || !RogueOfferRef_Eligible(run,ref)) return 0;
    if(run->phase==ROGUE_REWARD && !RogueReward_Eligible(run,ref)) return 0;
    pending.active=1; pending.run_id=run->run_id; pending.phase=run->phase;
    pending.card=card; pending.offers=*offers; pending.history_count=run->history_count;
    *choice=pending;
    return 1;
}
void RogueOffer_ChoiceCancel(RogueAcquireChoice* choice)
{ if(choice) memset(choice,0,sizeof(*choice)); }
int RogueOffer_ChoiceCommit(RogueRun* run, RogueAcquireChoice* choice, unsigned special_slot)
{
    RogueAcquireChoice live;
    int committed;
    if(!choice || !choice->active || special_slot>=4) return 0;
    memset(&live,0,sizeof(live));
    if(!RogueOffer_ChoiceBegin(run,choice->card,&live) || memcmp(&live,choice,sizeof(live))) return 0;
    committed=run->phase==ROGUE_SHOP ? buy_general(run,choice->card,special_slot+1) :
        apply_offer(run,RogueOffer_Decode(choice->offers.ids[choice->card]),special_slot+1);
    if(committed) RogueOffer_ChoiceCancel(choice);
    return committed;
}

unsigned RogueOfferRef_Price(const RogueRun* run, RogueOfferRef ref)
{
    unsigned price;
    if (!run || !ref.stable_id || !RogueOfferRef_Valid(ref)) return 0;
    if (ref.kind == ROGUE_OFFER_GOLD) return 0;
    if (ref.kind == ROGUE_OFFER_UPGRADE) price = RogueUpgrade_Find(ref.stable_id)->price;
    else if (ref.kind == ROGUE_OFFER_SPECIAL) price = RogueSpecial_Find(ref.stable_id)->price;
    else price = RogueAerial_Find(ref.stable_id)->price;
    return (price * (100 - run->stacks[22] * 5) + 99) / 100;
}

const char* RogueOfferRef_Name(RogueOfferRef ref)
{
    if (!ref.stable_id || !RogueOfferRef_Valid(ref)) return "No offer";
    if (ref.kind == ROGUE_OFFER_GOLD) return "10 gold";
    if (ref.kind == ROGUE_OFFER_UPGRADE) return RogueUpgrade_Find(ref.stable_id)->name;
    if (ref.kind == ROGUE_OFFER_SPECIAL) return RogueSpecial_Find(ref.stable_id)->name;
    return RogueAerial_Find(ref.stable_id)->name;
}

const char* RogueOfferRef_Description(RogueOfferRef ref)
{
    static const char* slots[4] = { "Replace neutral special", "Replace side special", "Replace up special", "Replace down special" };
    static const char* aerial_slots[5] = { "Equip Nair next fight.", "Equip Fair next fight.", "Equip Bair next fight.", "Equip Uair next fight.", "Equip Dair next fight." };
    if (!ref.stable_id || !RogueOfferRef_Valid(ref)) return "";
    if (ref.kind == ROGUE_OFFER_GOLD) return "Gain 10 gold, up to wallet limit.";
    if (ref.kind == ROGUE_OFFER_UPGRADE) return RogueUpgrade_Find(ref.stable_id)->description;
    if (ref.kind == ROGUE_OFFER_AERIAL) return aerial_slots[RogueAerial_Find(ref.stable_id)->slot];
    if (ref.stable_id == 16) return "Down special. Ground only.";
    return slots[RogueSpecial_Find(ref.stable_id)->slot];
}

/* Compatibility boundary for pre-expansion snapshots and regression fixtures.
 * Never use the current upgrade count to interpret these wire IDs. */
int RogueOffer_Eligible(const RogueRun* run, unsigned id)
{ return RogueOfferRef_Eligible(run, RogueOffer_Decode(id)); }
int RogueOffer_Apply(RogueRun* run, unsigned id)
{ return RogueOfferRef_Apply(run, RogueOffer_Decode(id)); }
unsigned RogueOffer_Price(const RogueRun* run, unsigned id)
{ return RogueOfferRef_Price(run, RogueOffer_Decode(id)); }
const char* RogueOffer_Name(unsigned id)
{ return RogueOfferRef_Name(RogueOffer_Decode(id)); }
const char* RogueOffer_Description(unsigned id)
{ return RogueOfferRef_Description(RogueOffer_Decode(id)); }

RogueOfferId RogueOffer_Pick(RogueRng* rng, RogueOfferId* ids, unsigned* count,
                         const RogueOfferState* prior)
{
    unsigned i,j,fresh=0,index;
    RogueOfferId chosen;
    if(!*count) {
        RogueOfferRef gold={ROGUE_OFFER_GOLD,10};
        return RogueOffer_Encode(gold);
    }
    /* Partition without changing stable IDs. Prefer absent previous cards;
     * a donor with only one eligible move may repeat that move. */
    for(i=0;i<*count;++i) {
        int old=0;
        if(prior) for(j=0;j<3;++j)
            if(ids[i]==RogueOffer_Encode(RogueOffer_Decode(prior->ids[j]))) old=1;
        if(!old) { chosen=ids[fresh]; ids[fresh++]=ids[i]; ids[i]=chosen; }
    }
    {
        unsigned limit=fresh ? fresh : *count;
        RogueOfferRef ref=RogueOffer_Decode(ids[0]);
        if(ref.kind==ROGUE_OFFER_UPGRADE) {
            static const unsigned weights[3]={65,30,5};
            unsigned counts[3]={0,0,0},total=0,tiers=0,tier=0,roll;
            for(i=0;i<limit;++i) counts[RogueUpgrade_Find(RogueOffer_Decode(ids[i]).stable_id)->rarity]++;
            for(i=0;i<3;++i) if(counts[i]) { total+=weights[i]; tiers++; tier=i; }
            /* Renormalize over available rarities. A single available tier
             * consumes no extra RNG draw, preserving all-common behavior. */
            if(tiers>1) {
                roll=RogueRng_Bounded(rng,total);
                for(tier=0;tier<3;++tier) if(counts[tier]) {
                    if(roll<weights[tier]) break;
                    roll-=weights[tier];
                }
            }
            roll=RogueRng_Bounded(rng,counts[tier]);
            for(index=0;index<limit;++index) {
                if(RogueUpgrade_Find(RogueOffer_Decode(ids[index]).stable_id)->rarity!=tier) continue;
                if(!roll) break;
                roll--;
            }
        } else index=RogueRng_Bounded(rng,limit);
    }
    chosen=ids[index]; ids[index]=ids[--*count];
    return chosen;
}
