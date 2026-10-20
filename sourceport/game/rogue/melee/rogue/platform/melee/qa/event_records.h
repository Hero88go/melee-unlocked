#ifndef ROGUE_QA_EVENT_RECORDS_H
#define ROGUE_QA_EVENT_RECORDS_H

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <dolphin/os.h>

/* Named persistent fields only: no padding, volatile state, or restoration. */
#define ROGUE_EVENT_RECORD_FIELDS (8 + TY_TROPHY_COUNT)
#define ROGUE_EVENT_STAT_FIELDS (6 + SELKIND_COUNT * (SELKIND_COUNT + 19))
static unsigned event_statistics_before[ROGUE_EVENT_STAT_FIELDS];

/* Explicit named fields: no padding comparisons and no saved-data writes. */
static void RogueEventStatistics_Read(unsigned* out)
{
    GmSaveData* save=&gmMainLib_GetCardData()->save_data;
    unsigned i,j,n=0;
    out[n++]=save->time_matches; out[n++]=save->stock_matches;
    out[n++]=save->coin_matches; out[n++]=save->bonus_matches;
    out[n++]=save->stamina_matches; out[n++]=save->match_resets;
    for(i=0;i<SELKIND_COUNT;++i) {
        struct FighterData* data=&save->x1F2C[i];
        for(j=0;j<SELKIND_COUNT;++j) out[n++]=data->fighter_kos[j];
        out[n++]=data->sd_count;
        out[n++]=data->attacks_hit; out[n++]=data->attacks_total;
        out[n++]=(unsigned)data->damage_dealt; out[n++]=(unsigned)data->damage_taken;
        out[n++]=(unsigned)data->damage_recovered; out[n++]=data->peak_damage;
        out[n++]=data->match_count; out[n++]=data->victories; out[n++]=data->losses;
        out[n++]=data->play_time; out[n++]=data->total_player_count;
        out[n++]=(unsigned)data->walk_distance; out[n++]=(unsigned)data->run_distance;
        out[n++]=(unsigned)data->fall_distance; out[n++]=(unsigned)data->peak_height;
        out[n++]=(unsigned)data->coins_collected; out[n++]=(unsigned)data->coins_swiped;
        out[n++]=(unsigned)data->coins_lost;
    }
}
static void RogueEventRecords_Values(unsigned* out)
{
    GmSaveData* save=&gmMainLib_GetCardData()->save_data;
    unsigned i;
    out[0]=save->unlocked_characters;
    out[1]=save->x186A;
    out[2]=save->x186C;
    out[3]=(unsigned)save->unk_8.x1C;
    out[4]=(unsigned)save->unk_28.x4;
    out[5]=(unsigned)save->unk_30.x8;
    out[6]=(unsigned)save->trophy_count;
    out[7]=save->trophy_category_flags;
    for(i=0;i<TY_TROPHY_COUNT;++i) out[8+i]=save->trophy_flags[i];
}

static void RogueEventRecords_Read(unsigned* out)
{
    RogueEventRecords_Values(out);
    RogueEventStatistics_Read(event_statistics_before);
}

static void RogueEventRecords_Check(const unsigned* before,const char* event)
{
    unsigned after[ROGUE_EVENT_RECORD_FIELDS],i,changed=0;
    RogueEventRecords_Values(after);
    for(i=0;i<ROGUE_EVENT_RECORD_FIELDS;++i) {
        if(before[i]!=after[i]) {
            ++changed;
            OSReport("[rogue] event_record_change event=%s field=%u before=%u after=%u\n",
                event,i,before[i],after[i]);
        }
    }
    OSReport("[rogue] event_records event=%s unlocks=3 completion=3 trophies=%u checked=%u changed=%u\n",
        event,2+TY_TROPHY_COUNT,ROGUE_EVENT_RECORD_FIELDS,changed);
    {
        unsigned statistics[ROGUE_EVENT_STAT_FIELDS];
        changed=0;
        RogueEventStatistics_Read(statistics);
        for(i=0;i<ROGUE_EVENT_STAT_FIELDS;++i) {
            if(event_statistics_before[i]!=statistics[i]) {
                ++changed;
                OSReport("[rogue] event_stat_change event=%s field=%u before=%u after=%u\n",
                    event,i,event_statistics_before[i],statistics[i]);
            }
        }
        OSReport("[rogue] event_statistics event=%s match_counters=6 fighters=%u checked=%u changed=%u\n",
            event,SELKIND_COUNT,ROGUE_EVENT_STAT_FIELDS,changed);
    }
}
#endif
