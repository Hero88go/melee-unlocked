/* The Friends lobby's select screen <-> host ABI, for the 4-player Friends lobby and the 2-seat Doubles
 * home (G2: the view's mode byte says which; seats 2-3 stay open in Doubles) (private command MU_LOBBY_COMMAND on
 * slippi_command, never a YouGame bridge key). The lobby itself lives in the runtime (yougame_lobby.h:
 * one session per room socket); the screen sends the local player's and the host's intents and reads
 * back what to draw. Exact-size byte formats, no native structs, padding or endian-dependent fields.
 *
 * Requests ({op, args}):
 *   ENTER    {op}                    the select is up: start the armed lobby's search (the Friends row
 *                                    or an accepted invite armed it), or keep the one running
 *   READ     {op}                    reply MU_LOBBY_VIEW_SIZE bytes (below); after a drop before the
 *                                    match it also takes the parked room back (slippi_online.cpp)
 *   PEEK     {op}                    READ's reply with no side effects (the select's close, before LEAVE)
 *   HOVER    {op, ch, color, team}   the local seat's cursor over a fighter
 *   PICK     {op, ch, color, team}   the local seat's coin went down
 *   UNPICK   {op}                    the local seat's coin came back up
 *   RULES    {op, teams, ff, time, stocks, minutes, pause}   host only; minutes 0 = no timer, stock only
 *   SEAT     {op, seat, level}       host only: level 1-9 makes or keeps a CPU, 0 opens the seat
 *   CPU_PICK {op, seat, ch, color, team}   host only: a CPU's coin (ch NONE takes it back up)
 *   START    {op, stage}             host only: stage 0 = random, else 2-32 except 21 and 26 (Doubles:
 *                                    either player, stage ignored: Start searches for opponents, and for
 *                                    a partner too when seat 1 is open)
 *   CANCEL   {op}                    Doubles: stop the search (Rooms refuses once a match is placed)
 *   INVITE   {op}                    open the YouGame multi-invite picker for this room
 *   LEAVE    {op}                    leave the lobby (B out of the select)
 *   LOCAL_SEAT {op, claim}          Doubles host: claim (1) or release (0) local seat 1
 *   LOCAL_HOVER / LOCAL_PICK {op, ch, color, team}  local controller's seat 1
 *   LOCAL_UNPICK {op}               take back local seat 1's pick
 *   COSTUMES {op, count[26]}         each fighter's costume count in this build (the select's
 *                                    gm_GetNumCostumesForCKind, 1-6): the match clamps colours to it
 *   CYCLE_MODE {op}                 Friends host: Melee -> Team Battle -> Squad -> Melee
 *   SQUAD_ADMISSION {op}             game passed the existing Squad budget scan
 *   LINEUP {op, count, char/color[4]} own seat, ordered; unused pairs zero
 * READ and PEEK answer the view; every other request answers one byte: 1 queued, 0 refused here (not
 * possible now).
 *
 * The view (READ and PEEK reply):
 *   u8 version, u8 flags (MU_LOBBY_F_*), u8 mode (MU_LOBBY_M_*), u8 my_seat (NONE), u8 host_seat (NONE),
 *   u8 block_hint (MU_LOBBY_HINT_*: why START isn't offered, or what the screen waits for),
 *   u8 refusal_hint, u8 refusal_seq (bumps once per refusal Rooms sent this player),
 *   u8 arm (bumps each time a lobby is armed: the select that saw another one never leaves it),
 *   u8 note_seq (bumps once per drop recorded: the select shows each drop's note once, same text or not),
 *   rules: u8 teams, u8 ff, u8 time, u8 stocks, u8 minutes, u8 pause,
 *   char note[MU_LOBBY_NOTE] (why the last match setup fell through, printable ASCII, NUL padded),
 *   4 seats * {u8 kind (MU_LOBBY_KIND_*), u8 level, u8 seat_flags (MU_LOBBY_S_*),
 *              u8 hover ch, color, team, u8 pick ch, color, team,
 *              u8 route (MU_LOBBY_ROUTE_*), u8 rtt_hi, u8 rtt_lo (ms, big-endian),
 *              char name[MU_LOBBY_NAME] (printable ASCII, NUL padded),
 *              u8 lineup_count, u8 lineup[4][2] (char/color in fight order)} */
#ifndef MU_LOBBY_WIRE_H
#define MU_LOBBY_WIRE_H
#include <stddef.h>
#include <string.h>

#define MU_LOBBY_COMMAND 0xD9u          /* free on slippi_command: D0-D8 are Slippi's, E8-EF taken */
#define MU_LOBBY_VERSION 6

#define MU_LOBBY_OP_ENTER 0
#define MU_LOBBY_OP_READ 1
#define MU_LOBBY_OP_HOVER 2
#define MU_LOBBY_OP_PICK 3
#define MU_LOBBY_OP_UNPICK 4
#define MU_LOBBY_OP_RULES 5
#define MU_LOBBY_OP_SEAT 6
#define MU_LOBBY_OP_CPU_PICK 7
#define MU_LOBBY_OP_START 8
#define MU_LOBBY_OP_INVITE 9
#define MU_LOBBY_OP_LEAVE 10
#define MU_LOBBY_OP_COSTUMES 11
#define MU_LOBBY_OP_PEEK 12             /* READ's view with no side effects (lobby_close, before LEAVE) */
#define MU_LOBBY_OP_CANCEL 13           /* Doubles: stop the search */
#define MU_LOBBY_OP_STAGE_SELECT 14    /* Friends: u8 open. The host went to the stage select (1) or backed out (0) */
#define MU_LOBBY_OP_LOCAL_SEAT 15       /* {op, claim}: second controller joins (1) or leaves (0) */
#define MU_LOBBY_OP_LOCAL_HOVER 16      /* {op, ch, color, team}: owned door 2 */
#define MU_LOBBY_OP_LOCAL_PICK 17
#define MU_LOBBY_OP_LOCAL_UNPICK 18
#define MU_LOBBY_OP_LINEUP 19       /* {op, count, four char/color pairs}; unused pairs zero */
#define MU_LOBBY_OP_CYCLE_MODE 20
#define MU_LOBBY_OP_SQUAD_ADMISSION 21 /* after the game passed the budget scan */
#define MU_LOBBY_OP_COUNT 22

#define MU_LOBBY_SEATS 4
#define MU_LOBBY_NONE 0xFF
#define MU_LOBBY_NAME 16
#define MU_LOBBY_CHAR_MAX 25
#define MU_LOBBY_COLOR_MAX 5
#define MU_LOBBY_TEAM_MAX 2
#define MU_LOBBY_RULE_MIN 1
#define MU_LOBBY_RULE_MAX 99
#define MU_LOBBY_NO_TIMER 0         /* stock's minutes: no time limit */
#define MU_LOBBY_CHARS (MU_LOBBY_CHAR_MAX + 1)
#define MU_LOBBY_NOTE 48

/* flags */
#define MU_LOBBY_F_ARMED 0x01       /* a lobby search is armed or running: the select is the 4-door one */
#define MU_LOBBY_F_KNOWN 0x02       /* the room's lobby state arrived */
#define MU_LOBBY_F_HOST 0x04        /* this player is the host */
#define MU_LOBBY_F_STARTED 0x08     /* the host's Start froze a setup: the match is being set up */
#define MU_LOBBY_F_UNSUPPORTED 0x10 /* the room has no Friends lobby */
#define MU_LOBBY_F_INVITING 0x20    /* the invite picker is open */
#define MU_LOBBY_F_GUEST 0x40       /* an invite opened this lobby */
#define MU_LOBBY_F_LEAVE 0x80       /* YouGame asked to leave (its lobby overlay): the select backs out as B does */

/* mode (the view's third byte) */
#define MU_LOBBY_M_SQUAD 0x10
#define MU_LOBBY_M_SQUAD_AVAILABLE 0x20
#define MU_LOBBY_M_LOCAL_OWNER 0x08 /* this machine owns local seat 1 */
#define MU_LOBBY_M_DOUBLES 0x01     /* the 2-seat Doubles home: seat 0 is me or my partner's host, seat 1 the partner */
#define MU_LOBBY_M_STAGE_SELECT 0x04 /* Friends: the host is on the stage select, and the guests watch it */
#define MU_LOBBY_M_SEARCHING 0x02   /* Doubles: Start went through, the home searches: the select is locked */
#define MU_LOBBY_DOUBLES_SEATS 2

/* seat kinds and flags */
#define MU_LOBBY_KIND_OPEN 0
#define MU_LOBBY_KIND_HUMAN 1
#define MU_LOBBY_KIND_CPU 2
#define MU_LOBBY_KIND_LOCAL 3
#define MU_LOBBY_S_HOVERING 0x01
#define MU_LOBBY_S_PICKED 0x02
#define MU_LOBBY_S_READY 0x04

/* a seat's link (the desktop app's direct link to that player): nothing until it is up */
#define MU_LOBBY_ROUTE_NONE 0
#define MU_LOBBY_ROUTE_DIRECT 1
#define MU_LOBBY_ROUTE_LAN 2
#define MU_LOBBY_ROUTE_RELAY 3
#define MU_LOBBY_ROUTE_UNKNOWN 4

/* hints: one short line under the doors */
#define MU_LOBBY_HINT_NONE 0
#define MU_LOBBY_HINT_OPENING 1          /* the room's state hasn't arrived yet */
#define MU_LOBBY_HINT_PRESS_START 2      /* host, nothing blocks */
#define MU_LOBBY_HINT_WAIT_HOST 3        /* guest, waiting for the host's Start */
#define MU_LOBBY_HINT_PICK 4             /* a pick still missing (pick_required) */
#define MU_LOBBY_HINT_PLAYERS 5          /* players_required */
#define MU_LOBBY_HINT_HUMANS 6           /* humans_required */
#define MU_LOBBY_HINT_TEAMS 7            /* teams_required */
#define MU_LOBBY_HINT_NOT_READY 8        /* digest_missing / a seat not ready */
#define MU_LOBBY_HINT_MISMATCH 9         /* digest_mismatch */
#define MU_LOBBY_HINT_AWAY 10            /* player_away */
#define MU_LOBBY_HINT_HOST_ONLY 11       /* host_only */
#define MU_LOBBY_HINT_STARTING 12        /* the setup froze: connecting to everyone */
#define MU_LOBBY_HINT_UNSUPPORTED 13     /* the room has no Friends lobby */
#define MU_LOBBY_HINT_TAKEN 14           /* seat_occupied / seat_owner */
#define MU_LOBBY_HINT_CHANGED 15         /* revision: someone changed the lobby first */
#define MU_LOBBY_HINT_IN_MATCH 16        /* match_active */
#define MU_LOBBY_HINT_REFUSED 17         /* any other refusal */
#define MU_LOBBY_HINT_CLOSED 18          /* the lobby's search failed or its room closed: B goes back */
#define MU_LOBBY_HINT_SEARCHING 19       /* Doubles: searching (B cancels) */
#define MU_LOBBY_HINT_START_SEARCH 20    /* Doubles: partner or CPU set, Start finds opponents */
#define MU_LOBBY_HINT_START_PARTNER 21   /* Doubles: seat 1 open, Start finds a partner and opponents */
#define MU_LOBBY_HINT_FIXED_RULES 22     /* fixed_rules: Doubles rules don't change */
#define MU_LOBBY_HINT_UNRANKED_ONLY 23   /* unranked_only */
#define MU_LOBBY_HINT_TEAM_MISMATCH 24   /* the matched other team has a different version */
#define MU_LOBBY_HINT_SEARCH_STOPPED 25  /* the search stopped (the queue closed or refused) */
#define MU_LOBBY_HINT_SQUAD_TWO 26
#define MU_LOBBY_HINT_SQUAD_CAPABILITY 27
#define MU_LOBBY_HINT_SQUAD_BUDGET 28
#define MU_LOBBY_HINT_SQUAD_RULES 29
#define MU_LOBBY_HINT_SQUAD_UNAVAILABLE 30
#define MU_LOBBY_HINT_COUNT 31

#define MU_LOBBY_RULES_SIZE 6
#define MU_LOBBY_SEAT_SIZE (3 + 3 + 3 + 3 + MU_LOBBY_NAME + 9)
#define MU_LOBBY_VIEW_HEAD 10
#define MU_LOBBY_VIEW_SIZE \
    (MU_LOBBY_VIEW_HEAD + MU_LOBBY_RULES_SIZE + MU_LOBBY_NOTE + MU_LOBBY_SEATS * MU_LOBBY_SEAT_SIZE)

typedef struct MuLobbyRules {
    unsigned char teams, ff, time, stocks, minutes, pause;
} MuLobbyRules;

typedef struct MuLobbySeat {
    unsigned char kind, level, flags;
    unsigned char hover_ch, hover_color, hover_team;
    unsigned char pick_ch, pick_color, pick_team;
    unsigned char route;
    unsigned short rtt_ms;
    char name[MU_LOBBY_NAME];
    unsigned char lineup_count, lineup[4][2];
} MuLobbySeat;

typedef struct MuLobbyView {
    unsigned char flags, mode, my_seat, host_seat, block_hint, refusal_hint, refusal_seq, arm, note_seq;
    MuLobbyRules rules;
    char note[MU_LOBBY_NOTE];
    MuLobbySeat seats[MU_LOBBY_SEATS];
} MuLobbyView;

static inline int mu_lobby_local_controls(const MuLobbyView* v)
{
    return v && (v->flags & (MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN)) == (MU_LOBBY_F_ARMED | MU_LOBBY_F_KNOWN) &&
           !(v->flags & MU_LOBBY_F_STARTED) && (v->mode & MU_LOBBY_M_DOUBLES) &&
           !(v->mode & MU_LOBBY_M_SEARCHING) && v->my_seat == 0 && (v->flags & MU_LOBBY_F_HOST);
}
static inline int mu_lobby_local_owned(const MuLobbyView* v)
{
    return mu_lobby_local_controls(v) && (v->mode & MU_LOBBY_M_LOCAL_OWNER) && v->seats[1].kind == MU_LOBBY_KIND_LOCAL;
}

/* Shown on the select screen, one line each, in Melee's label font: letters, digits, spaces and
 * periods only (mu_friends4_css.inc turns anything else into a space). */
static inline const char* mu_lobby_hint_text(unsigned hint)
{
    switch (hint) {
    case MU_LOBBY_HINT_SQUAD_TWO: return "Squad Strike is 2 players for now.";
    case MU_LOBBY_HINT_SQUAD_CAPABILITY: return "Your friend needs the latest update for Squad Strike.";
    case MU_LOBBY_HINT_SQUAD_BUDGET: return "These two games can't play Squad Strike together.";
    case MU_LOBBY_HINT_SQUAD_RULES: return "Squad Strike needs 4 stocks and no teams.";
    case MU_LOBBY_HINT_SQUAD_UNAVAILABLE: return "Squad Strike isn't available in this room yet.";
    case MU_LOBBY_HINT_OPENING: return "Opening the lobby...";
    case MU_LOBBY_HINT_PRESS_START: return "Press START to pick a stage";
    case MU_LOBBY_HINT_WAIT_HOST: return "Waiting for the host to start";
    case MU_LOBBY_HINT_PICK: return "Everyone needs to pick a fighter";
    case MU_LOBBY_HINT_PLAYERS: return "Press A on an open door to invite a friend";
    case MU_LOBBY_HINT_HUMANS: return "Needs 2 people. A on an open door invites";
    case MU_LOBBY_HINT_TEAMS: return "Put players on two teams";
    case MU_LOBBY_HINT_NOT_READY: return "Waiting for every game to be ready";
    case MU_LOBBY_HINT_MISMATCH: return "Someone has a different version";
    case MU_LOBBY_HINT_AWAY: return "A player is reconnecting";
    case MU_LOBBY_HINT_HOST_ONLY: return "Only the host can change that";
    case MU_LOBBY_HINT_STARTING: return "Connecting to everyone...";
    case MU_LOBBY_HINT_UNSUPPORTED: return "Everyone needs the latest version";
    case MU_LOBBY_HINT_TAKEN: return "That seat is taken";
    case MU_LOBBY_HINT_CHANGED: return "The lobby changed. Try again";
    case MU_LOBBY_HINT_IN_MATCH: return "A match is being set up";
    case MU_LOBBY_HINT_REFUSED: return "Could not do that. Try again";
    case MU_LOBBY_HINT_CLOSED: return "The lobby closed. Press B to go back";
    case MU_LOBBY_HINT_SEARCHING: return "Searching. Press B to cancel";
    case MU_LOBBY_HINT_START_SEARCH: return "Press START to find opponents";
    case MU_LOBBY_HINT_START_PARTNER: return "START finds a partner and opponents";
    case MU_LOBBY_HINT_FIXED_RULES: return "Doubles rules are fixed";
    case MU_LOBBY_HINT_UNRANKED_ONLY: return "Doubles is unranked for now";
    case MU_LOBBY_HINT_TEAM_MISMATCH: return "The other team has a different version";
    case MU_LOBBY_HINT_SEARCH_STOPPED: return "Search stopped. Press START to retry";
    default: return "";
    }
}

static inline const char* mu_lobby_route_text(unsigned route)
{
    switch (route) {
    case MU_LOBBY_ROUTE_DIRECT: return "Direct";
    case MU_LOBBY_ROUTE_LAN: return "LAN";
    case MU_LOBBY_ROUTE_RELAY: return "Relay";
    case MU_LOBBY_ROUTE_UNKNOWN: return "Linked";
    default: return "";
    }
}

/* The request's exact size for an op, 0 for an unknown op. */
static inline size_t mu_lobby_request_size(unsigned op)
{
    switch (op) {
    case MU_LOBBY_OP_CYCLE_MODE: case MU_LOBBY_OP_SQUAD_ADMISSION:
    case MU_LOBBY_OP_ENTER: case MU_LOBBY_OP_READ: case MU_LOBBY_OP_UNPICK: case MU_LOBBY_OP_INVITE: case MU_LOBBY_OP_LEAVE:
    case MU_LOBBY_OP_PEEK: case MU_LOBBY_OP_CANCEL: case MU_LOBBY_OP_LOCAL_UNPICK: return 1;
    case MU_LOBBY_OP_LOCAL_HOVER: case MU_LOBBY_OP_LOCAL_PICK:
    case MU_LOBBY_OP_HOVER: case MU_LOBBY_OP_PICK: return 4;
    case MU_LOBBY_OP_LINEUP: return 10;
    case MU_LOBBY_OP_RULES: return 1 + MU_LOBBY_RULES_SIZE;
    case MU_LOBBY_OP_SEAT: return 3;
    case MU_LOBBY_OP_CPU_PICK: return 5;
    case MU_LOBBY_OP_LOCAL_SEAT: case MU_LOBBY_OP_START: case MU_LOBBY_OP_STAGE_SELECT: return 2;
    case MU_LOBBY_OP_COSTUMES: return 1 + MU_LOBBY_CHARS;
    default: return 0;
    }
}

static inline int mu_lobby_pick_ok(unsigned ch, unsigned color, unsigned team)
{
    return ch <= MU_LOBBY_CHAR_MAX && color <= MU_LOBBY_COLOR_MAX && team <= MU_LOBBY_TEAM_MAX;
}
static inline int mu_lobby_stage_ok(unsigned stage)
{
    return stage == 0 || (stage >= 2 && stage <= 32 && stage != 21 && stage != 26);
}
/* {teams, ff, time, stocks, minutes, pause}: minutes 0 (no timer) only in stock (Rooms validMuRules). */
static inline int mu_lobby_rules_ok(const unsigned char* r)
{
    return r[0] <= 1 && r[1] <= 1 && r[2] <= 1 && r[3] >= MU_LOBBY_RULE_MIN && r[3] <= MU_LOBBY_RULE_MAX &&
           r[4] >= (r[2] ? MU_LOBBY_RULE_MIN : MU_LOBBY_NO_TIMER) && r[4] <= MU_LOBBY_RULE_MAX && r[5] <= 1;
}

/* Melee's own costume counts by fighter (gm_1601.c lbl_803D51A0 ncolors), for a build that never said. */
static inline unsigned mu_lobby_costumes_melee(unsigned ch)
{
    static const unsigned char n[MU_LOBBY_CHARS] = {6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5,
                                                    4, 4, 5, 5, 6, 5, 5, 4, 5, 5, 5, 4, 5};
    return ch < MU_LOBBY_CHARS ? n[ch] : 0;
}
static inline int mu_lobby_costumes_ok(const unsigned char* counts)
{
    unsigned i;
    for (i = 0; i < MU_LOBBY_CHARS; ++i)
        if (counts[i] < 1 || counts[i] > MU_LOBBY_COLOR_MAX + 1) return 0;
    return 1;
}

/* A request the host may act on: the exact size for its op and every field in range. */
static inline int mu_lobby_request_ok(const unsigned char* p, size_t n)
{
    if (!p || !n || n != mu_lobby_request_size(p[0])) return 0;
    switch (p[0]) {
    case MU_LOBBY_OP_LOCAL_HOVER: case MU_LOBBY_OP_LOCAL_PICK:
    case MU_LOBBY_OP_HOVER: case MU_LOBBY_OP_PICK: return mu_lobby_pick_ok(p[1], p[2], p[3]);
    case MU_LOBBY_OP_LINEUP: {
        unsigned i, seen = 0;
        if (p[1] > 4) return 0;
        for (i = 0; i < p[1]; ++i) {
            unsigned ch = p[2 + i * 2], color = p[3 + i * 2];
            if (ch > 25 || color > 5 || (seen & (1u << ch))) return 0;
            seen |= 1u << ch;
        }
        for (; i < 4; ++i) if (p[2 + i * 2] || p[3 + i * 2]) return 0;
        return 1;
    }
    case MU_LOBBY_OP_RULES: return mu_lobby_rules_ok(p + 1);
    case MU_LOBBY_OP_SEAT: return p[1] < MU_LOBBY_SEATS && p[2] <= 9;
    case MU_LOBBY_OP_CPU_PICK:
        return p[1] < MU_LOBBY_SEATS && (p[2] == MU_LOBBY_NONE || mu_lobby_pick_ok(p[2], p[3], p[4]));
    case MU_LOBBY_OP_START: return mu_lobby_stage_ok(p[1]);
    case MU_LOBBY_OP_LOCAL_SEAT: case MU_LOBBY_OP_STAGE_SELECT: return p[1] <= 1;
    case MU_LOBBY_OP_COSTUMES: return mu_lobby_costumes_ok(p + 1);
    default: return 1;
    }
}

static inline size_t mu_lobby_view_encode(const MuLobbyView* v, unsigned char* out, size_t capacity)
{
    size_t at = 0;
    unsigned i;
    if (!v || !out || capacity < MU_LOBBY_VIEW_SIZE) return 0;
    out[at++] = MU_LOBBY_VERSION;
    out[at++] = v->flags; out[at++] = v->mode; out[at++] = v->my_seat; out[at++] = v->host_seat;
    out[at++] = v->block_hint; out[at++] = v->refusal_hint; out[at++] = v->refusal_seq; out[at++] = v->arm;
    out[at++] = v->note_seq;
    out[at++] = v->rules.teams; out[at++] = v->rules.ff; out[at++] = v->rules.time;
    out[at++] = v->rules.stocks; out[at++] = v->rules.minutes; out[at++] = v->rules.pause;
    for (i = 0; i < MU_LOBBY_NOTE; ++i) {
        const char c = v->note[i];
        out[at++] = (unsigned char) (c >= 0x20 && c <= 0x7E ? c : 0);
    }
    out[at - 1] = 0;
    for (i = 0; i < MU_LOBBY_SEATS; ++i) {
        const MuLobbySeat* s = &v->seats[i];
        unsigned k;
        out[at++] = s->kind; out[at++] = s->level; out[at++] = s->flags;
        out[at++] = s->hover_ch; out[at++] = s->hover_color; out[at++] = s->hover_team;
        out[at++] = s->pick_ch; out[at++] = s->pick_color; out[at++] = s->pick_team;
        out[at++] = s->route;
        out[at++] = (unsigned char) (s->rtt_ms >> 8); out[at++] = (unsigned char) (s->rtt_ms & 0xFF);
        for (k = 0; k < MU_LOBBY_NAME; ++k) {
            const char c = s->name[k];
            out[at++] = (unsigned char) (c >= 0x20 && c <= 0x7E ? c : 0);
        }
        out[at - 1] = 0;   /* always NUL-terminated */
        out[at++] = s->lineup_count;
        for (k = 0; k < 4; ++k) { out[at++] = s->lineup[k][0]; out[at++] = s->lineup[k][1]; }
    }
    return at;
}

/* 0 when the bytes aren't a view of this version (the screen then draws nothing new). */
static inline int mu_lobby_view_decode(const unsigned char* b, size_t n, MuLobbyView* v)
{
    size_t at = 1;
    unsigned i;
    if (!b || !v || n != MU_LOBBY_VIEW_SIZE || b[0] != MU_LOBBY_VERSION) return 0;
    memset(v, 0, sizeof *v);
    v->flags = b[at++]; v->mode = b[at++]; v->my_seat = b[at++]; v->host_seat = b[at++];
    v->block_hint = b[at++]; v->refusal_hint = b[at++]; v->refusal_seq = b[at++]; v->arm = b[at++];
    v->note_seq = b[at++];
    v->rules.teams = b[at++]; v->rules.ff = b[at++]; v->rules.time = b[at++];
    v->rules.stocks = b[at++]; v->rules.minutes = b[at++]; v->rules.pause = b[at++];
    memcpy(v->note, b + at, MU_LOBBY_NOTE); at += MU_LOBBY_NOTE;
    v->note[MU_LOBBY_NOTE - 1] = 0;
    if ((v->my_seat >= MU_LOBBY_SEATS && v->my_seat != MU_LOBBY_NONE) ||
        (v->host_seat >= MU_LOBBY_SEATS && v->host_seat != MU_LOBBY_NONE) ||
        v->block_hint >= MU_LOBBY_HINT_COUNT || v->refusal_hint >= MU_LOBBY_HINT_COUNT ||
        (v->mode & ~(MU_LOBBY_M_DOUBLES | MU_LOBBY_M_SEARCHING | MU_LOBBY_M_STAGE_SELECT | MU_LOBBY_M_LOCAL_OWNER | MU_LOBBY_M_SQUAD | MU_LOBBY_M_SQUAD_AVAILABLE)) ||
        ((v->mode & MU_LOBBY_M_LOCAL_OWNER) && (!(v->mode & MU_LOBBY_M_DOUBLES) || v->my_seat != 0)) ||
        ((v->mode & MU_LOBBY_M_STAGE_SELECT) && (v->mode & MU_LOBBY_M_DOUBLES)) ||
        ((v->mode & MU_LOBBY_M_SEARCHING) && !(v->mode & MU_LOBBY_M_DOUBLES)) ||
        ((v->mode & MU_LOBBY_M_DOUBLES) && v->my_seat != MU_LOBBY_NONE && v->my_seat >= MU_LOBBY_DOUBLES_SEATS))
        return 0;
    if ((v->mode & MU_LOBBY_M_DOUBLES) && (v->mode & (MU_LOBBY_M_SQUAD | MU_LOBBY_M_SQUAD_AVAILABLE))) return 0;
    for (i = 0; i < MU_LOBBY_SEATS; ++i) {
        MuLobbySeat* s = &v->seats[i];
        s->kind = b[at++]; s->level = b[at++]; s->flags = b[at++];
        s->hover_ch = b[at++]; s->hover_color = b[at++]; s->hover_team = b[at++];
        s->pick_ch = b[at++]; s->pick_color = b[at++]; s->pick_team = b[at++];
        s->route = b[at++];
        s->rtt_ms = (unsigned short) ((b[at] << 8) | b[at + 1]); at += 2;
        memcpy(s->name, b + at, MU_LOBBY_NAME); at += MU_LOBBY_NAME;
        s->name[MU_LOBBY_NAME - 1] = 0;
        s->lineup_count = b[at++];
        {
            unsigned k, seen = 0;
            if (s->lineup_count > 4) return 0;
            for (k = 0; k < 4; ++k) {
                unsigned ch = b[at++], color = b[at++];
                s->lineup[k][0] = ch; s->lineup[k][1] = color;
                if (k < s->lineup_count) {
                    if (ch > 25 || color > 5 || (seen & (1u << ch))) return 0;
                    seen |= 1u << ch;
                }
            }
        }
        if (s->kind > MU_LOBBY_KIND_LOCAL || s->route > MU_LOBBY_ROUTE_UNKNOWN) return 0;
        if (s->kind == MU_LOBBY_KIND_LOCAL && (!(v->mode & MU_LOBBY_M_DOUBLES) || i != 1)) return 0;
    }
    if ((v->mode & MU_LOBBY_M_LOCAL_OWNER) && v->seats[1].kind != MU_LOBBY_KIND_LOCAL) return 0;
    return 1;
}

#endif
