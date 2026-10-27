/* Pro AI ("Fight a pro"): a network trained on a pro's own games (tools in ghost-ai, file ghost_ai.bin
 * beside the game) plays the practice CPU's fighter. The game sends what it sees each frame, the same
 * numbers a replay records after a frame (sourceport/game/shim/mu_replay.c, post-frame event), and the
 * host (port/runtime/hle/ghost_ai.cpp) answers with the controller to hold. Plain C, also C++. */
#ifndef MU_GHOST_AI_H
#define MU_GHOST_AI_H

#define MU_GHOST_VERSION 1u
#define MU_GHOST_NONE 0xFFu   /* no pro: the practice CPU's own model plays */

/* One fighter as a replay's post-frame event has it. num: x, y, facing, percent, shield, stocks,
 * animation frame, the action state's own value (misc), airborne, jumps left, own speed x (air),
 * own speed y, knockback x, knockback y, own speed x (ground), hitlag frames left. */
typedef struct MuGhostFighter {
    float num[16];
    unsigned short motion;
    unsigned char kind;    /* internal character id */
    unsigned char flags[5];
} MuGhostFighter;

typedef struct MuGhostAct {
    float lx, ly, cx, cy, l, r;
    unsigned int buttons;    /* GameCube bits (MU_PC_*) */
} MuGhostAct;

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
} MuGhostObs;


#endif
