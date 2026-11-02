/* Pro AI ("Fight a pro"): a network trained on a pro's own games (tools in ghost-ai, file ghost_ai.bin
 * beside the game) plays the practice CPU's fighter. The game sends what it sees each frame, the same
 * numbers a replay records after a frame (sourceport/game/shim/mu_replay.c, post-frame event), and the
 * host (port/runtime/hle/ghost_ai.cpp) answers with the controller to hold. Plain C, also C++. */
#ifndef MU_GHOST_AI_H
#define MU_GHOST_AI_H

#define MU_GHOST_VERSION 3u
#define MU_GHOST_NONE 0xFFu   /* no pro: the practice CPU's own model plays */

/* One fighter as a replay's post-frame event has it. num: x, y, facing, percent, shield, stocks,
 * animation frame, the action state's own value (misc), airborne, jumps left, own speed x (air),
 * own speed y, knockback x, knockback y, own speed x (ground), hitlag frames left. */
typedef struct MuGhostFighter {
    float num[16];
    unsigned short motion;
    unsigned char kind;    /* internal character id */
    unsigned char flags[5];
    unsigned char hurtbox;   /* the replay's hurtbox state: 0 vulnerable, else invulnerable or intangible */
    unsigned char pad[3];
} MuGhostFighter;

/* Set in MuGhostAct.buttons by a policy that plays every frame itself (Phillip): the practice CPU's
 * recovery safety net stays off for it. Never a controller bit. */
#define MU_GHOST_ACT_OWN_RECOVERY 0x80000000u

typedef struct MuGhostAct {
    float lx, ly, cx, cy, l, r;
    unsigned int buttons;    /* GameCube bits (MU_PC_*) */
} MuGhostAct;

/* An item as a replay's item update has it: kind (Slippi item type), action state, position and the
 * spawn id that keeps it in the same slot while it lives. */
typedef struct MuGhostItem {
    unsigned short kind;
    unsigned char state;
    unsigned char exists;
    unsigned int spawn;
    float x, y;
} MuGhostItem;
#define MU_GHOST_ITEMS 15

typedef struct MuGhostObs {
    unsigned char version;   /* MU_GHOST_VERSION */
    unsigned char pro;       /* which pro (index into the file's pros) */
    unsigned char slot;      /* the fighter's port: each has its own memory of the match */
    unsigned char reset;     /* 1: a new match, a restart or a jump: forget what came before */
    short stage;             /* Slippi stage id (31 Battlefield, 32 Final Destination ...), 0 other */
    short pad;
    unsigned int frame;
    MuGhostFighter self, opp;
    MuGhostAct applied;      /* what the fighter really held last frame (after the safety net): the
                              * network remembers that, not what it asked for */
    /* Version 3, for Phillip's newest policies: */
    int match_frame;         /* the Slippi frame number (-123 on the match's first frame) */
    float fod_left, fod_right;   /* Fountain of Dreams platform heights (0 elsewhere) */
    unsigned char self_nana, opp_nana, item_count, pad3;   /* a Nana follows the fighter; items used */
    MuGhostFighter self_nana_f, opp_nana_f;
    MuGhostItem items[MU_GHOST_ITEMS];
} MuGhostObs;


#endif
