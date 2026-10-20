#ifndef ROGUE_RUN_H
#define ROGUE_RUN_H
#include "rng.h"
#include "event_route.h"
#include "offer_identity.h"
#include "../combat/aerials/aerial_catalog.h"
#include "features.h"
#include "catalog_counts.h"
#include "settlement.h"

#define ROGUE_FLOORS 15
#define ROGUE_RECIPES 15
#define ROGUE_HISTORY 256
enum RoguePhase { ROGUE_SELECT, ROGUE_REWARD, ROGUE_ROUTE, ROGUE_FIGHT,
                  ROGUE_SHOP, ROGUE_REST, ROGUE_DEAD, ROGUE_COMPLETE, ROGUE_EVENT };
enum RogueNodeType { ROGUE_NORMAL, ROGUE_ELITE, ROGUE_BOSS, ROGUE_NODE_SHOP, ROGUE_NODE_REST, ROGUE_NODE_EVENT };
typedef struct RogueOfferState {
    RogueOfferId ids[3];
    unsigned sold[3], generation, rerolls, claimed;
} RogueOfferState;

enum RogueRewardKind { ROGUE_REWARD_NONE, ROGUE_REWARD_STARTER,
    ROGUE_REWARD_COMBAT, ROGUE_REWARD_SKILL, ROGUE_REWARD_GAUNTLET,
    ROGUE_REWARD_LEGACY };
#define ROGUE_REWARD_DONORS 3
typedef struct RogueRewardSource {
    RogueSeed run_id;
    unsigned kind, node_id, receipt_id, policy_version, won;
    unsigned provenance_verified, donor_count, donors[ROGUE_REWARD_DONORS];
    unsigned generated, claimed;
} RogueRewardSource;

/* Sole owner of vendor inventories. No native actor or resource pointers. */
typedef struct RogueCampVisitState {
    RogueSeed run_id;
    unsigned node_id, kind, active, heal_used, exit_committed;
    RogueOfferState general, aerial[ROGUE_AERIAL_SLOTS];
    unsigned general_prices[3], aerial_prices[ROGUE_AERIAL_SLOTS][3];
    unsigned pending_special_slots, pending_aerial_slots;
} RogueCampVisitState;
typedef struct RogueEncounter {
    unsigned recipe, stage, enemy_count, fighters[3], stocks[3];
    unsigned tags, damage, defense, speed, scale, starting_percent;
} RogueEncounter;
typedef struct RogueNodeDesc {
    unsigned kind, node_id, ruleset_version;
    union { RogueEncounter combat; RogueEventDesc event; } payload;
} RogueNodeDesc;

#define ROGUE_SNAPSHOT_V4_BYTES 10936U
#define ROGUE_SNAPSHOT_V5_BYTES 10948U
#define ROGUE_SNAPSHOT_VERSION 16U
#define ROGUE_SNAPSHOT_MAX_BYTES 131072U
#define ROGUE_CATALOG_VERSION 1U
/* Zero names the inspected pre-expansion accounting/draft policy. */
#define ROGUE_SCORE_POLICY_VERSION ROGUE_SCORE_RULES_VERSION
#define ROGUE_REWARD_POLICY_VERSION 1U
#define ROGUE_EVENT_POLICY_VERSION 1U

typedef struct RogueHistoryEvent {
    unsigned kind, floor, cost, generation, before[3], after[3];
} RogueHistoryEvent;
typedef struct RogueRun {
    RogueSeed seed;
    unsigned version, character, act, floor, phase, gold;
    RoguePoints score;
    unsigned route[ROGUE_FLOORS][2], selected_route[ROGUE_FLOORS];
    unsigned stacks[ROGUE_UPGRADES], specials[4];
    unsigned aerials[ROGUE_AERIAL_SLOTS];
    unsigned recent_recipes[4], recent_stages[3], encounters_offered;
    unsigned gold_spent, rerolls_used, history_count, carried_percent;
    unsigned combat_nodes_won, death_reason, reroll_gold_spent;
    RoguePoints native_score;
    RogueRng route_rng, encounter_rng, reward_rng, shop_rng;
    RogueOfferState reward;
    RogueCampVisitState camp;
    RogueNodeDesc preview[2], current;
    RogueHistoryEvent history[ROGUE_HISTORY];
    unsigned starting_stocks, difficulty, costume;
    unsigned ruleset_version, events_planned;
    unsigned nodes_resolved, events_attempted, events_completed, submatches_played;
    unsigned legacy_score_unverified, legacy_active_incompatible;
    RogueRng proc_rng;
    RogueEventOpportunity event_opportunities[3];
    RogueSeed run_id;
    unsigned catalog_version, score_policy_version, event_policy_version;
    unsigned reward_policy_version, legacy_identity;
    RogueRewardSource reward_source;
    RogueScoreTotals score_totals;
    RoguePoints legacy_score_base;
    unsigned receipt_count, settled_nodes;
    RogueScoreReceipt receipts[ROGUE_FLOORS];
    /* Zero is unset; 1..4 selects N/S/U/D independently of equipped donors. */
    unsigned upgrade_choices[ROGUE_UPGRADES];
} RogueRun;

typedef void (*RogueRunObserver)(const RogueRun*, unsigned event, unsigned a, unsigned b, unsigned c);
const RogueEncounter* RogueNode_Combat(const RogueNodeDesc* node);
void RogueRun_SetObserver(RogueRunObserver observer);
void RogueRun_Init(RogueRun* run, RogueSeed seed, unsigned character);
/* The deterministic initializer is also used by replay fixtures. Live attempts
 * must receive a separately allocated nonzero ID before any action. */
int RogueRun_SetAttemptId(RogueRun* run, RogueSeed run_id);
int RogueRun_PlanEvents(RogueRun* run, const RogueEventAvailability available[7],
                        const unsigned combat_stages[6]);
int RogueRun_Configure(RogueRun* run, unsigned stocks, unsigned difficulty, unsigned costume);
unsigned RogueRun_CpuLevel(const RogueRun* run);
int RogueRun_ChooseUpgrade(RogueRun* run, unsigned slot);
int RogueRun_ChooseRoute(RogueRun* run, unsigned slot);
int RogueRun_SelectEventKit(RogueRun* run, unsigned side, unsigned native_moves);
int RogueRun_MatchEnd(RogueRun* run, int won, unsigned native_score);
enum RogueSettlementStatus { ROGUE_SETTLEMENT_INVALID, ROGUE_SETTLEMENT_COMMITTED,
    ROGUE_SETTLEMENT_ALREADY_COMMITTED };
unsigned RogueRun_Resolve(RogueRun* run, const RogueNodeResult* result);
int RogueRun_HealCamp(RogueRun* run);
int RogueRun_ExitCamp(RogueRun* run);
int RogueRun_LeaveService(RogueRun* run);
void RogueRun_Reward(RogueRun* run);
void RogueRun_Shop(RogueRun* run);
unsigned RogueRun_RerollCost(const RogueRun* run, int shop);
int RogueRun_Reroll(RogueRun* run, int shop);
int RogueRun_Buy(RogueRun* run, unsigned slot);
void RogueEncounter_Generate(RogueRun* run, unsigned tier, RogueEncounter* out);
unsigned RogueEncounter_Budget(unsigned act, unsigned floor, unsigned tier);
unsigned RogueEncounter_Threat(const RogueEncounter* encounter);
unsigned RogueRun_Serialize(const RogueRun* run, unsigned char* out, unsigned capacity);
int RogueRun_Deserialize(RogueRun* run, const unsigned char* data, unsigned size);
enum RogueImportResult { ROGUE_IMPORT_INVALID, ROGUE_IMPORT_READY,
                         ROGUE_IMPORT_INCOMPATIBLE_ACTIVE };
/* Incompatible results preserve value data for an explicit history/setup
 * choice. Deserialize rejects those results without changing its destination. */
unsigned RogueRun_ImportSnapshot(RogueRun* archive, const unsigned char* data, unsigned size);
#endif
