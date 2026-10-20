#include "upgrade_registry.h"
const RogueUpgradeDef rogue_upgrades[24] = {
    { "power", "Power", "Deal 8% more damage.", 35, 3, 0, 8, 1, 0, 0 },
    { "guard", "Thick Skin", "Take 8% less damage.", 40, 3, 1, 8, 2, 0, 0 },
    { "stride", "Long Stride", "Run 6% faster.", 45, 3, 2, 6, 3, 0, 0 },
    { "air", "Air Control", "Move 6% faster in the air.", 50, 3, 3, 6, 4, 0, 0 },
    { "jump", "High Jump", "Jump 5% higher.", 35, 3, 4, 5, 5, 0, 0 },
    { "shield", "Shield Core", "Increase shield capacity by 10%.", 40, 3, 5, 10, 6, 0, 0 },
    { "regen", "Shield Recovery", "Recover shield 8% faster.", 45, 3, 6, 8, 7, 0, 0 },
    { "weight", "Anchor", "Increase weight by 5%.", 50, 3, 7, 5, 8, 0, 0 },
    { "grip", "Strong Grip", "Deal 10% more standard throw damage.", 35, 3, 8, 10, 9, 0, 0 },
    { "reach", "Recovery Window", "Gain 5 frames of ledge invulnerability.", 40, 3, 9, 5, 10, 0, 0 },
    { "stock", "Second Wind", "Gain one stock for the next fight.", 45, 3, 10, 1, 11, 0, 0 },
    { "gold", "Bounty", "Earn 10% more combat gold.", 50, 3, 11, 10, 12, 0, 0 },
    { "score", "Showmanship", "Earn 10% more run score.", 35, 3, 12, 10, 13, 0, 0 },
    { "heal", "Campfire", "Recover 10% damage at rest.", 40, 3, 13, 10, 14, 0, 0 },
    { "launch", "Launcher", "Increase outgoing knockback by 5%.", 45, 3, 14, 5, 15, 0, 0 },
    { "poise", "Poise", "Reduce received knockback by 5%.", 50, 3, 15, 5, 16, 0, 0 },
    { "dash", "Quick Start", "Accelerate on the ground 6% faster.", 35, 3, 16, 6, 17, 0, 0 },
    { "fall", "Feather", "Fall 5% more slowly.", 40, 3, 17, 5, 18, 0, 0 },
    { "landing", "Soft Landing", "Reduce landing lag by 5%.", 45, 3, 18, 5, 19, 0, 0 },
    { "dodge", "Evasion", "Gain 2 frames of dodge invulnerability.", 50, 3, 19, 2, 20, 0, 0 },
    { "vampire", "Victory Snack", "Recover 8% damage after a KO.", 35, 3, 20, 8, 21, 0, 0 },
    { "thorns", "Retaliation", "Return 4% of received damage.", 40, 3, 21, 4, 22, 0, 0 },
    { "discount", "Haggler", "Shop items cost 5% less.", 45, 3, 22, 5, 23, 0, 0 },
    { "first", "Opening Strike", "First hit each stock deals 15% more damage.", 50, 3, 23, 15, 24, 0, 0 },
};

static const unsigned stack_ids[ROGUE_UPGRADES] = {
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 24U
};
unsigned RogueUpgrade_StackId(unsigned index)
{
    return index < ROGUE_UPGRADES ? stack_ids[index] : 0;
}
unsigned RogueUpgrade_StackIndex(unsigned id)
{
    unsigned i;
    for(i=0;i<ROGUE_UPGRADES;++i)
        if(stack_ids[i]==id) return i;
    return ROGUE_UPGRADES;
}

const RogueUpgradeDef* RogueUpgrade_Find(unsigned id)
{
    unsigned i;
    for (i = 0; i < 24; ++i)
        if (rogue_upgrades[i].id == id) return &rogue_upgrades[i];
    return 0;
}
