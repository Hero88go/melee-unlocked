#include "camp_vendor.h"
#include "offers.h"
#include "balance.h"
#include <string.h>

int RogueCamp_Active(const RogueRun* run)
{
    return run && run->floor<ROGUE_FLOORS && (run->phase==ROGUE_SHOP || run->phase==ROGUE_REST) &&
        run->camp.active && !run->camp.exit_committed &&
        run->camp.run_id==run->run_id && run->camp.node_id &&
        run->camp.node_id==(run->current.node_id ? run->current.node_id :
            1+run->floor*2+run->selected_route[run->floor]) &&
        run->camp.kind==(unsigned)(run->phase==ROGUE_SHOP ? ROGUE_NODE_SHOP : ROGUE_NODE_REST);
}
int RogueCamp_ValidateInventory(const RogueRun* run)
{
    unsigned i,j;
    if(!run) return 0;
    if(!run->camp.active) return 1;
    if(!RogueCamp_Active(run)) return 0;
    for(i=0;i<3;++i) if(!run->camp.general.sold[i]) {
        RogueOfferId id=run->camp.general.ids[i];
        RogueOfferRef ref=RogueOffer_Decode(id);
        if(!RogueOfferId_Valid(id) || (ref.kind!=ROGUE_OFFER_NONE &&
            ref.kind!=ROGUE_OFFER_UPGRADE && ref.kind!=ROGUE_OFFER_SPECIAL) ||
            run->camp.general_prices[i]!=RogueOffer_Price(run,id)) return 0;
    }
    for(i=0;i<ROGUE_AERIAL_SLOTS;++i) for(j=0;j<3;++j) {
        RogueOfferId id=run->camp.aerial[i].ids[j];
        RogueOfferRef ref=RogueOffer_Decode(id);
        if(!RogueOfferId_Valid(id) || (ref.kind!=ROGUE_OFFER_NONE &&
           (ref.kind!=ROGUE_OFFER_AERIAL || RogueAerial_Find(ref.stable_id)->slot!=i))) return 0;
        if(!run->camp.aerial[i].sold[j] &&
           run->camp.aerial_prices[i][j]!=RogueOffer_Price(run,id)) return 0;
    }
    return 1;
}
static unsigned candidates(const RogueRun* run, const RogueOfferState* state,
                           unsigned kind, unsigned aerial_slot, RogueOfferId* ids)
{
    unsigned i,j,count=0;
    for(i=0;i<ROGUE_OFFERS;++i) {
        RogueOfferId id=RogueOffer_At(i);
        RogueOfferRef ref=RogueOffer_Decode(id);
        if(ref.kind!=kind || !RogueOfferRef_Eligible(run,ref)) continue;
        if(kind==ROGUE_OFFER_AERIAL && RogueAerial_Find(ref.stable_id)->slot!=aerial_slot) continue;
        for(j=0;j<3;++j) if(state->sold[j] &&
            RogueOffer_Encode(RogueOffer_Decode(state->ids[j]))==id) break;
        if(j==3) ids[count++]=id;
    }
    return count;
}
void RogueCamp_GeneralOffers(RogueRun* run, RogueOfferState* state, RogueRng* rng, int reroll)
{
    RogueOfferId ordinary[ROGUE_OFFERS],special[ROGUE_OFFERS];
    RogueOfferState prior=*state;
    unsigned nc=candidates(run,state,ROGUE_OFFER_UPGRADE,0,ordinary);
    unsigned ns=candidates(run,state,ROGUE_OFFER_SPECIAL,0,special),i;
    for(i=0;i<3;++i) if(!state->sold[i]) {
        if(!nc && !ns) state->ids[i]=0;
        else if(!ns || (nc && RogueRng_Bounded(rng,4)<3))
            state->ids[i]=RogueOffer_Pick(rng,ordinary,&nc,reroll ? &prior : 0);
        else state->ids[i]=RogueOffer_Pick(rng,special,&ns,reroll ? &prior : 0);
    }
}
static void aerial_offers(RogueRun* run, unsigned slot, int reroll)
{
    RogueOfferState* state=&run->camp.aerial[slot];
    RogueOfferState prior=*state;
    RogueOfferId ids[ROGUE_OFFERS];
    RogueRng rng;
    unsigned count=candidates(run,state,ROGUE_OFFER_AERIAL,slot,ids),i;
    /* A local bank stream cannot perturb general-vendor, reward or route RNG.
     * Generation is persisted and advances only on a paid refresh. */
    RogueRng_Derive(&rng,run->seed^((RogueSeed)state->generation<<32),
        ROGUE_STREAM_AERIAL_VENDOR,1,run->camp.node_id*ROGUE_AERIAL_SLOTS+slot);
    for(i=0;i<3;++i) if(!state->sold[i])
        state->ids[i]=count ? RogueOffer_Pick(&rng,ids,&count,reroll ? &prior : 0) : 0;
}
void RogueCamp_Prices(RogueRun* run)
{
    unsigned i,j;
    for(i=0;i<3;++i) if(!run->camp.general.sold[i])
        run->camp.general_prices[i]=RogueOffer_Price(run,run->camp.general.ids[i]);
    for(i=0;i<ROGUE_AERIAL_SLOTS;++i) for(j=0;j<3;++j)
        if(!run->camp.aerial[i].sold[j])
            run->camp.aerial_prices[i][j]=RogueOffer_Price(run,run->camp.aerial[i].ids[j]);
}
void RogueCamp_PrepareAerials(RogueRun* run)
{
    unsigned slot;
    for(slot=0;slot<ROGUE_AERIAL_SLOTS;++slot) aerial_offers(run,slot,0);
    RogueCamp_Prices(run);
}
const RogueOfferState* RogueRun_AerialOffers(const RogueRun* run, unsigned slot)
{
    return RogueCamp_Active(run) && slot<ROGUE_AERIAL_SLOTS ? &run->camp.aerial[slot] : 0;
}
unsigned RogueRun_AerialRerollCost(const RogueRun* run)
{
    unsigned uses=0,i;
    if(!run) return ~0U;
    for(i=0;i<ROGUE_AERIAL_SLOTS;++i) {
        if(uses>~0U-run->camp.aerial[i].rerolls) return ~0U;
        uses+=run->camp.aerial[i].rerolls;
    }
    if(uses>(~0U-ROGUE_SHOP_REROLL_BASE)/ROGUE_SHOP_REROLL_STEP) return ~0U;
    return ROGUE_SHOP_REROLL_BASE+uses*ROGUE_SHOP_REROLL_STEP;
}
int RogueRun_RerollAerial(RogueRun* run, unsigned slot)
{
    RogueOfferState* state;
    RogueHistoryEvent* event;
    unsigned cost,i;
    if(!RogueRun_AerialOffers(run,slot) || !ROGUE_ENABLE_AERIALS) return 0;
    state=&run->camp.aerial[slot]; cost=RogueRun_AerialRerollCost(run);
    if(run->gold<cost || state->claimed || state->generation==~0U) return 0;
    for(i=0;i<3;++i) if(!state->sold[i]) break;
    if(i==3) return 0;
    event=&run->history[run->history_count%ROGUE_HISTORY];
    memset(event,0,sizeof(*event));
    event->kind=11; event->floor=run->floor; event->cost=cost;
    memcpy(event->before,state->ids,sizeof(event->before));
    state->generation++; state->rerolls++;
    aerial_offers(run,slot,1); RogueCamp_Prices(run);
    run->gold-=cost; run->gold_spent+=cost; run->reroll_gold_spent+=cost; run->rerolls_used++;
    /* This history kind uses generation for filter context; each inventory
     * persists its actual generation separately. */
    event->generation=slot;
    memcpy(event->after,state->ids,sizeof(event->after)); run->history_count++;
    return 1;
}
int RogueRun_BuyAerial(RogueRun* run, unsigned slot, unsigned card)
{
    RogueOfferState* state;
    RogueOfferRef ref;
    RogueHistoryEvent* event;
    unsigned price;
    if(!RogueRun_AerialOffers(run,slot) || card>=3) return 0;
    state=&run->camp.aerial[slot]; ref=RogueOffer_Decode(state->ids[card]);
    if(state->sold[card] || state->claimed || ref.kind!=ROGUE_OFFER_AERIAL ||
       !RogueOfferRef_Eligible(run,ref) || RogueAerial_Find(ref.stable_id)->slot!=slot) return 0;
    price=run->camp.aerial_prices[slot][card];
    if(!price || price!=RogueOfferRef_Price(run,ref) || run->gold<price) return 0;
    /* Pure selection mutation: no fighter/resource callbacks can run here. */
    run->aerials[slot]=ref.stable_id; state->sold[card]=1;
    run->camp.pending_aerial_slots|=1U<<slot;
    run->gold-=price; run->gold_spent+=price;
    event=&run->history[run->history_count%ROGUE_HISTORY];
    memset(event,0,sizeof(*event));
    event->kind=10; event->floor=run->floor; event->cost=price;
    event->generation=run->history_count++;
    event->after[0]=RogueOffer_Encode(ref); event->after[1]=card; event->after[2]=slot;
    return 1;
}
