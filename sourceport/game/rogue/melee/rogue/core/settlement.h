#ifndef ROGUE_SETTLEMENT_H
#define ROGUE_SETTLEMENT_H
#include "rng.h"
#include "score_rules.h"

typedef signed long long RoguePoints;
#define ROGUE_POINTS_MAX ((RoguePoints)0x7fffffffffffffffLL)
#define ROGUE_POINTS_MIN (-ROGUE_POINTS_MAX-1)
#define ROGUE_NATIVE_ROWS 261U
enum RogueResultKind { ROGUE_RESULT_NORMAL, ROGUE_RESULT_ELITE,
    ROGUE_RESULT_BOSS, ROGUE_RESULT_SKILL, ROGUE_RESULT_GAUNTLET };
enum RogueResultOutcome { ROGUE_RESULT_WIN=1, ROGUE_RESULT_FAILURE, ROGUE_RESULT_QUIT };
enum RogueEventEnd { ROGUE_EVENT_END_UNKNOWN, ROGUE_EVENT_END_COMPLETE,
    ROGUE_EVENT_END_TIMEOUT, ROGUE_EVENT_END_FALL, ROGUE_EVENT_END_SKIP,
    ROGUE_EVENT_END_INTERRUPTED, ROGUE_EVENT_END_QUIT, ROGUE_EVENT_END_NO_DISTANCE };

enum RogueNativeFormat { ROGUE_NATIVE_DELTA, ROGUE_NATIVE_CUMULATIVE };
enum RogueScoreSource { ROGUE_SOURCE_UNKNOWN, ROGUE_SOURCE_STYLE,
    ROGUE_SOURCE_OBJECTIVE, ROGUE_SOURCE_TIME, ROGUE_SOURCE_CAMPAIGN,
    ROGUE_SOURCE_COMBAT_BONUS };
enum RogueScoreExclusion { ROGUE_SCORE_KEEP, ROGUE_SCORE_DUPLICATE_OBJECTIVE,
    ROGUE_SCORE_CAMPAIGN, ROGUE_SCORE_UNCLASSIFIED };
typedef struct RogueNativeRow {
    unsigned native_id, occurrences, source, exclusion;
    RoguePoints points;
} RogueNativeRow;

enum RogueDistanceDisplay { ROGUE_DISTANCE_TENTHS_FOOT=1, ROGUE_DISTANCE_DECIMETERS };
typedef struct RogueHomeRunMetric {
    unsigned present, conversion_version, native_mode, display_unit, display_value;
    RoguePoints raw_centimeters, distance_mm;
} RogueHomeRunMetric;

typedef struct RogueTargetMetric {
    unsigned present, base_character, native_stage, total, remaining;
    unsigned remaining_frames, native_goal, native_outcome, native_kit_assist;
} RogueTargetMetric;
typedef struct RogueRaceMetric {
    unsigned present, base_character, native_stage, native_kit_assist;
    unsigned limit_frames, remaining_frames, checkpoints, native_goal, native_outcome;
    unsigned native_goal_score;
    RoguePoints checkpoint_points, goal_points, time_points;
} RogueRaceMetric;
typedef struct RogueNodeResult {
    RogueSeed run_id;
    unsigned node_id, submatch_id, submatch_count, kind, outcome, measured, debug_forced;
    unsigned native_format, display_capped, row_count;
    RoguePoints native_input, native_output, native_stage_delta, native_display_output;
    RoguePoints objective_points, upgrade_style_points;
    unsigned effect_gold, carried_percent, provenance_verified, donor_count, donors[3];
    RogueNativeRow rows[ROGUE_NATIVE_ROWS];
    RogueHomeRunMetric home_run;
    unsigned event_end;
    RogueTargetMetric targets;
    RogueRaceMetric race;
} RogueNodeResult;
typedef struct RogueScoreModifiers {
    unsigned showmanship_rank, bounty_rank, patron_rank;
} RogueScoreModifiers;
typedef struct RogueScoreReceipt {
    RogueNodeResult result;
    RogueScoreModifiers modifiers;
    unsigned policy_version, committed;
    RoguePoints native_counted_delta, eligible_native_points, rogue_clear_points;
    RoguePoints showmanship_bonus, run_score_delta, performance_basis;
    RoguePoints boosted_performance, uncapped_performance_gold, adjusted_performance_gold;
    unsigned performance_cap, effect_cap, base_gold, performance_gold, effect_gold, total_gold;
} RogueScoreReceipt;
typedef struct RogueScoreTotals {
    RoguePoints native_raw, native_counted, objective, rogue_added;
    unsigned clear_gold, performance_gold, effect_gold;
} RogueScoreTotals;

int RoguePoints_Add(RoguePoints a, RoguePoints b, RoguePoints* out);
int RoguePoints_Subtract(RoguePoints a, RoguePoints b, RoguePoints* out);
void RoguePoints_Format(RoguePoints value, char out[24]);
/* All-or-nothing pure calculation. Invalid/unmeasured inputs leave out intact. */
int RogueSettlement_Calculate(const RogueNodeResult* result,
    const RogueScoreModifiers* modifiers, RogueScoreReceipt* out);
#endif
