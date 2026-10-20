#include "settlement.h"
#include "event_metrics.h"
#include "balance.h"
#include <string.h>

int RoguePoints_Add(RoguePoints a, RoguePoints b, RoguePoints* out)
{
    if(!out || (b>0 && a>ROGUE_POINTS_MAX-b) || (b<0 && a<ROGUE_POINTS_MIN-b)) return 0;
    *out=a+b; return 1;
}
int RoguePoints_Subtract(RoguePoints a, RoguePoints b, RoguePoints* out)
{
    if(!out || (b<0 && a>ROGUE_POINTS_MAX+b) || (b>0 && a<ROGUE_POINTS_MIN+b)) return 0;
    *out=a-b; return 1;
}
void RoguePoints_Format(RoguePoints value, char out[24])
{
    char digits[20];
    unsigned n=0,pos=0;
    RogueSeed magnitude=value<0 ? 0-(RogueSeed)value : (RogueSeed)value;
    do { digits[n++]=(char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
    if(value<0) out[pos++]='-';
    while(n) out[pos++]=digits[--n];
    out[pos]=0;
}
static int percent(RoguePoints value, unsigned rate, RoguePoints* out)
{
    RoguePoints whole=value/100, tail=(value%100)*rate/100;
    if(value<0 || rate>100 || (rate && whole>ROGUE_POINTS_MAX/rate)) return 0;
    return RoguePoints_Add(whole*rate,tail,out);
}
int RogueSettlement_Calculate(const RogueNodeResult* r,
    const RogueScoreModifiers* m, RogueScoreReceipt* out)
{
    RogueScoreReceipt receipt;
    RoguePoints sum=0, delta, raw, bonus, p;
    unsigned i,j,spendable,base=0;
    static const unsigned caps[5]={ROGUE_PERFORMANCE_CAP_NORMAL,ROGUE_PERFORMANCE_CAP_ELITE,
        ROGUE_PERFORMANCE_CAP_BOSS,ROGUE_PERFORMANCE_CAP_SKILL,ROGUE_PERFORMANCE_CAP_GAUNTLET};
    if(!r || !m || !out || !r->run_id || !r->node_id || r->node_id>30 ||
        r->submatch_id || !r->submatch_count || r->submatch_count>3 ||
        r->kind>ROGUE_RESULT_GAUNTLET || r->outcome<ROGUE_RESULT_WIN ||
        r->outcome>ROGUE_RESULT_QUIT || r->measured!=1 || r->debug_forced>1 ||
        r->native_format>ROGUE_NATIVE_CUMULATIVE || r->display_capped>1 ||
        r->row_count>ROGUE_NATIVE_ROWS || r->objective_points<0 || r->upgrade_style_points<0 ||
        r->carried_percent>999 || r->provenance_verified>1 || r->donor_count>3 || (!r->provenance_verified && r->donor_count) ||
        m->showmanship_rank>ROGUE_SHOWMANSHIP_MAX_RANK || m->bounty_rank>ROGUE_BOUNTY_MAX_RANK ||
        m->patron_rank>1) return 0;
    if(!RogueEvent_HomeRunResultValid(r) || !RogueTarget_ResultValid(r) || !RogueRace_ResultValid(r)) return 0;
    if(r->kind!=ROGUE_RESULT_SKILL && r->objective_points) return 0;
    if(r->kind!=ROGUE_RESULT_GAUNTLET && r->submatch_count!=1) return 0;
    if(r->kind==ROGUE_RESULT_GAUNTLET && r->outcome==ROGUE_RESULT_WIN && r->submatch_count!=3) return 0;
    if(r->kind==ROGUE_RESULT_SKILL && r->donor_count) return 0;
    for(i=0;i<r->donor_count;++i) {
        if(r->donors[i]>=26) return 0;
        for(j=0;j<i;++j) if(r->donors[i]==r->donors[j]) return 0;
    }
    if(r->native_format==ROGUE_NATIVE_CUMULATIVE) {
        if(!RoguePoints_Subtract(r->native_output,r->native_input,&delta)) return 0;
    } else {
        if(r->native_input) return 0;
        delta=r->native_output;
    }
    if(delta!=r->native_stage_delta ||
        r->display_capped!=(unsigned)(r->native_display_output!=r->native_output)) return 0;
    memset(&receipt,0,sizeof(receipt));
    receipt.result=*r; receipt.modifiers=*m; receipt.policy_version=ROGUE_SCORE_RULES_VERSION;
    memset(receipt.result.rows+r->row_count,0,(ROGUE_NATIVE_ROWS-r->row_count)*sizeof(r->rows[0]));
    for(i=r->donor_count;i<3;++i) receipt.result.donors[i]=0;
    for(i=0;i<r->row_count;++i) {
        const RogueNativeRow* row=&r->rows[i];
        if(!row->occurrences || row->source>ROGUE_SOURCE_COMBAT_BONUS ||
            row->exclusion>ROGUE_SCORE_UNCLASSIFIED) return 0;
        if(row->source==ROGUE_SOURCE_COMBAT_BONUS && r->kind==ROGUE_RESULT_SKILL) return 0;
        for(j=0;j<i;++j) if(row->native_id==r->rows[j].native_id) return 0;
        if((row->source==ROGUE_SOURCE_UNKNOWN)!=(row->exclusion==ROGUE_SCORE_UNCLASSIFIED) ||
            (row->source==ROGUE_SOURCE_CAMPAIGN)!=(row->exclusion==ROGUE_SCORE_CAMPAIGN)) return 0;
        if(row->exclusion==ROGUE_SCORE_DUPLICATE_OBJECTIVE &&
            (r->kind!=ROGUE_RESULT_SKILL ||
             (row->source!=ROGUE_SOURCE_OBJECTIVE && row->source!=ROGUE_SOURCE_TIME))) return 0;
        if(!RoguePoints_Add(sum,row->points,&sum)) return 0;
        if(row->exclusion!=ROGUE_SCORE_DUPLICATE_OBJECTIVE &&
            !(r->kind==ROGUE_RESULT_SKILL && row->exclusion==ROGUE_SCORE_CAMPAIGN))
            if(!RoguePoints_Add(receipt.native_counted_delta,row->points,&receipt.native_counted_delta)) return 0;
        if(row->exclusion==ROGUE_SCORE_KEEP)
            if(!RoguePoints_Add(receipt.eligible_native_points,row->points,&receipt.eligible_native_points)) return 0;
    }
    if(sum!=delta) return 0;
    if(r->outcome==ROGUE_RESULT_WIN) {
        if(r->kind==ROGUE_RESULT_NORMAL) { receipt.rogue_clear_points=ROGUE_SCORE_NORMAL_CLEAR; base=ROGUE_NORMAL_REWARD; }
        if(r->kind==ROGUE_RESULT_ELITE || r->kind==ROGUE_RESULT_GAUNTLET) {
            receipt.rogue_clear_points=ROGUE_SCORE_ELITE_CLEAR; base=ROGUE_ELITE_REWARD;
        }
        if(r->kind==ROGUE_RESULT_BOSS) { receipt.rogue_clear_points=ROGUE_SCORE_BOSS_CLEAR; base=ROGUE_BOSS_REWARD; }
        if(r->kind==ROGUE_RESULT_SKILL) base=ROGUE_SKILL_COMPLETION_GOLD;
        else base+=(base/10)*m->bounty_rank;
    }
    if(!RoguePoints_Add(receipt.native_counted_delta,r->objective_points,&raw) ||
        !RoguePoints_Add(raw,receipt.rogue_clear_points,&raw) ||
        !RoguePoints_Add(raw,r->upgrade_style_points,&raw) ||
        !percent(raw>0?raw:0,m->showmanship_rank*ROGUE_SHOWMANSHIP_PERCENT_PER_RANK,&bonus) ||
        !RoguePoints_Add(raw,bonus,&receipt.run_score_delta)) return 0;
    receipt.showmanship_bonus=bonus;
    if(!RoguePoints_Add(receipt.eligible_native_points,r->objective_points,&p) ||
        !RoguePoints_Add(p,r->upgrade_style_points,&p)) return 0;
    receipt.performance_basis=p>0?p:0;
    if(!percent(receipt.performance_basis,m->showmanship_rank*ROGUE_SHOWMANSHIP_PERCENT_PER_RANK,&bonus) ||
        !RoguePoints_Add(receipt.performance_basis,bonus,&receipt.boosted_performance)) return 0;
    receipt.uncapped_performance_gold=receipt.boosted_performance/ROGUE_POINTS_PER_GOLD;
    if(!percent(receipt.uncapped_performance_gold,m->patron_rank*ROGUE_PATRON_PERCENT,&bonus) ||
        !RoguePoints_Add(receipt.uncapped_performance_gold,bonus,&receipt.adjusted_performance_gold)) return 0;
    receipt.performance_cap=caps[r->kind]; receipt.effect_cap=ROGUE_EFFECT_GOLD_CAP;
    spendable=!r->debug_forced && r->outcome!=ROGUE_RESULT_QUIT &&
        (r->kind==ROGUE_RESULT_SKILL || r->outcome==ROGUE_RESULT_WIN);
    if(spendable) {
        receipt.base_gold=base;
        receipt.performance_gold=receipt.adjusted_performance_gold>receipt.performance_cap ?
            receipt.performance_cap : (unsigned)receipt.adjusted_performance_gold;
        receipt.effect_gold=r->effect_gold>receipt.effect_cap?receipt.effect_cap:r->effect_gold;
    }
    receipt.total_gold=receipt.base_gold+receipt.performance_gold+receipt.effect_gold;
    *out=receipt; return 1;
}
