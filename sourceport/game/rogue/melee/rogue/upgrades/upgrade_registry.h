#ifndef ROGUE_UPGRADE_REGISTRY_H
#define ROGUE_UPGRADE_REGISTRY_H
#include "../core/catalog_counts.h"
enum RogueUpgradeRarity { ROGUE_RARITY_COMMON, ROGUE_RARITY_UNCOMMON, ROGUE_RARITY_RARE };
enum RogueUpgradeChoice { ROGUE_CHOICE_NONE, ROGUE_CHOICE_SPECIAL_SLOT };
typedef struct RogueUpgradeDef {
    const char* key;
    const char* name;
    const char* description;
    unsigned price, max_stacks, stat, amount, id, rarity, choice;
} RogueUpgradeDef;
extern const RogueUpgradeDef rogue_upgrades[ROGUE_UPGRADES];
const RogueUpgradeDef* RogueUpgrade_Find(unsigned id);
unsigned RogueUpgrade_StackId(unsigned index);
unsigned RogueUpgrade_StackIndex(unsigned id);
#endif
