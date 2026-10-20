#include "reward_policy.h"
#include "offers.h"
#include "../upgrades/upgrade_registry.h"
#include <string.h>

int RogueReward_Entitled(const RogueRun* run)
{
    const RogueRewardSource* source;
    const RogueNodeResult* result;
    const RogueScoreReceipt* receipt;
    unsigned kind;
    if(!run || run->floor>=ROGUE_FLOORS || run->phase==ROGUE_DEAD ||
       run->phase==ROGUE_COMPLETE || run->camp.active) return 0;
    source=&run->reward_source;
    if(source->claimed) return 0;
    if(source->run_id!=run->run_id || source->policy_version!=run->reward_policy_version)
        return 0;
    if(source->kind==ROGUE_REWARD_STARTER)
        return !run->floor && !run->receipt_count && !source->node_id &&
            !source->receipt_id && !source->donor_count;
    if(source->kind==ROGUE_REWARD_LEGACY)
        return !source->donor_count && !source->provenance_verified;
    if(!source->receipt_id || source->receipt_id>ROGUE_FLOORS ||
       !source->node_id || source->node_id>30 ||
       source->receipt_id!=(source->node_id-1)/2+1 ||
       run->floor!=source->receipt_id) return 0;
    receipt=&run->receipts[source->receipt_id-1]; result=&receipt->result;
    if(!receipt->committed || !(run->settled_nodes&(1U<<(source->node_id-1))) ||
       result->run_id!=source->run_id || result->node_id!=source->node_id ||
       source->won!=(unsigned)(result->outcome==ROGUE_RESULT_WIN) ||
       source->provenance_verified!=result->provenance_verified ||
       source->donor_count!=result->donor_count ||
       source->donor_count>ROGUE_REWARD_DONORS ||
       memcmp(source->donors,result->donors,sizeof(source->donors))) return 0;
    kind=result->kind==ROGUE_RESULT_SKILL ? ROGUE_REWARD_SKILL :
        result->kind==ROGUE_RESULT_GAUNTLET ? ROGUE_REWARD_GAUNTLET : ROGUE_REWARD_COMBAT;
    if(source->kind!=kind || result->outcome==ROGUE_RESULT_QUIT) return 0;
    return kind==ROGUE_REWARD_SKILL || source->won;
}

int RogueReward_Eligible(const RogueRun* run, RogueOfferRef ref)
{
    unsigned i;
    const RogueSpecialDef* special;
    if(!RogueReward_Entitled(run)) return 0;
    if(ref.kind==ROGUE_OFFER_GOLD) return ref.stable_id==10;
    if(!RogueOfferRef_Eligible(run,ref)) return 0;
    if(ref.kind==ROGUE_OFFER_UPGRADE) return 1;
    if(ref.kind!=ROGUE_OFFER_SPECIAL || !run->reward_source.provenance_verified ||
       (run->reward_source.kind!=ROGUE_REWARD_COMBAT &&
        run->reward_source.kind!=ROGUE_REWARD_GAUNTLET)) return 0;
    special=RogueSpecial_Find(ref.stable_id);
    if(!special || special->slot>=4) return 0;
    for(i=0;i<run->reward_source.donor_count;++i)
        if(special->character==run->reward_source.donors[i]) return 1;
    return 0;
}

static unsigned pool(const RogueRun* run, unsigned kind, RogueOfferId* ids)
{
    unsigned i,count=0;
    for(i=0;i<ROGUE_OFFERS;++i) {
        RogueOfferId id=RogueOffer_At(i);
        RogueOfferRef ref=RogueOffer_Decode(id);
        if(ref.kind==kind && RogueReward_Eligible(run,ref)) ids[count++]=id;
    }
    return count;
}
int RogueReward_Generate(const RogueRun* run, RogueRng* rng,
                         const RogueOfferState* prior, RogueOfferState* out,
                         unsigned* fallback)
{
    RogueOfferId ordinary[ROGUE_OFFERS],specials[ROGUE_OFFERS];
    RogueOfferState next;
    unsigned ordinary_count,special_count,i,reason;
    if(!rng || !out || !RogueReward_Entitled(run)) return 0;
    ordinary_count=pool(run,ROGUE_OFFER_UPGRADE,ordinary);
    special_count=pool(run,ROGUE_OFFER_SPECIAL,specials);
    reason=special_count ? ROGUE_REWARD_SPECIAL_PRESENT :
        (run->reward_source.kind!=ROGUE_REWARD_COMBAT && run->reward_source.kind!=ROGUE_REWARD_GAUNTLET)
        ? ROGUE_REWARD_GENERIC_CONTEXT :
        (!run->reward_source.provenance_verified || !run->reward_source.donor_count)
        ? ROGUE_REWARD_NO_VERIFIED_DONOR : ROGUE_REWARD_NO_ELIGIBLE_SPECIAL;
    memset(&next,0,sizeof(next));
    if(prior) { next.generation=prior->generation; next.rerolls=prior->rerolls; }
    for(i=0;i<(special_count ? 2U : 3U);++i)
        next.ids[i]=RogueOffer_Pick(rng,ordinary,&ordinary_count,prior);
    if(special_count) next.ids[2]=RogueOffer_Pick(rng,specials,&special_count,prior);
    *out=next; if(fallback) *fallback=reason;
    return 1;
}
int RogueReward_Validate(const RogueRun* run, const RogueOfferState* draft)
{
    RogueOfferId unused[ROGUE_OFFERS];
    unsigned ordinary,specials,i,j,actual_specials=0,gold=0;
    if(!draft || draft->claimed || !RogueReward_Entitled(run) || !run->reward_source.generated) return 0;
    ordinary=pool(run,ROGUE_OFFER_UPGRADE,unused);
    specials=pool(run,ROGUE_OFFER_SPECIAL,unused);
    for(i=0;i<3;++i) {
        RogueOfferRef ref=RogueOffer_Decode(draft->ids[i]);
        if(draft->sold[i] || !RogueReward_Eligible(run,ref)) return 0;
        if(ref.kind==ROGUE_OFFER_SPECIAL) actual_specials++;
        if(ref.kind==ROGUE_OFFER_GOLD) gold++;
        else for(j=0;j<i;++j)
            if(RogueOffer_Encode(ref)==RogueOffer_Encode(RogueOffer_Decode(draft->ids[j]))) return 0;
    }
    i=specials ? 2U : 3U;
    return actual_specials==(unsigned)(specials!=0) && gold==(ordinary<i ? i-ordinary : 0);
}
