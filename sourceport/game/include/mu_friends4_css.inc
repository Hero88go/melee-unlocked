/* Included only by the native mncharsel.c, after mu_doubles_css.inc: the 4-player Friends lobby's select
 * (shim/mu_slippi_menu.c mu_lobby_css()). It is Melee's VS 4-door select. This player's pad drives its own
 * seat's door (shim/mu_pad.c puts it there); the other three doors are drawn from the lobby's view
 * (port/runtime/abi/mu_lobby_wire.h): hover, pick, colour, team, CPU and level. The host also sets the
 * rules, Teams and the CPUs; everyone sees them. One hint line says why Start isn't offered or what the
 * screen waits for, and each door says "Invite", "CPU Lv N" or its link (route and ping, once it is up).
 * The Doubles home (view.mode MU_LOBBY_M_DOUBLES, G2) is the same select with two doors: mine and my
 * partner's. Doors 3 and 4 are hidden outright, door, name plate, tab, KO counter and tag (mu_f4_shut; the
 * opponents come with the match); doors 1 and 2 stay where Melee puts them, on the far left, from the
 * first frame on. Seat 0 picks the partner with the partner door's tab, and the partner door shows the
 * choice (11-11): Find (a "?" portrait; START searches for a partner), Invite (a friend portrait; START
 * opens YouGame's friend list), P2 (Player 2's own door at once, as if a 2nd controller were plugged in) or
 * CPU (Melee's CPU door, its level on the slider); see mu_friends4_partner_cycle and mu_f4_art_draw.
 * The rules are fixed (no Rules, no Teams, no team boxes: matchmaking sets the teams);
 * either player's START searches, no stage select; while Rooms searches the select is locked and B cancels
 * (mu_lobby_pads, shim/mu_slippi_menu.c). Doubles draws no hint line or door labels: the right half of the
 * row, where doors 3 and 4 were, carries the panel of Slippi's online select (shim/mu_slippi_css.c: its
 * font, places, colours and spinners), saying the fighter, the search and the partner (mu_f4_panel). Every
 * other select, Friends included, keeps all four doors and its text. */
#include <mu_lobby_wire.h>

#define MU_F4_HOLD 60    /* frames the host's own change to a door wins over a view that hasn't caught up */
#define MU_F4_NOTE 120   /* frames a refusal or a press that did nothing stays on the hint line */
#define MU_F4_LINES 6    /* the hint, the rules, one label per door (all blank in Doubles: mu_f4_panel) */
/* Doubles: the right-hand panel, subtext for subtext the one mu_slippi_css_text_init makes (header, three
 * lines each with its spinner, two gray rows, the top bar's two, four red error rows). */
#define MU_F4_PANEL_SUBS 15
#define MU_F4_ST_HEADER 0
#define MU_F4_ST_LINE1 1     /* lines 1, 3, 5; each line's spinner is the next subtext */
#define MU_F4_ST_GRAY 7      /* the gray rows 7 and 8 */
#define MU_F4_ST_ERR1 11     /* red rows 11 to 14 */
#define MU_F4_PANEL_WRAP 30  /* an error wraps at 30 letters, as Slippi's */
#define MU_F4_SPIN_FRAMES 15 /* each spinner mark shows 15 frames */
#define MU_F4_SHUT_MAX 160  /* doors 3 and 4's joints (54 in MnSlChr: 27 a door, before the name plate's) */
#define MU_F4_SHUT_TEXTS 8  /* their KO count, name tag, name list and shield label */
#define MU_F4_IDLE 0xFE  /* sent[0]: no hover or pick out, and nothing to take back */

static struct {
    MuLobbyView view;               /* the last frame's, for the hooks inside the cursor think */
    int view_ok;
    HSD_Text* text;                 /* made at the first frame of each build of the screen */
    int line[MU_F4_LINES];
    char drawn[MU_F4_LINES][96];
    float toggle[4][2], teambox[4][2];   /* the doors' HMN/CPU switch and team box as Melee has them */
    float toggle_put[4][2], teambox_put[4][2];   /* what mu_f4_bounds last wrote there */
    unsigned sig[4];                /* what each other door last drew from the view, 0: draw again */
    int hold[4];
    unsigned char kind_seen[4];     /* each door's kind as this code last left it, 0xFF: not yet; any
                                     * other kind at the next frame is the host's own switch press */
    unsigned char partner_sent[4];
    unsigned char sent[4];          /* this player's last HOVER/PICK/UNPICK request, sent[0] 0xFF: none,
                                     * MU_F4_IDLE: off the icons after a hover (nothing went out) */
    unsigned char seat_sent[4][2];  /* the host's last SEAT {kind, level} per door */
    unsigned char cpu_sent[4][3];   /* the host's last CPU_PICK {ch, color, team} per door */
    MuLobbyRules rules_sent, rules_drawn;
    int rules_sent_ok, rules_drawn_ok;
    unsigned char mode_seen, seat_seen, lineup_sent[10];
    int mode_pending, rules_sync;
    unsigned char refusal_seq;
    int refusal_ok;
    unsigned char note;
    int note_frames;
    char note_text[MU_LOBBY_NOTE];  /* why the last setup fell through (view.note), on the hint line */
    char note_seen[MU_LOBBY_NOTE];
    unsigned char note_seq_seen;    /* view.note_seq when note_seen was taken: a new drop shows again */
    int note_text_frames;
    unsigned char door_color[4];    /* the colour each door's own X/Y cycle last sent (m-ex wrap) */
    int locked;                     /* this player's B5 lock-in went out for the started setup */
    int leaving;                    /* the select is on its way to the splash or the stage select */
    int curtain_made;               /* this build of the screen has its black cover (mu_lobby_curtain) */
    unsigned char kind_logged[4];   /* each seat's kind + 1 as last logged, 0: not yet */
    unsigned char lineup_logged[4]; /* Squad Strike: each seat's lineup count + 1 as last logged, 0: not yet */
    /* Doubles: doors 3 and 4 hidden, with each joint's and text's own hidden bit from before, for the way
     * back. Only this build's: every build of the screen makes new joints and texts. */
    int shut;
    int shut_count, shut_texts;
    HSD_JObj* shut_jobj[MU_F4_SHUT_MAX];
    unsigned char shut_was[MU_F4_SHUT_MAX];
    HSD_Text* shut_text[MU_F4_SHUT_TEXTS];
    unsigned char shut_text_was[MU_F4_SHUT_TEXTS];
    int shut_logged;                /* this visit logged the two-door select */
    unsigned char partner_logged;   /* the partner door's state (MU_F4_P_*) + 1 as last logged, 0: not yet */
    int cap_logged;                 /* bits: this visit logged a full hide list (1), a deep tree (2), texts (4) */
    /* Doubles: the partner door's tab (mu_lobby_partner_want). */
    unsigned char partner_kind;     /* seat 1's kind as the tab last followed it, 0xFF: not yet this visit */
    int partner_cpu_wait;           /* frames the CPU waits for Player 2 to leave the seat (P2 -> CPU) */
    unsigned char pair_team;        /* Doubles: the pair's colour (0 red, 1 blue, 2 green), 0xFF: not yet this visit */
    unsigned char partner_team_seen;/* Doubles: the friend partner's colour as last seen, 0xFF: none yet */
    int pair_wait;                  /* Doubles: frames the guest's press waits for the host to show it */
    unsigned char pair_worn[3];     /* Doubles: frames since the host last wore each colour, counting down from 1 s */
    unsigned char pair_seen[2];     /* Doubles: doors 1 and 2's colour as last applied, 0xFF: not this machine's door */
    unsigned char pair_unsent;      /* Doubles: the guest's press has not gone out yet (no hover or pick to carry it) */
    int pair_shown;                 /* Doubles: frames since the host's own hover or pick first went out, up to 1 s */
    int partner_p2_wait;            /* frames the tab waits for Player 2 to sit down (its LOCAL_SEAT went out) */
    /* Doubles: the plates of doors 3 and 4 (MU_F4_PLATES_JOINT's shapes 3 and 4) with their own flags. */
    HSD_DObj* plate[2];
    unsigned plate_was[2];
    int plates;                     /* 2 once found this build, -1: looked and not found */
    /* Doubles: the panel (mu_f4_panel). Made with the other texts at each build of the screen. */
    HSD_Text* panel;
    char panel_drawn[MU_F4_PANEL_SUBS][40];   /* each subtext as drawn, "" until its first draw this build */
    GXColor panel_color[MU_F4_PANEL_SUBS];
    unsigned panel_draws;           /* redraws (only on a change): the tests count them */
    unsigned panel_frame;           /* the spinners' clock */
    /* Doubles: the partner door's Find / Invite portrait (mu_f4_art_draw). */
    int art_made;                   /* this build of the screen has its drawing text */
    unsigned char art;              /* MU_F4_ART_*: what it draws this frame */
    unsigned char art_logged;       /* art + 1 as last logged, 0: not yet */
    float art_box[4];               /* left, top, right, bottom, in world units */
    /* Doubles: the partner door's tag and the caption plate under its Find / Invite portrait (mu_f4_partner_marks). */
    HSD_Text* tag;                  /* made at the first frame that needs it, each build of the screen */
    HSD_Text* caption;
    char tag_drawn[8], caption_drawn[16];     /* the words each text holds */
    char tag_logged[8], caption_logged[16];   /* the words last logged ("-": none) */
    float caption_box[4];           /* the plate: left, top, right, bottom, in world units (mu_f4_caption_draw) */
    unsigned char invite_door;      /* the door whose card opened the invite picker + 1, 0: none */
    unsigned char stopped_seen;     /* last frame's block_hint was SEARCH_STOPPED: its 2 s note shows once */
    unsigned char route_logged[4];  /* Doubles: each seat's link route + 1 as last logged, 0: not yet */
    unsigned char own_drawn;        /* this player's door + 1 once drawn as HMN this visit, 0: not yet */
} mu_f4;

static void mu_f4_marks_off(int freed);

/* Every build of the screen (its first, and again after the Rules overlay or name entry, which free all
 * texts): the texts are made anew and every other door is drawn again from the view. */
static void mu_friends4_css_build(void)
{
    int i;
    if (mu_friends_css_frames == 0) {
        /* A new visit: nothing kept from the last one but the doors' saved bounds and the last note seen
         * (its text and drop count), so a drop's note shows once, not again on every return from the stage
         * select; a new drop (view.note_seq) or a different text shows again. */
        float toggle[4][2], teambox[4][2], toggle_put[4][2], teambox_put[4][2];
        char note_seen[MU_LOBBY_NOTE];
        const unsigned char note_seq_seen = mu_f4.note_seq_seen;
        /* Behind the cover the first read can't be the baseline: it may already carry the refusal of this Start. */
        const unsigned char refusal_seq = mu_f4.refusal_seq;
        const int refusal_ok = mu_f4.refusal_ok && mu_lobby_curtain();
        memcpy(toggle, mu_f4.toggle, sizeof toggle);
        memcpy(teambox, mu_f4.teambox, sizeof teambox);
        memcpy(toggle_put, mu_f4.toggle_put, sizeof toggle_put);
        memcpy(teambox_put, mu_f4.teambox_put, sizeof teambox_put);
        memcpy(note_seen, mu_f4.note_seen, sizeof note_seen);
        memset(&mu_f4, 0, sizeof mu_f4);
        memcpy(mu_f4.toggle, toggle, sizeof toggle);
        memcpy(mu_f4.teambox, teambox, sizeof teambox);
        memcpy(mu_f4.toggle_put, toggle_put, sizeof toggle_put);
        memcpy(mu_f4.teambox_put, teambox_put, sizeof teambox_put);
        memcpy(mu_f4.note_seen, note_seen, sizeof note_seen);
        mu_f4.note_seq_seen = note_seq_seen;
        mu_f4.refusal_seq = refusal_seq;
        mu_f4.refusal_ok = refusal_ok;
        mu_f4.sent[0] = mu_f4.partner_sent[0] = 0xFF;
        mu_f4.partner_kind = 0xFF;
        mu_f4.pair_team = mu_f4.partner_team_seen = mu_f4.pair_seen[0] = mu_f4.pair_seen[1] = 0xFF;
        for (i = 0; i < 4; ++i) mu_f4.seat_sent[i][0] = mu_f4.cpu_sent[i][0] = 0xFF;
    }
    for (i = 0; i < 4; ++i) mu_f4.kind_seen[i] = 0xFF;
    mu_f4.mode_seen = mu_f4.seat_seen = 0xFF;
    mu_f4.lineup_sent[0] = 0xFF;
    mu_f4.text = mu_f4.panel = NULL;
    memset(mu_f4.drawn, 0, sizeof mu_f4.drawn);
    memset(mu_f4.panel_drawn, 0, sizeof mu_f4.panel_drawn);
    for (i = 0; i < 4; ++i) mu_f4.sig[i] = 0;
    mu_f4.curtain_made = 0;
    mu_f4.art_made = 0;
    mu_f4.art = 0;
    mu_f4_marks_off(1);   /* the partner door's tag and plate: freed with the other texts */
    /* The last build's joints and texts are gone: nothing to put back, and the next frame hides anew. */
    mu_f4.shut = 0;
    mu_f4.shut_count = mu_f4.shut_texts = mu_f4.plates = 0;
}

static int mu_f4_mine(void)
{
    return mu_f4.view_ok && mu_f4.view.my_seat < MU_LOBBY_SEATS ? mu_f4.view.my_seat : (gm_801677F0() & 3);
}
static int mu_f4_host(void)
{
    return mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_HOST) && !(mu_f4.view.flags & MU_LOBBY_F_STARTED);
}
static void mu_f4_note(unsigned hint)
{
    mu_f4.note = (unsigned char) hint;
    mu_f4.note_frames = MU_F4_NOTE;
}
/* The Doubles home (2 doors), and its search (the select locked, B cancels). */
static int mu_f4_doubles(void) { return mu_f4.view_ok && (mu_f4.view.mode & MU_LOBBY_M_DOUBLES); }
static int mu_f4_searching(void) { return mu_f4_doubles() && (mu_f4.view.mode & MU_LOBBY_M_SEARCHING); }
/* The doors a seat of this select stands behind: 2 in Doubles, else 4. */
static int mu_f4_doors_open(void) { return mu_f4_doubles() ? MU_LOBBY_DOUBLES_SEATS : MU_LOBBY_SEATS; }

/* The coin pickup (cursor think): only this player's own coin, and the host's on a CPU's door. A
 * person's coin is already only theirs in Melee. Once the setup is frozen nobody's coin moves. */
static int mu_friends4_coin_locked(u8 door)
{
    if (!mu_lobby_css()) return 0;
    if (mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_STARTED)) return 1;
    if (mu_f4_searching()) return 1;   /* Doubles: Rooms refuses every change while it searches */
    if (door == mu_f4_mine()) return 0;
    if (door == 1 && mu_lobby_local_owned(&mu_f4.view)) return 0;
    return !(mu_f4_host() && door < mu_f4_doors_open() && mu_f4.view.seats[door].kind == MU_LOBBY_KIND_CPU);
}

/* The Rules button and the Teams toggle (cursor think, on A): the host's alone. 1 = refused here. */
static int mu_friends4_host_only(void)
{
    if (!mu_lobby_css()) return 0;
    if (mu_f4_doubles()) {   /* Doubles: fixed rules, and matchmaking sets the teams */
        lbAudioAx_80024030(3);
        mu_f4_note(mu_f4_searching() ? MU_LOBBY_HINT_SEARCHING : MU_LOBBY_HINT_FIXED_RULES);
        return 1;
    }
    if (mu_f4_host()) return 0;
    lbAudioAx_80024030(3);
    mu_f4_note(mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_STARTED) ? MU_LOBBY_HINT_IN_MATCH
                                                                         : MU_LOBBY_HINT_HOST_ONLY);
    return 1;
}

/* A on the existing top-left toggle. The room alone chooses the next mode. */
static int mu_friends4_cycle_mode(void)
{
    unsigned char op = MU_LOBBY_OP_CYCLE_MODE;
    if (!mu_lobby_css()) return 0;
    if (mu_friends4_host_only()) return 1;
#if MU_SQUAD
    if (!mu_f4.mode_pending && mu_lobby_send(&op, 1)) {
        mu_f4.mode_pending = MU_F4_HOLD;
        sfxMove();
    }
#else
    return 0;
#endif
    return 1;
}

static int mu_f4_send(const unsigned char* req, unsigned size)
{
    return mu_lobby_send(req, size);
}

/* ---- Doubles: the partner door's tab (11-10, report e22413bd) ----
 * Seat 0's tab on the partner door is the partner choice, in this order: Find (START searches for a partner and
 * the opponents), Invite (START opens YouGame's friend list; the friend who accepts takes the door, and B takes
 * the invite back), P2 (the door is Player 2's at once, as if a 2nd controller were plugged in: a 2nd controller
 * on this machine plays it, its B gives it back) and CPU (the door's CPU, its level on the door's slider). Each
 * home starts on Find; the choice is this machine's (mu_lobby_partner_want, shim/mu_slippi_menu.c) and stays
 * through the home's matches. Nothing new goes over the wire: CPU is the SEAT request the HMN/CPU switch always
 * sent (mu_f4_host_doors), P2 the LOCAL_SEAT claim a 2nd controller's START used to send, Invite the INVITE op
 * (a Teams invite, yougame_friends.h doubles_invite). Only seat 0 turns it; it is locked while the home
 * searches, once a match is being set up, while a friend is in the seat, and while a CPU or Player 2 sits down. */
static const char* const mu_f4_want_names[MU_LOBBY_WANTS] = { "find", "invite", "p2", "cpu" };

/* The partner door's kind as the tab sets it, the way Melee's own switch press does (an empty port's door is
 * never HMN, so mu_f4_host_doors never reads it as one); the door keeps it while the view catches up. */
static void mu_f4_partner_door(CSSData* css, u8 kind)
{
    CSSDoor* door = &mnCharSel_803F0DFC.doors[1];
    PlayerInitData* p = &css->vs.start.players[1];
    door->p_kind = door->p_kind_prev = kind;
    p->slot_type = kind;
    mu_f4.kind_seen[1] = kind;
    mu_f4.hold[1] = MU_F4_HOLD;
    if (kind == Gm_PKind_Cpu) {
        p->nametag = 0x78;   /* as Melee's switch: a CPU wears no tag */
        mnCharSel_803F0E8C[1].data->use_tag = 0;
        if (door->selected_since_load == 0 && mnCharSel_804A0BD0[1] != NULL) mnCharSel_8025FB50(1, 1);
    }
    mnCharSel_8025DB34(1);
}

static void mu_f4_want(int want, const char* why)
{
    OSReport("doubles: partner tab %s -> %s (%s)\n", mu_f4_want_names[mu_lobby_partner_want()], mu_f4_want_names[want], why);
    mu_lobby_set_partner_want(want);
}

/* Player 2 takes the partner door now (the tab on P2, or START there after a refusal): the LOCAL_SEAT claim, and
 * once the room seats it the door is a person's, played by a 2nd controller on this machine (mu_f4_local's
 * second door; mu_lobby_pads gives it the first other connected pad). 1: the claim went out. */
static int mu_f4_p2_claim(const char* why)
{
    const unsigned char claim[2] = { MU_LOBBY_OP_LOCAL_SEAT, 1 };
    if (!mu_f4_send(claim, sizeof claim)) {
        OSReport("doubles: Player 2's door not claimed (%s, not sent)\n", why);
        return 0;
    }
    OSReport("doubles: Player 2 takes the partner door (%s)\n", why);
    mu_f4.partner_p2_wait = MU_F4_HOLD;   /* the tab waits for the seat, then turns again */
    return 1;
}

/* A on the partner door's tab (the cursor think, before Melee flips the door). 1: Melee's flip is skipped. */
static int mu_friends4_partner_cycle(u8 door)
{
    CSSData* css = mnCharSel_804D6CB0;
    const MuLobbySeat* s = &mu_f4.view.seats[1];
    const char* locked = NULL;
    int want, next;
    if (!mu_lobby_css() || !mu_f4_doubles()) return 0;
    if (door != 1 || css == NULL || mu_f4_mine() != 0) locked = "not the host's partner door";
    else if (mu_f4.view.flags & MU_LOBBY_F_STARTED) locked = "a match is being set up";
    else if (mu_f4_searching()) locked = "searching";
    else if (!mu_f4_host()) locked = "only the host picks the partner";
    else if (mu_f4.view.flags & MU_LOBBY_F_INVITING) locked = "an invite is out (B takes it back)";
    else if (s->kind == MU_LOBBY_KIND_HUMAN) locked = "a friend is in the seat";
    else if (mu_f4.partner_cpu_wait > 0) locked = "Player 2 is leaving the seat";
    else if (mu_lobby_partner_want() == MU_LOBBY_WANT_CPU && s->kind == MU_LOBBY_KIND_OPEN && mu_f4.hold[1] > 0)
        locked = "the CPU is sitting down";
    else if (mu_lobby_partner_want() == MU_LOBBY_WANT_P2 && s->kind == MU_LOBBY_KIND_OPEN && mu_f4.partner_p2_wait > 0)
        locked = "Player 2 is sitting down";
    if (locked != NULL) {
        lbAudioAx_80024030(3);
        OSReport("doubles: partner tab locked (%s)\n", locked);
        if (mu_f4_searching()) mu_f4_note(MU_LOBBY_HINT_SEARCHING);
        else if (mu_f4.view.flags & MU_LOBBY_F_STARTED) mu_f4_note(MU_LOBBY_HINT_IN_MATCH);
        return 1;
    }
    want = mu_lobby_partner_want();
    next = (want + 1) % MU_LOBBY_WANTS;
    if (want == MU_LOBBY_WANT_P2 && s->kind == MU_LOBBY_KIND_LOCAL) {
        const unsigned char release[2] = { MU_LOBBY_OP_LOCAL_SEAT, 0 };
        if (!mu_f4_send(release, sizeof release)) {
            lbAudioAx_80024030(3);
            mu_f4_note(MU_LOBBY_HINT_REFUSED);
            return 1;
        }
        mu_f4.partner_cpu_wait = 2 * MU_F4_HOLD;   /* the CPU sits down once the seat is open */
    } else if (want == MU_LOBBY_WANT_CPU) {
        mu_f4_partner_door(css, Gm_PKind_NA);   /* mu_f4_host_doors sends SEAT {1, 0} */
    } else if (next == MU_LOBBY_WANT_CPU && s->kind == MU_LOBBY_KIND_OPEN) {
        mu_f4_partner_door(css, Gm_PKind_Cpu);  /* mu_f4_host_doors sends SEAT {1, level} and the CPU's pick */
    }
    mu_f4_want(next, "tab");
    if (next == MU_LOBBY_WANT_P2 && s->kind == MU_LOBBY_KIND_OPEN) mu_f4_p2_claim("tab");
    sfxMove();
    return 1;
}

/* Once a frame, after the doors are drawn from the view: the tab follows what the room did with the seat (a
 * CPU or Player 2 that sat down, a friend who took it), and a CPU that never sat down is Find again. */
static void mu_f4_partner_sync(CSSData* css, int mine)
{
    const unsigned kind = mu_f4.view.seats[1].kind;
    int want;
    if (!mu_f4_doubles() || mine != 0 || !(mu_f4.view.flags & MU_LOBBY_F_HOST)) return;
    want = mu_lobby_partner_want();
    if (kind != mu_f4.partner_kind) {
        if (kind == MU_LOBBY_KIND_CPU && want != MU_LOBBY_WANT_CPU) mu_f4_want(MU_LOBBY_WANT_CPU, "a CPU is in the seat");
        else if (kind == MU_LOBBY_KIND_LOCAL && want != MU_LOBBY_WANT_P2 && mu_f4.partner_cpu_wait == 0)
            mu_f4_want(MU_LOBBY_WANT_P2, "Player 2 is in the seat");
        else if (kind == MU_LOBBY_KIND_HUMAN && want != MU_LOBBY_WANT_FIND)
            mu_f4_want(MU_LOBBY_WANT_FIND, "a friend took the seat");
        mu_f4.partner_kind = (unsigned char) kind;
    }
    if (kind != MU_LOBBY_KIND_OPEN) mu_f4.partner_p2_wait = 0;
    else if (mu_f4.partner_p2_wait > 0) --mu_f4.partner_p2_wait;
    if (mu_f4.partner_cpu_wait > 0) {
        if (kind == MU_LOBBY_KIND_OPEN && !mu_lobby_local_owned(&mu_f4.view)) {
            mu_f4.partner_cpu_wait = 0;
            if (mu_lobby_partner_want() == MU_LOBBY_WANT_CPU && mu_f4_host() && !mu_f4_searching())
                mu_f4_partner_door(css, Gm_PKind_Cpu);
        } else if (--mu_f4.partner_cpu_wait == 0 && kind == MU_LOBBY_KIND_LOCAL) {
            mu_f4_want(MU_LOBBY_WANT_P2, "Player 2 never left the seat");
        }
    } else if (mu_lobby_partner_want() == MU_LOBBY_WANT_CPU && kind == MU_LOBBY_KIND_OPEN && mu_f4.hold[1] == 0 &&
               mnCharSel_803F0DFC.doors[1].p_kind != Gm_PKind_Cpu) {
        mu_f4_want(MU_LOBBY_WANT_FIND, "no CPU sat down");
    }
}

/* START on the host's partner choice while the seat is still empty (Doubles). 1: handled here. Find falls
 * through to the search; Invite opens the friend list; P2 claims the door again (the room refused or never
 * answered the tab's claim); a CPU on its way only buzzes (it is already coming). */
static int mu_f4_partner_start(void)
{
    const int want = mu_lobby_partner_want();
    unsigned char invite = MU_LOBBY_OP_INVITE;
    if (!mu_f4_host() || mu_f4_mine() != 0 || mu_f4_searching() || mu_f4.view.seats[1].kind != MU_LOBBY_KIND_OPEN ||
        want == MU_LOBBY_WANT_FIND)
        return 0;
    if (want == MU_LOBBY_WANT_INVITE && !(mu_f4.view.flags & MU_LOBBY_F_INVITING) && mu_f4_send(&invite, 1)) {
        OSReport("doubles: START, invite a friend (a Teams invite for this home)\n");
        sfxForward();
        return 1;
    }
    if (want == MU_LOBBY_WANT_P2 && mu_f4.partner_p2_wait == 0 && mu_f4_p2_claim("START")) {
        sfxForward();
        return 1;
    }
    lbAudioAx_80024030(3);
    OSReport("doubles: START on %s does nothing\n", mu_f4_want_names[want]);
    return 1;
}

/* START on the lobby's select (the cursor think's START hunk): only the host's, and only when nothing
 * blocks it; it goes to the stage select, whose pick goes out with the room's Start
 * (shim/mu_slippi_menu.c lobby_sss_decide). Anything else plays the error sound and says why.
 * `ready` is vanilla's Ready to Fight delay run out (mnCharSel_804D6CF2 == 0). The Friends lobby waits for
 * it; the Doubles home never gets it (its two doors share a team, or one door is open), so the room's
 * hint alone decides there. Returns the trigger the vanilla START code sees: always none. */
static u32 mu_friends4_css_start(u32 trigger, int ready)
{
    if (!(trigger & HSD_PAD_START) || mu_f4.leaving) return 0;
    if (mu_f4_doubles()) {
        /* Doubles: either player, no stage select (Rooms draws the stage). With the partner door empty the
         * search finds a partner and opponents together; with a friend or a CPU, opponents. */
        const unsigned hint = mu_f4.view.block_hint;
        unsigned char start[2] = { MU_LOBBY_OP_START, 0 };
        if (mu_f4_partner_start()) return 0;   /* the host's Invite, P2 or CPU on the empty partner door */
        if (hint != MU_LOBBY_HINT_START_SEARCH && hint != MU_LOBBY_HINT_START_PARTNER) {
            lbAudioAx_80024030(3);
            mu_f4_note(hint);
            return 0;
        }
        if (mu_lobby_send(start, sizeof start)) {
            OSReport("doubles: start, searching (%s)\n",
                     hint == MU_LOBBY_HINT_START_PARTNER ? "partner and opponents" : "opponents");
            sfxForward();
        } else {
            lbAudioAx_80024030(3);
            mu_f4_note(MU_LOBBY_HINT_REFUSED);
        }
        return 0;
    }
    if (mu_f4.mode_pending || mu_f4.mode_seen != ((mu_f4.view.mode & MU_LOBBY_M_SQUAD) | (mu_f4.view.rules.teams ? 1 : 0))) {
        lbAudioAx_80024030(3);
        mu_f4_note(MU_LOBBY_HINT_CHANGED);
        return 0;
    }
    /* Squad refusals must be readable before vanilla has two locked coins. */
    if (!ready && !(mu_f4.view.mode & MU_LOBBY_M_SQUAD)) return 0;
    if (!mu_f4.view_ok || !(mu_f4.view.flags & MU_LOBBY_F_HOST)) {
        lbAudioAx_80024030(3);
        mu_f4_note(mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_KNOWN) ? MU_LOBBY_HINT_WAIT_HOST
                                                                           : MU_LOBBY_HINT_OPENING);
        return 0;
    }
    if (mu_f4.view.block_hint != MU_LOBBY_HINT_PRESS_START) {
        lbAudioAx_80024030(3);
        mu_f4_note(mu_f4.view.block_hint);
        return 0;
    }
    OSReport("friends lobby: host start, to the stage select\n");
    {
        const unsigned char open[2] = { MU_LOBBY_OP_STAGE_SELECT, 1 };   /* the guests go with this host */
        if (!mu_lobby_send(open, sizeof open)) OSReport("friends lobby: stage select open not sent\n");
    }
    mu_f4.leaving = 1;
    mu_lobby_css_exit(MU_LOBBY_EXIT_SSS);
    mnCharSel_804D6CF6 = 1;
    mnCharSel_804D6CF2 = 0xFF;
    gm_801A4B60();
    return 0;
}

/* The HMN/CPU switch and the team box answer only on doors this player controls: the switch on an
 * open or CPU door for the host, the team box on its own door and, for the host, a CPU's (Doubles: its own
 * door and Player 2's; the pair's colour, mu_f4_pair_team, and a CPU partner follows it). Melee's own
 * bounds are taken once, while none is saved: the door may hold an emptied one this code wrote. */
static void mu_f4_bounds(int mine)
{
    int i;
    const int doubles = mu_f4_doubles(), doors = mu_f4_doors_open(), locked = mu_f4_searching();
    for (i = 0; i < 4; ++i) {
        CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
        const unsigned kind = mu_f4.view_ok ? mu_f4.view.seats[i].kind : MU_LOBBY_KIND_OPEN;
        const int host = mu_f4_host() && i != mine && i < doors && !locked;
        /* Doubles: the partner tab also turns with Player 2 in the seat (P2 -> CPU); never on a friend. */
        const int toggle = host && kind != MU_LOBBY_KIND_HUMAN && (doubles || kind != MU_LOBBY_KIND_LOCAL);
        /* Doubles: matchmaking sets who plays whom; the box picks the pair's colour (red, blue or green) on
         * the doors this machine plays, until a match is being set up. */
        const int own = i == mine || (i == 1 && mine == 0 && mu_f4.view_ok && mu_lobby_local_owned(&mu_f4.view));
        const int teambox = doubles ? own && !locked && !(mu_f4.view.flags & MU_LOBBY_F_STARTED)
                                    : i == mine || (host && kind == MU_LOBBY_KIND_CPU);
        if (mu_f4.toggle[i][0] == mu_f4.toggle[i][1] && door->togglebtn_left != door->togglebtn_right) {
            mu_f4.toggle[i][0] = door->togglebtn_left;
            mu_f4.toggle[i][1] = door->togglebtn_right;
        }
        if (mu_f4.teambox[i][0] == mu_f4.teambox[i][1] && door->teambtn_left != door->teambtn_right) {
            mu_f4.teambox[i][0] = door->teambtn_left;
            mu_f4.teambox[i][1] = door->teambtn_right;
        }
        door->togglebtn_left = toggle ? mu_f4.toggle[i][0] : 0;
        door->togglebtn_right = toggle ? mu_f4.toggle[i][1] : 0;
        door->teambtn_left = teambox ? mu_f4.teambox[i][0] : 0;
        door->teambtn_right = teambox ? mu_f4.teambox[i][1] : 0;
        mu_f4.toggle_put[i][0] = door->togglebtn_left;
        mu_f4.toggle_put[i][1] = door->togglebtn_right;
        mu_f4.teambox_put[i][0] = door->teambtn_left;
        mu_f4.teambox_put[i][1] = door->teambtn_right;
        /* A level slider on a door this player doesn't set lets go. */
        if (door->is_hold_cpu_slider && !toggle) {
            int c;
            for (c = 0; c < 4; ++c)
                if (mnCharSel_804A0BC0[c] && mnCharSel_804A0BC0[c]->x5 == 1 && mnCharSel_804A0BC0[c]->x6 == i + 4)
                    mnCharSel_804A0BC0[c]->x5 = 2;
            door->is_hold_cpu_slider = 0;
        }
    }
}

/* Doubles shows two portraits: doors 3 and 4 are hidden whole. The first frame walks each part's joint
 * tree (backdrop, emblem, portrait, team mark, door panels, player label, name plate with its HMN/CPU
 * tab and sliders, KO stars, tag) and keeps every joint's own hidden bit; from then on the same joints are
 * hidden every frame, since the door's redraw (mnCharSel_8025DB34) sets some of them again. Their texts
 * (KO count, name tag, name list, shield label) are hidden every frame too. Their hit areas are already
 * off (mu_f4_bounds), and the lobby gives their ports no pads, so no hand or coin shows there. */
static void mu_f4_shut_text(HSD_Text* text)
{
    int k;
    if (text == NULL) return;
    for (k = 0; k < mu_f4.shut_texts; ++k)
        if (mu_f4.shut_text[k] == text) break;
    if (k == mu_f4.shut_texts && k < MU_F4_SHUT_TEXTS) {
        mu_f4.shut_text[k] = text;
        mu_f4.shut_text_was[k] = text->hidden ? 1 : 0;
        ++mu_f4.shut_texts;
    } else if (k == mu_f4.shut_texts && !(mu_f4.cap_logged & 4)) {
        OSReport("doubles: text hide list full (%d); a later text is hidden but not shown again after\n",
                 MU_F4_SHUT_TEXTS);
        mu_f4.cap_logged |= 4;
    }
    text->hidden = 1;
}
static void mu_f4_shut_tree(HSD_JObj* root)
{
    HSD_JObj* stack[16];
    int n = 0;
    if (root == NULL) return;
    stack[n++] = root;
    while (n > 0) {
        HSD_JObj* j = stack[--n];
        HSD_JObj* c;
        if (mu_f4.shut_count < MU_F4_SHUT_MAX) {
            mu_f4.shut_jobj[mu_f4.shut_count] = j;
            mu_f4.shut_was[mu_f4.shut_count] = (HSD_JObjGetFlags(j) & JOBJ_HIDDEN) ? 1 : 0;
            ++mu_f4.shut_count;
        } else if (!(mu_f4.cap_logged & 1)) {
            /* Once a visit: past the list a joint is hidden this once, not again after a redraw, and stays
             * hidden after Doubles. */
            OSReport("doubles: joint hide list full (%d); later joints are not kept\n", MU_F4_SHUT_MAX);
            mu_f4.cap_logged |= 1;
        }
        HSD_JObjSetFlags(j, JOBJ_HIDDEN);
        for (c = HSD_JObjGetChild(j); c != NULL; c = HSD_JObjGetNext(c)) {
            if (n < 16) {
                stack[n++] = c;
            } else if (!(mu_f4.cap_logged & 2)) {
                OSReport("doubles: a door part's joint tree is wider than 16; some joints stay shown\n");
                mu_f4.cap_logged |= 2;
            }
        }
    }
}
/* The name plates' boxes, the empty boxes under each door (11-11 report: "get rid of those two empty text box
 * controller slots"), are not in any door's joints: MnSlChr draws all four as one joint, MU_F4_PLATES_JOINT
 * (a child of the slider group that holds each door's name slider), with one flat shape a door, in door
 * order. Found by reading MnSlChr.usd's menu tree off the disc (every joint by lb_80011E24 id, then each shape's
 * vertices): 0x55's four shapes span x -29.4..-16.8, -14.0..-1.4, 1.4..14.0 and 16.8..29.4 at y -24.1..-18.3,
 * under doors 1 to 4. Doors 3 and 4's shapes are hidden; the joint is only used when it is that shape (four
 * shapes, under door 1's name slider's group). */
#define MU_F4_PLATES_JOINT 0x55
#define MU_F4_PLATES 4

static void mu_f4_plates_find(void)
{
    HSD_JObj* plates = NULL;
    HSD_JObj* slider = NULL;
    HSD_DObj* d;
    HSD_DObj* shape[MU_F4_PLATES];
    int n = 0, k;
    mu_f4.plates = -1;
    lb_80011E24(mnCharSel_804D6CC0, &plates, MU_F4_PLATES_JOINT, -1);
    lb_80011E24(mnCharSel_804D6CC0, &slider, mnCharSel_803F0DFC.doors[0].slidername_joint, -1);
    if (plates == NULL || slider == NULL || HSD_JObjGetParent(plates) != HSD_JObjGetParent(slider) ||
        (HSD_JObjGetFlags(plates) & JOBJ_INSTANCE))
        return;
    for (d = HSD_JObjGetDObj(plates); d != NULL; d = d->next) {
        if (n == MU_F4_PLATES) return;   /* more than four: not the plates */
        shape[n++] = d;
    }
    if (n != MU_F4_PLATES) return;
    for (k = 0; k < 2; ++k) {
        mu_f4.plate[k] = shape[MU_LOBBY_DOUBLES_SEATS + k];
        mu_f4.plate_was[k] = mu_f4.plate[k]->flags & DOBJ_HIDDEN;
    }
    mu_f4.plates = 2;
}

static void mu_f4_shut(void)
{
    int i, k;
    if (mnCharSel_804D6CC0 == NULL) return;
    if (!mu_f4.shut) {
        mu_f4.shut_count = mu_f4.shut_texts = 0;
        if (mu_f4.plates == 0) mu_f4_plates_find();
        for (i = MU_LOBBY_DOUBLES_SEATS; i < 4; ++i) {
            const CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
            const struct CSSTag* tag = &mnCharSel_803F0E8C[i];
            u8 joints[15];
            int n = 0;
            joints[n++] = door->bg_joint;
            joints[n++] = door->emblem_joint;
            joints[n++] = door->costume_joint;
            joints[n++] = door->team_joint;
            joints[n++] = door->door_joint;
            joints[n++] = door->player_indicator_joint;
            joints[n++] = door->slidername_joint;
            joints[n++] = data2.ko_stars[i].joint;
            joints[n++] = tag->x4;
            /* the name tag's other joints (its list and KO text) */
            joints[n++] = tag->list_joint;
            joints[n++] = tag->name_jointl;
            joints[n++] = tag->x7;
            joints[n++] = tag->kostar_text_joint;
            joints[n++] = door->cpuslider_joint;
            joints[n++] = door->cpuslider2_joint;
            for (k = 0; k < n; ++k) {
                HSD_JObj* jobj = NULL;
                lb_80011E24(mnCharSel_804D6CC0, &jobj, joints[k], -1);
                mu_f4_shut_tree(jobj);
            }
        }
        mu_f4.shut = 1;
        if (!mu_f4.shut_logged) {
            OSReport("doubles: two-door select (doors 3 and 4 hidden, %d joints; their name plates %s)\n",
                     mu_f4.shut_count, mu_f4.plates == 2 ? "hidden" : "not found, left showing");
            mu_f4.shut_logged = 1;
        }
    } else {
        for (k = 0; k < mu_f4.shut_count; ++k) HSD_JObjSetFlags(mu_f4.shut_jobj[k], JOBJ_HIDDEN);
    }
    if (mu_f4.plates == 2)
        for (k = 0; k < 2; ++k) mu_f4.plate[k]->flags |= DOBJ_HIDDEN;
    for (i = MU_LOBBY_DOUBLES_SEATS; i < 4; ++i) {
        CSSTagData* tag = mnCharSel_803F0E8C[i].data;
        mu_f4_shut_text(data2.ko_stars[i].text);
        if (tag != NULL) {
            mu_f4_shut_text(tag->text);
            mu_f4_shut_text(tag->name_ls);
        }
        mu_f4_shut_text(mu_te_shield_text[i]);
    }
}
/* Back to four doors: each joint and text gets its own hidden bit back, then the door draws itself again
 * (its team mark, name tag and KO stars by the screen's own rules). */
static void mu_f4_unshut(void)
{
    int i, k;
    if (!mu_f4.shut) return;
    for (k = 0; k < mu_f4.shut_count; ++k)
        if (!mu_f4.shut_was[k]) HSD_JObjClearFlags(mu_f4.shut_jobj[k], JOBJ_HIDDEN);
    for (k = 0; k < mu_f4.shut_texts; ++k) mu_f4.shut_text[k]->hidden = mu_f4.shut_text_was[k];
    if (mu_f4.plates == 2)
        for (k = 0; k < 2; ++k)
            if (!mu_f4.plate_was[k]) mu_f4.plate[k]->flags &= ~(unsigned) DOBJ_HIDDEN;
    mu_f4.shut = 0;
    mu_f4.shut_count = mu_f4.shut_texts = mu_f4.plates = 0;
    if (mu_friends_css_frames >= 2)
        for (i = MU_LOBBY_DOUBLES_SEATS; i < 4; ++i) mnCharSel_8025DB34((u8) i);
    OSReport("doubles: four-door select again (doors 3 and 4 shown)\n");
}
/* The Rules overlay and name entry free the screen and build it anew: the kept joints are gone. */
static void mu_f4_shut_forget(void)
{
    mu_f4.shut = 0;
    mu_f4.shut_count = mu_f4.shut_texts = mu_f4.plates = 0;
}

/* Any other select gets its switches and team boxes back (the bounds are static) and doors 3 and 4. A
 * bound goes back where it is emptied or still holds what mu_f4_bounds wrote: anything else is another
 * select's own (mu_doubles_css). */
static void mu_f4_restore(void)
{
    int i;
    mu_f4.art = 0;   /* the partner door's portrait (mu_f4_art_draw) */
    mu_f4_marks_off(mnCharSel_804D6CF6 >= 3);   /* its tag and caption plate; past 3 their texts are freed */
    if (mnCharSel_804D6CF6 >= 3) {
        mu_f4_shut_forget();
    } else {
        mu_f4_unshut();
    }
    for (i = 0; i < 4; ++i) {
        CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
        if (mu_f4.toggle[i][0] != mu_f4.toggle[i][1] &&
            (door->togglebtn_left == door->togglebtn_right ||
             (door->togglebtn_left == mu_f4.toggle_put[i][0] && door->togglebtn_right == mu_f4.toggle_put[i][1]))) {
            door->togglebtn_left = mu_f4.toggle[i][0];
            door->togglebtn_right = mu_f4.toggle[i][1];
        }
        if (mu_f4.teambox[i][0] != mu_f4.teambox[i][1] &&
            (door->teambtn_left == door->teambtn_right ||
             (door->teambtn_left == mu_f4.teambox_put[i][0] && door->teambtn_right == mu_f4.teambox_put[i][1]))) {
            door->teambtn_left = mu_f4.teambox[i][0];
            door->teambtn_right = mu_f4.teambox[i][1];
        }
        mu_f4.toggle[i][0] = mu_f4.toggle[i][1] = mu_f4.teambox[i][0] = mu_f4.teambox[i][1] = 0;
        mu_f4.toggle_put[i][0] = mu_f4.toggle_put[i][1] = mu_f4.teambox_put[i][0] = mu_f4.teambox_put[i][1] = 0;
    }
}

static unsigned char mu_f4_u8(unsigned v, unsigned max) { return (unsigned char) (v > max ? max : v); }

/* How many of the fighter's colours the room carries: this build's count (Melee's X/Y cycle's own, m-ex
 * included), at most MU_LOBBY_COLOR_MAX + 1, as COSTUMES sends it (mu_slippi_menu.c). */
static unsigned mu_f4_colors(unsigned ch)
{
    const unsigned n = ch <= MU_LOBBY_CHAR_MAX ? gm_GetNumCostumesForCKind((u8) ch) : 0;
    return n < 1 ? 1 : n > MU_LOBBY_COLOR_MAX + 1 ? MU_LOBBY_COLOR_MAX + 1 : n;
}

/* A colour the room carries for the fighter, else its last: the same rule the match setup applies
 * (yougame_lobby_setup.h costume_for), so the door and every machine's setup hash agree. */
static unsigned char mu_f4_color(unsigned ch, unsigned color)
{
    const unsigned n = mu_f4_colors(ch);
    return (unsigned char) (color < n ? color : n - 1);
}

/* The colour door i's own X/Y cycle sends (this player's door, or the host's CPU doors): mu_f4_color, except
 * on an m-ex fighter with more costumes than the room carries (7th, 8th). There Y from the first colour wraps
 * to the fighter's last and lands on the room's last, and X from the room's last steps to the first, so
 * both directions keep cycling. */
static unsigned char mu_f4_door_color(int i, unsigned ch, unsigned costume)
{
    const unsigned n = mu_f4_colors(ch);
    unsigned char c = mu_f4_color(ch, costume);
    if (costume > MU_LOBBY_COLOR_MAX && costume == n && mu_f4.door_color[i] == n - 1) c = 0;
    mu_f4.door_color[i] = c;
    return c;
}

/* This player's own door turns HMN in the first frames, before the screen can draw it, so it already says HMN
 * when drawing is possible and nothing draws it again: Melee's N/A stayed on it. Draw it once, when it can be. */
static void mu_f4_own_drawn(int mine)
{
    if (mu_friends_css_frames < 2 || mine < 0 || mine > 3 || mu_f4.own_drawn == mine + 1 ||
        mnCharSel_803F0DFC.doors[mine].p_kind != Gm_PKind_Human)
        return;
    mu_f4.own_drawn = (unsigned char) (mine + 1);
    OSReport("friends lobby: own door %d drawn as HMN\n", mine);
    mnCharSel_8025DB34((u8) mine);
}

/* This player's own door: a person, and its hover / pick / unpick go to the room when they change. */
static void mu_f4_local(CSSData* css, int mine)
{
    CSSDoor* door = &mnCharSel_803F0DFC.doors[mine];
    struct CSSCharModel* coin = mnCharSel_804A0BD0[mine];
    PlayerInitData* p = &css->vs.start.players[mine];
    unsigned char* sent = mine == mu_f4.view.my_seat ? mu_f4.sent : mu_f4.partner_sent;
    unsigned char req[4];
    unsigned size = 1;
    if (door->p_kind != Gm_PKind_Human) {
        door->p_kind = door->p_kind_prev = Gm_PKind_Human;
        p->slot_type = Gm_PKind_Human;
        mnCharSel_8025DB34((u8) mine);
    }
    if (coin == NULL) return;
#if MU_SQUAD
    if (gmSquad_LobbyChosen()) {
        GmSquadMember line[GMSQUAD_SIZE];
        unsigned char request[10] = { MU_LOBBY_OP_LINEUP, 0 };
        int j;
        if (mu_f4.view.flags & MU_LOBBY_F_STARTED) return;
        request[1] = (unsigned char) gmSquad_Lineup(mine, squad_coin_pick(mine), squad_coin_color(mine), line);
        for (j = 0; j < request[1]; ++j) {
            request[2 + 2*j] = line[j].ckind; request[3 + 2*j] = line[j].color;
        }
        if (memcmp(request, mu_f4.lineup_sent, sizeof request) != 0 && mu_f4_send(request, sizeof request))
            memcpy(mu_f4.lineup_sent, request, sizeof request);
        return;
    }
#endif
    /* A frozen setup: the picks are the room's now; nothing goes out (and coming back from the stage
     * select never sends the pick again). */
    if (mu_f4.view.flags & MU_LOBBY_F_STARTED) return;
    if (mu_f4_searching()) return;   /* Doubles: locked while Rooms searches */
    req[0] = MU_F4_IDLE;
    req[1] = req[2] = req[3] = 0;
    if (coin->x5 == 0 && (unsigned) p->ckind <= MU_LOBBY_CHAR_MAX) {
        req[0] = MU_LOBBY_OP_PICK;
        req[1] = (unsigned char) p->ckind;
    } else if (coin->x5 != 0 && door->sel_icon < CSS_NONE && icons[door->sel_icon].char_kind <= MU_LOBBY_CHAR_MAX) {
        req[0] = MU_LOBBY_OP_HOVER;
        req[1] = icons[door->sel_icon].char_kind;
    }
    if (req[0] != MU_F4_IDLE) {
        req[2] = mu_f4_door_color(mine, req[1], door->costume);
        req[3] = mu_f4_u8(door->team, MU_LOBBY_TEAM_MAX);
        size = 4;
        /* An m-ex fighter with more costumes than the room carries (7th, 8th): the door shows the colour
         * that goes out and plays, as X/Y would draw it (mnCharSel_8025DB34 redraws the door). */
        if (door->costume != req[2] && door->costume > MU_LOBBY_COLOR_MAX) {
            door->costume = req[2];
            mnCharSel_8025DB34((u8) mine);
        }
    }
    if (memcmp(req, sent, size) == 0) return;
    if (req[0] == MU_LOBBY_OP_PICK) {
        /* A new visit of the select (nothing sent yet) whose pick the room already has: no resend. Only
         * then: after this visit's own UNPICK the view may still show the old pick for a moment. */
        const MuLobbySeat* me = &mu_f4.view.seats[mine];
        if (sent[0] == 0xFF && (me->flags & MU_LOBBY_S_PICKED) && me->pick_ch == req[1] && me->pick_color == req[2] &&
            me->pick_team == req[3]) {
            memcpy(sent, req, size);
            return;
        }
    } else if (sent[0] == MU_LOBBY_OP_PICK) {
        /* A pick taken back up: UNPICK, only ever after a PICK; the hover follows on the next frame. */
        req[0] = MU_LOBBY_OP_UNPICK;
        size = 1;
    } else if (req[0] == MU_F4_IDLE) {
        /* Off the icons after a hover, or nothing yet: nothing to take back, nothing goes out. */
        sent[0] = MU_F4_IDLE;
        return;
    }
    if (mine != mu_f4.view.my_seat) {
        unsigned char local[4];
        memcpy(local, req, size);
        local[0] = req[0] == MU_LOBBY_OP_HOVER ? MU_LOBBY_OP_LOCAL_HOVER :
                   req[0] == MU_LOBBY_OP_PICK ? MU_LOBBY_OP_LOCAL_PICK : MU_LOBBY_OP_LOCAL_UNPICK;
        if (mu_f4_send(local, size)) memcpy(sent, req, size);
    } else if (mu_f4_send(req, size)) memcpy(sent, req, size);
}

static unsigned char mu_f4_kind(unsigned lobby_kind)
{
    return (lobby_kind == MU_LOBBY_KIND_HUMAN || lobby_kind == MU_LOBBY_KIND_LOCAL) ? Gm_PKind_Human
         : lobby_kind == MU_LOBBY_KIND_CPU   ? Gm_PKind_Cpu
                                             : Gm_PKind_NA;
}

static int mu_f4_icon_of(unsigned ch)
{
    int j;
    for (j = 0; j < CSS_NONE; ++j)
        if (icons[j].char_kind == ch) return j;
    return CSS_NONE;
}

/* What a door shows from its seat; a change draws it again. Never 0. */
static unsigned mu_f4_sig(const MuLobbySeat* s)
{
    const int picked = (s->flags & MU_LOBBY_S_PICKED) != 0, hovering = (s->flags & MU_LOBBY_S_HOVERING) != 0;
    const unsigned ch = picked ? s->pick_ch : hovering ? s->hover_ch : 0xFF;
    const unsigned color = picked ? s->pick_color : hovering ? s->hover_color : 0;
    const unsigned team = picked ? s->pick_team : hovering ? s->hover_team : 0;
    return 0x80000000u | ((unsigned) s->kind << 26) | ((unsigned) (s->level & 15) << 22) |
           ((unsigned) picked << 21) | ((unsigned) hovering << 20) | ((ch & 0xFF) << 8) | ((color & 7) << 4) |
           (team & 3);
}

/* The door already shows the seat (the host's own change, now in the view): nothing to draw. */
static int mu_f4_door_shows(CSSData* css, int i, const MuLobbySeat* s)
{
    CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
    PlayerInitData* p = &css->vs.start.players[i];
    struct CSSCharModel* coin = mnCharSel_804A0BD0[i];
    if (door->p_kind != mu_f4_kind(s->kind)) return 0;
    if (s->kind == MU_LOBBY_KIND_CPU && p->cpu_level != s->level) return 0;
    if (s->kind == MU_LOBBY_KIND_OPEN) return 1;
    if (s->flags & MU_LOBBY_S_PICKED)
        return coin && coin->x5 == 0 && (unsigned) p->ckind == s->pick_ch &&
               door->costume == mu_f4_color(s->pick_ch, s->pick_color) && door->team == s->pick_team;
    return s->kind == MU_LOBBY_KIND_CPU && (unsigned) p->ckind > MU_LOBBY_CHAR_MAX;
}

/* Another seat's door, from the view. */
static void mu_f4_draw(CSSData* css, int i, const MuLobbySeat* s)
{
    CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
    PlayerInitData* p = &css->vs.start.players[i];
    struct CSSCharModel* coin = mnCharSel_804A0BD0[i];
    struct CSSCursorData* hand = mnCharSel_804A0BC0[i];
    const unsigned char kind = mu_f4_kind(s->kind);
    door->p_kind = door->p_kind_prev = kind;
    mu_f4.kind_seen[i] = kind;
    p->slot_type = kind;
    if (s->kind == MU_LOBBY_KIND_CPU && s->level >= 1 && s->level <= 9) p->cpu_level = s->level;
    if (coin == NULL || hand == NULL) {
        mnCharSel_8025DB34((u8) i);
        return;
    }
    if (s->kind != MU_LOBBY_KIND_OPEN && (s->flags & MU_LOBBY_S_PICKED) && mu_f4_icon_of(s->pick_ch) < CSS_NONE) {
        /* The coin goes down on the fighter, as the player's own drop does (Melee's B-return helper). */
        if (hand->x5 == 1 && hand->x6 == i) hand->x5 = 2;
        p->ckind = (s8) s->pick_ch;
        p->team = door->team = s->pick_team;
        {
            /* Redrawing the room's picks when the screen is built isn't anyone picking: no pick sounds or name
             * calls. 3 is the value FDEC already keeps quiet on, and sound is all it decides there. */
            const u8 scene = mnCharSel_804D6CF6;
            if (mu_friends_css_frames < 2 || mu_lobby_curtain()) mnCharSel_804D6CF6 = 3;
            mnCharSel_8025FDEC((u8) i);
            mnCharSel_804D6CF6 = scene;
        }
        door->costume = mu_f4_color(s->pick_ch, s->pick_color);
        p->color = door->costume;
    } else {
        p->ckind = ChKind_None;
        if (s->kind == MU_LOBBY_KIND_HUMAN || s->kind == MU_LOBBY_KIND_LOCAL) {
            /* A person's coin in their hand. The hand stays parked: only their door shows what they hover. */
            const int icon = (s->flags & MU_LOBBY_S_HOVERING) ? mu_f4_icon_of(s->hover_ch) : CSS_NONE;
            if (coin->x5 == 0) {
                coin->x5 = (u8) (i + 1);
                hand->x5 = 1;
                hand->x6 = (u8) i;
                HSD_GObjGXLink_803909D8(coin->gobj, hand->gobj);
            }
            if (icon < CSS_NONE) {
                door->costume = mu_f4_color(s->hover_ch, s->hover_color);
                p->team = door->team = s->hover_team;
            }
            door->sel_icon = (u8) icon;
        } else {
            door->sel_icon = CSS_NONE;   /* an open door, or a CPU with no fighter yet */
        }
    }
    /* The door's update makes the cursor's sound and resets the colour when these differ. Another
     * player's hover only changes the portrait. */
    door->sel_icon_prev = door->sel_icon;
    mnCharSel_8025DB34((u8) i);
}

/* Another person's hand (cursor think): their door comes from the view and the hand stays parked. */
static int mu_friends4_view_hand(u8 port)
{
    if (!mu_lobby_css() || !mu_f4.view_ok || !(mu_f4.view.flags & MU_LOBBY_F_KNOWN) || port >= MU_LOBBY_SEATS)
        return 0;
    if (port == mu_f4_mine() || (port == 1 && mu_lobby_local_owned(&mu_f4.view))) return 0;
    return mu_f4.view.seats[port].kind == MU_LOBBY_KIND_HUMAN || mu_f4.view.seats[port].kind == MU_LOBBY_KIND_LOCAL;
}

/* Doubles: one colour for the pair (build 96 report). Melee's team box on this machine's own door, and on
 * Player 2's, picks it, and every door of the pair wears it: Player 2 and a CPU partner at once, and a friend
 * partner through the pick or hover the select already sends (it carries the door's colour). With a friend the
 * host's machine (seat 0) holds the colour: the guest wears whatever the host's door shows, and the host takes a
 * guest's new colour as a press, in the order presses reach it (the last one stands). The guest's door also shows
 * the host's colours as they reach it, a moment late, so the host passes over a colour it wore itself in the last
 * second (mu_f4.pair_worn) until that second is up: by then the guest's door shows the host's latest, and a press
 * the guest really made to that colour is still there and is taken then. A guest's press wears its colour until
 * the host's door turns to it, or 2 s after it went out (a press made with the coin in hand, off the icons, goes out
 * with the next hover or pick, and waits until then). The first colour the host sees from a guest is taken as a
 * press unless the host's own colour went out less than a second before (then the guest may not have seen it yet).
 * Each machine paints the friend's door in the pair's colour too (mu_f4_pair_paint). The match never
 * trusts this: each room team plays its lowest seat's colour, the same on every machine (yougame_lobby_setup.h
 * doubles_colors), and both lock in with the colour their door wears (mu_f4_lock_in). */
static void mu_f4_pair_team(CSSData* css, int mine)
{
    const MuLobbySeat* partner;
    int doors[2], n = 0, k, cpu = 0, out;
    unsigned t;
    if (!mu_f4_doubles() || mine > 1 || mu_f4_searching() || (mu_f4.view.flags & MU_LOBBY_F_STARTED)) return;
    doors[n++] = mine;
    if (mine == 0 && mu_lobby_local_owned(&mu_f4.view)) doors[n++] = 1;
    else if (mine == 0 && mu_f4_host() && mu_f4.view.seats[1].kind == MU_LOBBY_KIND_CPU &&
             mnCharSel_803F0DFC.doors[1].p_kind == Gm_PKind_Cpu)
        doors[n++] = cpu = 1;   /* the CPU follows the pair; its box stays shut (mu_f4_bounds) */
    if (n == 1 && mine == 0) mu_f4.pair_seen[1] = 0xFF;   /* door 2 is a friend's or empty: not ours */
    if (mu_f4.pair_team > MU_LOBBY_TEAM_MAX) {
        const unsigned worn = mnCharSel_803F0DFC.doors[mine].team;
        mu_f4.pair_team = (unsigned char) (worn <= MU_LOBBY_TEAM_MAX ? worn : 0);
    }
    /* This machine's own hover or pick, as mu_f4_local last sent it (it runs after this, so last frame's). */
    out = mu_f4.sent[0] == MU_LOBBY_OP_HOVER || mu_f4.sent[0] == MU_LOBBY_OP_PICK;
    if (mine == 0 && (out || mu_f4.pair_shown > 0) && mu_f4.pair_shown < MU_F4_HOLD) ++mu_f4.pair_shown;
    if (mu_f4.pair_unsent && out && mu_f4.sent[3] == mu_f4.pair_team) {
        mu_f4.pair_unsent = 0;   /* the guest's press is out: now it waits for the host */
        mu_f4.pair_wait = 2 * MU_F4_HOLD;
    }
    /* This machine's own press: a door it plays whose box turned since the colour was put on it. A door that
     * just became ours (Player 2 sat down) only takes the pair's colour; a CPU's door is never read (the view
     * may redraw it). */
    for (k = 0; k < n; ++k) {
        const int i = doors[k];
        const unsigned team = mnCharSel_803F0DFC.doors[i].team;
        if (mu_f4.pair_seen[i] > MU_LOBBY_TEAM_MAX) continue;
        if (team != mu_f4.pair_seen[i] && team <= MU_LOBBY_TEAM_MAX && team != mu_f4.pair_team) {
            mu_f4.pair_team = (unsigned char) team;
            mu_f4.pair_unsent = (unsigned char) (mine == 1);   /* the guest's wait starts once it goes out */
            mu_f4.pair_wait = 0;
            OSReport("doubles: the pair's colour is now %u (this machine)\n", team);
            break;
        }
    }
    /* The friend partner: its pick or hover. */
    partner = &mu_f4.view.seats[1 - mine];
    t = partner->kind != MU_LOBBY_KIND_HUMAN ? 0xFF
      : (partner->flags & MU_LOBBY_S_PICKED) ? partner->pick_team
      : (partner->flags & MU_LOBBY_S_HOVERING) ? partner->hover_team : 0xFF;
    if (partner->kind != MU_LOBBY_KIND_HUMAN) {
        mu_f4.partner_team_seen = 0xFF;
        mu_f4.pair_wait = 0;
        mu_f4.pair_unsent = 0;
    } else if (t <= MU_LOBBY_TEAM_MAX && mine == 1) {
        /* The guest wears the host's colour once the host's door turns to its press (not one it showed already:
         * that may be from before the host saw the press), or the press has waited 2 s since it went out. */
        const int moved = t != mu_f4.partner_team_seen;
        mu_f4.partner_team_seen = (unsigned char) t;
        if (t == mu_f4.pair_team) {
            if (moved) mu_f4.pair_wait = mu_f4.pair_unsent = 0;
        } else if (mu_f4.pair_wait == 0 && !mu_f4.pair_unsent) {
            mu_f4.pair_team = (unsigned char) t;
            OSReport("doubles: the pair's colour is now %u (the host's)\n", t);
        }
    } else if (t <= MU_LOBBY_TEAM_MAX) {
        /* The host takes the guest's presses; one the host wore in the last second waits (it may be the guest
         * still showing it). The first colour seen is a press too, unless the host's own went out less than a
         * second before: then it may be the guest's door from before it saw the host's. */
        if (t == mu_f4.pair_team ||
            (mu_f4.partner_team_seen > MU_LOBBY_TEAM_MAX && mu_f4.pair_shown > 0 && mu_f4.pair_shown < MU_F4_HOLD)) {
            mu_f4.partner_team_seen = (unsigned char) t;
        } else if (t != mu_f4.partner_team_seen && mu_f4.pair_worn[t] == 0) {
            mu_f4.pair_team = (unsigned char) t;
            mu_f4.partner_team_seen = (unsigned char) t;
            OSReport("doubles: the pair's colour is now %u (the guest's)\n", t);
        }
    }
    if (mu_f4.pair_wait > 0) --mu_f4.pair_wait;
    for (k = 0; k <= MU_LOBBY_TEAM_MAX; ++k)
        if (k == mu_f4.pair_team) mu_f4.pair_worn[k] = MU_F4_HOLD;
        else if (mu_f4.pair_worn[k] > 0) --mu_f4.pair_worn[k];
    for (k = 0; k < n; ++k) {
        const int i = doors[k];
        CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
        mu_f4.pair_seen[i] = cpu && i == 1 ? 0xFF : mu_f4.pair_team;   /* a CPU's door is never read */
        if (door->team == mu_f4.pair_team) continue;
        door->team = mu_f4.pair_team;
        css->vs.start.players[i].team = mu_f4.pair_team;
        mnCharSel_8025DB34((u8) i);
    }
}

/* Doubles: the friend partner's door wears the pair's colour too, painted after the view drew it (mu_f4_doors);
 * only painted: what that machine sends is its own. */
static void mu_f4_pair_paint(CSSData* css, int mine)
{
    const int i = 1 - mine;
    CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
    if (!mu_f4_doubles() || mine > 1 || mu_f4_searching() || (mu_f4.view.flags & MU_LOBBY_F_STARTED)) return;
    if (mu_f4.pair_team > MU_LOBBY_TEAM_MAX || mu_f4.view.seats[i].kind != MU_LOBBY_KIND_HUMAN) return;
    if (door->p_kind != Gm_PKind_Human || door->team == mu_f4.pair_team) return;
    door->team = mu_f4.pair_team;
    css->vs.start.players[i].team = mu_f4.pair_team;
    mnCharSel_8025DB34((u8) i);
}

/* The host's changes to an open or CPU door go to the room: the switch (open <-> CPU), the level, and
 * the CPU's coin. The door keeps the host's change for a moment while the view catches up. */
static void mu_f4_host_doors(CSSData* css, int mine)
{
    int i;
    if (!mu_f4_host() || mu_f4_searching()) return;
    for (i = 0; i < mu_f4_doors_open(); ++i) {   /* Doubles: the partner door only */
        CSSDoor* door = &mnCharSel_803F0DFC.doors[i];
        PlayerInitData* p = &css->vs.start.players[i];
        struct CSSCharModel* coin = mnCharSel_804A0BD0[i];
        const MuLobbySeat* s = &mu_f4.view.seats[i];
        unsigned char req[5];
        int cpu, level;
        if (i == mine || s->kind == MU_LOBBY_KIND_HUMAN || s->kind == MU_LOBBY_KIND_LOCAL) continue;
        if (door->p_kind == Gm_PKind_Human) {
            if (mu_f4.kind_seen[i] == 0xFF || mu_f4.kind_seen[i] == Gm_PKind_Human) {
                /* HMN that no press of the host's made (a door the view hasn't redrawn): draw it again. */
                mu_f4.sig[i] = 0;
                continue;
            }
            OSReport("friends lobby: seat %d's empty door read as HMN, taken as the host's press\n", i);
            door->p_kind = door->p_kind_prev = Gm_PKind_Cpu;   /* the host's press: an empty port is never HMN */
            p->slot_type = Gm_PKind_Cpu;
            mu_f4.kind_seen[i] = Gm_PKind_Cpu;
        }
        cpu = door->p_kind != Gm_PKind_NA;
        level = p->cpu_level < 1 ? 1 : p->cpu_level > 9 ? 9 : p->cpu_level;
        req[0] = MU_LOBBY_OP_SEAT;
        req[1] = (unsigned char) i;
        req[2] = (unsigned char) (cpu ? level : 0);
        if ((cpu != (s->kind == MU_LOBBY_KIND_CPU) || (cpu && level != s->level)) &&
            (mu_f4.seat_sent[i][0] != (cpu ? 1 : 0) || mu_f4.seat_sent[i][1] != req[2])) {
            if (mu_f4_send(req, 3)) {
                OSReport("friends lobby: sent seat %d %s level %d (door kind %d, seat kind %d)\n", i,
                         cpu ? "cpu" : "open", (int) req[2], (int) door->p_kind, (int) s->kind);
                mu_f4.seat_sent[i][0] = (unsigned char) (cpu ? 1 : 0);
                mu_f4.seat_sent[i][1] = req[2];
                mu_f4.hold[i] = MU_F4_HOLD;
            }
        }
        if (!cpu || coin == NULL || s->kind != MU_LOBBY_KIND_CPU || (mu_f4.view.mode & MU_LOBBY_M_SQUAD)) continue;
        req[0] = MU_LOBBY_OP_CPU_PICK;
        req[1] = (unsigned char) i;
        if (coin->x5 == 0 && (unsigned) p->ckind <= MU_LOBBY_CHAR_MAX) {
            req[2] = (unsigned char) p->ckind;
            req[3] = mu_f4_door_color(i, req[2], door->costume);
        } else {
            req[2] = MU_LOBBY_NONE;
            req[3] = 0;
        }
        req[4] = mu_f4_u8(door->team, MU_LOBBY_TEAM_MAX);
        {
            const int picked = (s->flags & MU_LOBBY_S_PICKED) != 0;
            const int same = req[2] == MU_LOBBY_NONE ? !picked
                                                     : picked && s->pick_ch == req[2] && s->pick_color == req[3] &&
                                                           s->pick_team == req[4];
            if (same || memcmp(mu_f4.cpu_sent[i], req + 2, 3) == 0) continue;
        }
        if (mu_f4_send(req, 5)) {
            memcpy(mu_f4.cpu_sent[i], req + 2, 3);
            mu_f4.hold[i] = MU_F4_HOLD;
        }
    }
}

/* The other doors from the view, each drawn again only when its seat changed. */
static void mu_f4_doors(CSSData* css, int mine)
{
    int i;
    for (i = 0; i < 4; ++i) {
        const MuLobbySeat* s = &mu_f4.view.seats[i];
        unsigned sig;
        int reopen;
        if (i == mine || (i == 1 && mu_lobby_local_owned(&mu_f4.view))) continue;
        if (mu_f4.hold[i] > 0) {
            /* The host's change held the door; when the hold runs out the door is drawn from the view again. */
            if (--mu_f4.hold[i] == 0) mu_f4.sig[i] = 0;
            continue;
        }
        sig = mu_f4_sig(s);
        /* The first cursor think after the select is rebuilt (back from the stage select) still sees the
         * last screen's pads, so a port with no controller closes its door once, after the build drew it.
         * Nothing else here closes a person's door: draw it again. */
        reopen = mu_friends_css_frames >= 2 && s->kind == MU_LOBBY_KIND_HUMAN &&
                 mnCharSel_803F0DFC.doors[i].p_kind != Gm_PKind_Human;
        if (sig == mu_f4.sig[i] && !reopen) continue;
        if (reopen) OSReport("friends lobby: seat %d's door was closed, drawn again\n", i);
        if (mu_f4.kind_logged[i] != s->kind + 1) {
            mu_f4.kind_logged[i] = (unsigned char) (s->kind + 1);
            OSReport("friends lobby: seat %d is now kind %d (door kind %d)\n", i, (int) s->kind,
                     (int) mnCharSel_803F0DFC.doors[i].p_kind);
        }
        mu_f4.sig[i] = sig;
        /* The host's own sends are settled once the view shows them; the next change sends again. */
        mu_f4.seat_sent[i][0] = mu_f4.cpu_sent[i][0] = 0xFF;
        if (mu_friends_css_frames < 2 || !mu_f4_door_shows(css, i, s)) mu_f4_draw(css, i, s);
    }
}

static void mu_f4_rules_local(CSSData* css, MuLobbyRules* r)
{
    GameRules* g = gmMainLib_GetGameRules();
    r->teams = css->vs.start.rules.is_teams ? 1 : 0;
    r->ff = g->friendly_fire ? 1 : 0;
    r->time = g->mode == 0 ? 1 : 0;   /* Coin and Bonus (2, 3) play as Stock */
    r->stocks = g->stock_count < MU_LOBBY_RULE_MIN ? MU_LOBBY_RULE_MIN : mu_f4_u8(g->stock_count, MU_LOBBY_RULE_MAX);
    /* Time mode's limit (1-99), or Stock's own: 0 there is no timer, as in Melee's Rules screen. */
    if (r->time)
        r->minutes = g->time_limit < MU_LOBBY_RULE_MIN ? MU_LOBBY_RULE_MIN : mu_f4_u8(g->time_limit, MU_LOBBY_RULE_MAX);
    else
        r->minutes = mu_f4_u8(g->stock_time_limit, MU_LOBBY_RULE_MAX);
    r->pause = g->pause ? 1 : 0;
}
static int mu_f4_rules_same(const MuLobbyRules* a, const MuLobbyRules* b)
{
    return a->teams == b->teams && a->ff == b->ff && a->time == b->time && a->stocks == b->stocks &&
           a->minutes == b->minutes && a->pause == b->pause;
}

/* The host's rules (its Rules screen and Teams toggle) go to the room; everyone else plays the room's,
 * written into the same GameRules and Teams this screen and the match read. */
static void mu_f4_rules(CSSData* css)
{
    const MuLobbyRules* room = &mu_f4.view.rules;
    unsigned char bytes[MU_LOBBY_RULES_SIZE];
    if (mu_f4_host() && !mu_f4_doubles() && !mu_f4.mode_pending && !mu_f4.rules_sync &&
        !(mu_f4.view.mode & MU_LOBBY_M_SQUAD)) {   /* Doubles: the room's fixed rules, for both players */
        MuLobbyRules mine;
        unsigned char req[1 + MU_LOBBY_RULES_SIZE];
        mu_f4_rules_local(css, &mine);
        if (mu_f4_rules_same(&mine, room) || (mu_f4.rules_sent_ok && mu_f4_rules_same(&mine, &mu_f4.rules_sent))) return;
        req[0] = MU_LOBBY_OP_RULES;
        req[1] = mine.teams; req[2] = mine.ff; req[3] = mine.time;
        req[4] = mine.stocks; req[5] = mine.minutes; req[6] = mine.pause;
        if (mu_f4_send(req, sizeof req)) {
            mu_f4.rules_sent = mine;
            mu_f4.rules_sent_ok = 1;
        }
        return;
    }
    if ((mu_f4.view.flags & MU_LOBBY_F_HOST) && !mu_f4_doubles() && !mu_f4.mode_pending && !mu_f4.rules_sync &&
        !(mu_f4.view.mode & MU_LOBBY_M_SQUAD)) return;   /* the host while a setup is frozen: its own stand */
    if (mu_f4.rules_drawn_ok && mu_f4_rules_same(room, &mu_f4.rules_drawn)) return;
    bytes[0] = room->teams; bytes[1] = room->ff; bytes[2] = room->time;
    bytes[3] = room->stocks; bytes[4] = room->minutes; bytes[5] = room->pause;
    if (!mu_lobby_rules_ok(bytes)) return;
    {
        GameRules* g = gmMainLib_GetGameRules();
        const int teams_changed = css->vs.start.rules.is_teams != room->teams;
        int i;
        g->friendly_fire = room->ff;
        g->mode = room->time ? 0 : 1;
        g->stock_count = room->stocks;
        if (room->time)
            g->time_limit = room->minutes;
        else
            g->stock_time_limit = room->minutes;   /* 0: no timer */
        g->pause = room->pause;
        css->vs.start.rules.is_teams = room->teams;
        mu_f4.rules_drawn = *room;
        mu_f4.rules_drawn_ok = 1;
        mnCharSel_8025EE8C(css->match_type);
        if (teams_changed)
            for (i = 0; i < 4; ++i) mnCharSel_8025DB34((u8) i);
    }
}

/* Where a door's body is, below its HMN/CPU switch: the switch's left edge to the next door's, from
 * Melee's bounds. 2: on the door's tab (its switch, above). */
static int mu_f4_over_door(int i, float x, float y)
{
    float left = mu_f4.toggle[i][0], width = 0;
    if (mu_f4.toggle[i][0] == mu_f4.toggle[i][1]) return 0;
    if (i < 3 && mu_f4.toggle[i + 1][0] != mu_f4.toggle[i + 1][1]) width = mu_f4.toggle[i + 1][0] - mu_f4.toggle[i][0];
    else if (i > 0 && mu_f4.toggle[i - 1][0] != mu_f4.toggle[i - 1][1]) width = mu_f4.toggle[i][0] - mu_f4.toggle[i - 1][0];
    if (width <= 0) width = 15.0f;
    if (!(x > left - 1.0f && x < left + width - 2.0f)) return 0;
    return y > -22.0f && y < -4.6f ? 1 : y >= -4.6f && y < 0.2f ? 2 : 0;
}

/* A pressed on any port in the lobby: what the invite needs, so a press that does nothing says why.
 * Once per press (trigger), so it never floods the log. */
static void mu_f4_log_a(int mine)
{
    int c, i;
    for (c = 0; c < 4; ++c) {
        struct CSSCursorData* hand = mnCharSel_804A0BC0[c];
        if (!(HSD_PadCopyStatus[c].trigger & HSD_PAD_A)) continue;
        OSReport("friends lobby: A on port %d, mine %d, my seat %d, view %d, flags 0x%02x, hand %s %.1f,%.1f held %d/%d\n",
                 c, mine, (int) mu_f4.view.my_seat, mu_f4.view_ok, (unsigned) mu_f4.view.flags, hand ? "at" : "none",
                 hand ? hand->xC : 0.0f, hand ? hand->x10 : 0.0f, hand ? (int) hand->x5 : -1, hand ? (int) hand->x6 : -1);
        for (i = 0; hand && i < MU_LOBBY_SEATS; ++i)
            OSReport("friends lobby:   door %d kind %d switch %.1f..%.1f hit %d\n", i + 1, (int) mu_f4.view.seats[i].kind,
                     mu_f4.toggle[i][0], mu_f4.toggle[i][1], mu_f4_over_door(i, hand->xC, hand->x10));
    }
}

/* A on an open door's body opens YouGame's multi-invite picker for this room; seats fill as friends
 * accept. A coin in the hand doesn't stop it (nothing takes a coin down there); a level slider does (A
 * lets go of it). Not in Doubles: there the partner tab's Invite does it. */
static void mu_f4_invite(int mine)
{
    struct CSSCursorData* hand = mnCharSel_804A0BC0[mine];
    const char* why = NULL;
    int i;
    if (mu_f4_doubles()) return;   /* Doubles: the partner tab's Invite and START (mu_f4_partner_start) */
    if (!hand || !(HSD_PadCopyStatus[mine].trigger & HSD_PAD_A)) return;
    if (hand->x5 == 1 && hand->x6 >= 4) why = "holding a level slider";
    else if (mu_f4.view.flags & MU_LOBBY_F_STARTED) why = "a match is being set up";
    else if (mu_f4.view.flags & MU_LOBBY_F_INVITING) why = "the invite picker is already open";
    for (i = 0; why == NULL && i < MU_LOBBY_SEATS; ++i) {
        unsigned char req = MU_LOBBY_OP_INVITE;
        if (i == mine || mu_f4.view.seats[i].kind != MU_LOBBY_KIND_OPEN || mu_f4_over_door(i, hand->xC, hand->x10) != 1)
            continue;
        if (mu_f4_send(&req, 1)) {
            mu_f4.invite_door = (unsigned char) (i + 1);
            OSReport("friends lobby: invite picker for door %d\n", i + 1);
            sfxForward();
        } else {
            lbAudioAx_80024030(3);
            mu_f4_note(MU_LOBBY_HINT_REFUSED);
        }
        return;
    }
}

/* While the invite picker is open, the one door that says "Inviting...": the door whose card opened it,
 * while it is still open, else the first open door (the picker opened another way, or that door filled).
 * The other open doors keep "Invite". -1: none. */
static int mu_f4_inviting_door(const MuLobbyView* v, int mine)
{
    int i, d = (int) mu_f4.invite_door - 1;
    if (!v || !(v->flags & MU_LOBBY_F_INVITING)) return -1;
    if (d >= 0 && d < 4 && d != mine && v->seats[d].kind == MU_LOBBY_KIND_OPEN) return d;
    for (i = 0; i < 4; ++i)
        if (i != mine && v->seats[i].kind == MU_LOBBY_KIND_OPEN) return i;
    return -1;
}

/* The host's Start froze a setup: once the links are up, this player locks in its pick (the selections
 * only say everyone is in; the frozen setup defines the match), and when everyone has, the splash. */
static void mu_f4_lock_in(int mine)
{
    MuMatchState m;
    MuSlippiSelections sel;
    const MuLobbySeat* me = &mu_f4.view.seats[mine];
    if (mu_f4.leaving) return;
    memset(&sel, 0, sizeof sel);
    sel.stage_opt = MU_SLP_STAGE_RANDOM;
    sel.online_mode = mu_slippi_state()->mode;
    if (!(mu_f4.view.flags & MU_LOBBY_F_STARTED)) {
        if (mu_f4.locked) {   /* the setup was called off: not locked in any more */
            mu_slippi_set_selections(&sel);
            mu_f4.locked = 0;
        }
        return;
    }
    if (mu_slippi_load_match_state(&m) != 0 || m.connection_state != MU_SLP_MM_CONNECTION_SUCCESS) return;
    if (!m.local_ready && !mu_f4.locked && (me->flags & MU_LOBBY_S_PICKED)) {
        /* Doubles: the colour this machine's door wears (the pair's; the guest's pick carries its last press). */
        sel.team_id = mu_f4_doubles() && mnCharSel_803F0DFC.doors[mine].team <= MU_LOBBY_TEAM_MAX
                          ? mnCharSel_803F0DFC.doors[mine].team : me->pick_team;
        sel.char_id = me->pick_ch;
        sel.char_color = me->pick_color;
        sel.char_opt = 1;
        mu_slippi_set_selections(&sel);
        mu_f4.locked = 1;
        OSReport("friends lobby: locked in for the started setup\n");
    }
    if (m.local_ready && m.remote_ready) {
        OSReport("friends lobby: everyone is in, to the splash\n");
        mu_f4.leaving = 1;
        mu_lobby_css_exit(MU_LOBBY_EXIT_SPLASH);
        mnCharSel_804D6CF6 = 1;
        mnCharSel_804D6CF2 = 0xFF;
        gm_801A4B60();
    }
}

/* The match is on its way: black over the select, drawn the way the rank badge draws its plain quads
 * (SIS sets the camera, vertex layout and blend first). See mu_lobby_curtain.
 * It is drawn with no area on every other frame of this select. A renderer skips a draw for a frame or two
 * while it builds a shader it has not seen, and nothing else uses this one, so the first real cover would
 * miss the select's first frame. */
static void mu_f4_curtain_draw(void* object)
{
    const float half = mu_lobby_curtain() ? 2000.0f : 0.0f;
    GXColor black = { 0, 0, 0, 255 };
    (void) object;
    GXSetTevColor(GX_TEVREG0, black);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-half, -half, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(half, -half, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(half, half, 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(-half, half, 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
}

/* Doubles: the partner door's portrait while its seat is empty (11-11 report: "I want the character portrait to
 * change to toggle between the different options"). Find and Invite have no fighter, so the door would show
 * Melee's closed shutter: this draws baked art in the portrait's own place instead (a "?" figure for Find,
 * two figures and a "+" for Invite; tools/bake_doubles_partner_art.py). P2 and CPU are Melee's own Player 2
 * and CPU doors. Drawn like the rank badge's quads: SIS's camera (world units), its vertex layout and blend.
 * The box is the portrait joint's (door->costume_joint: x 0..14.2, y -22.1..-3.1 from the joint in
 * MnSlChr) down to the top of the name plate (world y -18.3), the part of a portrait Melee shows above the
 * plate: square, as the art. SIS's canvas draws over the menu model, so the box stops at the plate's top
 * edge and the plate and the door's tab stay clear of it. */
#include <mu_doubles_partner_art.h>
#define MU_F4_ART_NONE 0
#define MU_F4_ART_FIND 1
#define MU_F4_ART_INVITE 2
#define MU_F4_PORTRAIT_W 14.2f
#define MU_F4_PORTRAIT_TOP (-3.1f)
#define MU_F4_PORTRAIT_BOTTOM (-22.1f)
#define MU_F4_PLATE_TOP (-18.26f)
static const char* const mu_f4_art_names[3] = { "closed", "find", "invite" };

static void mu_f4_art_draw(void* object)
{
    GXTexObj tex;
    GXColor white = { 255, 255, 255, 255 };
    const unsigned char* image = mu_f4.art == MU_F4_ART_FIND ? mu_dpa_find : mu_f4.art == MU_F4_ART_INVITE ? mu_dpa_invite : NULL;
    const float* b = mu_f4.art_box;
    (void) object;
    if (image == NULL || mu_lobby_curtain()) return;
    GXSetTevColor(GX_TEVREG0, white);
    GXInitTexObj(&tex, (void*) image, MU_DPA_W, MU_DPA_H, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(b[0], b[1], 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(b[2], b[1], 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(b[2], b[3], 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(b[0], b[3], 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
}

/* Which portrait the partner door shows this frame, and where: only the host's (seat 0) view of an empty
 * partner seat whose door is shut (no CPU or Player 2 on its way), on Find or Invite (or with an invite out). */
static void mu_f4_art_frame(int mine)
{
    const MuLobbyView* v = &mu_f4.view;
    const CSSDoor* door = &mnCharSel_803F0DFC.doors[1];
    unsigned char art = MU_F4_ART_NONE;
    if (mu_f4_doubles() && (v->flags & MU_LOBBY_F_KNOWN) && mine == 0 && v->seats[1].kind == MU_LOBBY_KIND_OPEN &&
        door->p_kind == Gm_PKind_NA) {
        const int want = mu_lobby_partner_want();
        if (v->flags & MU_LOBBY_F_INVITING) art = MU_F4_ART_INVITE;
        else if (want == MU_LOBBY_WANT_FIND) art = MU_F4_ART_FIND;
        else if (want == MU_LOBBY_WANT_INVITE) art = MU_F4_ART_INVITE;
    }
    if (art != MU_F4_ART_NONE) {
        HSD_JObj* portrait = NULL;
        Vec3 at;
        lb_80011E24(mnCharSel_804D6CC0, &portrait, door->costume_joint, -1);
        if (portrait == NULL) {
            art = MU_F4_ART_NONE;
        } else {
            lb_8000B1CC(portrait, NULL, &at);
            mu_f4.art_box[0] = at.x;
            mu_f4.art_box[1] = at.y + MU_F4_PORTRAIT_TOP;
            mu_f4.art_box[2] = at.x + MU_F4_PORTRAIT_W;
            mu_f4.art_box[3] = MU_F4_PLATE_TOP;
            if (mu_f4.art_box[3] > mu_f4.art_box[1] - 1.0f || mu_f4.art_box[3] < at.y + MU_F4_PORTRAIT_BOTTOM)
                mu_f4.art_box[3] = mu_f4.art_box[1] - MU_F4_PORTRAIT_W;   /* not where MnSlChr has it: a square */
        }
    }
    if (art != MU_F4_ART_NONE && !mu_f4.art_made) {
        /* Here, not in the build, as the curtain: the build runs before the screen's text camera exists. */
        HSD_Text* t = HSD_SisLib_803A6754(0, 0);
        HSD_SisLib_803A6B98(t, 0.0f, 0.0f, " ");
        t->render_callback = mu_f4_art_draw;
        mu_f4.art_made = 1;
    }
    mu_f4.art = art;
    if (mu_f4.art_logged != art + 1 && (art != MU_F4_ART_NONE || mu_f4.art_logged != 0)) {
        OSReport("doubles: partner portrait %s\n", mu_f4_art_names[art]);
        mu_f4.art_logged = (unsigned char) (art + 1);
    }
}

/* Melee's label font: letters, digits, spaces and periods; anything else becomes a space. */
static void mu_f4_line(int line, const char* s)
{
    char out[sizeof mu_f4.drawn[0]];
    size_t i;
    if (mu_f4.text == NULL) return;
    if (mu_lobby_curtain()) s = "";
    for (i = 0; i + 1 < sizeof out && s[i]; ++i) {
        const char c = s[i];
        out[i] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || (((mu_f4.view.mode & MU_LOBBY_M_SQUAD) || mu_f4.note >= MU_LOBBY_HINT_SQUAD_TWO) &&
                 (c == '\'' || c == '(' || c == ')' || c == '/')) ? c : ' ';
    }
    out[i] = 0;
    if (out[0] == 0) {
        out[0] = ' ';
        out[1] = 0;
    }
    if (strcmp(out, mu_f4.drawn[line]) == 0) return;
    strcpy(mu_f4.drawn[line], out);
    /* Squad Strike's hint line goes to the log as it changes (the agents' proof of each refusal). */
    if (line == 0 && out[1] && ((mu_f4.view.mode & MU_LOBBY_M_SQUAD) || (mu_f4.note_frames > 0 && mu_f4.note >= MU_LOBBY_HINT_SQUAD_TWO)))
        OSReport("friends lobby: squad line \"%s\"\n", out);
    HSD_SisLib_803A70A0(mu_f4.text, mu_f4.line[line], "%s", out);
}

/* Doubles: the right-hand panel, made as Slippi's online select makes its own (mu_slippi_css_text_init): one
 * text left-aligned at font 0.1, then each subtext at Slippi's place and size, in Slippi's order. Every
 * select gets it; only Doubles writes in it. */
static const GXColor mu_f4_white = { 0xFF, 0xFF, 0xFF, 0xFF };
static const GXColor mu_f4_gray = { 0x8E, 0x91, 0x96, 0xFF };
static const GXColor mu_f4_red = { 0xFF, 0x00, 0x00, 0xFF };
static const GXColor mu_f4_spin_done = { 0x33, 0xFF, 0x2F, 0xFF };
static const GXColor mu_f4_spin_wait = { 0x3C, 0xBC, 0xFF, 0xFF };
static const char mu_f4_spin_str[2][3] = { { (char) 0x81, 0x7B, 0 }, { (char) 0x81, 0x7E, 0 } };
static const char mu_f4_spin_done_str[3] = { (char) 0x81, 0x7C, 0 };

static void mu_f4_panel_sub(float scale, float x, float y)
{
    const int idx = HSD_SisLib_803A6B98(mu_f4.panel, x, y, "");
    HSD_SisLib_803A7548(mu_f4.panel, idx, scale, scale);
}
static void mu_f4_panel_make(void)
{
    static const float line_y[3] = { 52.0f, 75.0f, 98.0f };
    static const float err_y[4] = { 52.0f, 70.0f, 88.0f, 106.0f };
    HSD_Text* t = HSD_SisLib_803A6754(0, 0);
    int i;
    t->default_kerning = 1;
    t->default_alignment = 0;
    t->pos_z = 0.0f;
    t->font_size.x = 0.1f;
    t->font_size.y = 0.1f;
    mu_f4.panel = t;
    mu_f4_panel_sub(0.5f, 70.0f, 23.0f);              /* header */
    for (i = 0; i < 3; ++i) {
        mu_f4_panel_sub(0.4f, 90.0f, line_y[i]);      /* line */
        mu_f4_panel_sub(0.45f, 70.0f, line_y[i]);     /* its spinner */
    }
    mu_f4_panel_sub(0.4f, 70.0f, 132.5f);             /* gray rows */
    mu_f4_panel_sub(0.4f, 70.0f, 152.5f);
    mu_f4_panel_sub(0.5f, -130.0f, -256.0f);          /* the top bar's two (Slippi's "Playing:"), unused */
    mu_f4_panel_sub(0.5f, -50.0f, -256.0f);
    for (i = 0; i < 4; ++i) mu_f4_panel_sub(0.4f, 70.0f, err_y[i]);   /* red rows */
    memset(mu_f4.panel_drawn, 0, sizeof mu_f4.panel_drawn);
}

static void mu_f4_text_make(void)
{
    HSD_Text* text = HSD_SisLib_803A6754(0, 0);
    int i;
    text->default_kerning = 1;
    text->default_alignment = 1;
    text->font_size.x = 0.1f;
    text->font_size.y = 0.1f;
    /* The hint in the band under the doors (between the fighters and the doors it covered the doors' tags),
     * the rules in the clear strip above the title banner (on the top row they covered the fighters),
     * a label in each door's lower band, under the name plate and above the hint.
     * Higher (y 177) it sits on the fighter's name; below the KO counter (y 232) it is off the screen. */
    mu_f4.line[0] = HSD_SisLib_803A6B98(text, 0.0f, 218.0f, " ");
    HSD_SisLib_803A7548(text, mu_f4.line[0], 0.45f, 0.45f);
    mu_f4.line[1] = HSD_SisLib_803A6B98(text, 156.0f, -280.0f, " ");
    HSD_SisLib_803A7548(text, mu_f4.line[1], 0.32f, 0.32f);
    for (i = 0; i < 4; ++i) {
        const float x = mu_skin_pos_set[i] ? MU_TE_SHIELD_X(mu_skin_pos[i].x) : -240.0f + 160.0f * i;
        const float y = 196.0f;
        mu_f4.line[2 + i] = HSD_SisLib_803A6B98(text, x, y, " ");
        HSD_SisLib_803A7548(text, mu_f4.line[2 + i], 0.4f, 0.4f);
    }
    mu_f4_panel_make();
    mu_f4.text = text;
    memset(mu_f4.drawn, 0, sizeof mu_f4.drawn);
}

/* The Doubles partner door, as the log names it. */
#define MU_F4_P_FIND 0     /* empty: START finds a partner with the opponents */
#define MU_F4_P_INVITE 1   /* empty, this room's invite picker is open */
#define MU_F4_P_CPU 2
#define MU_F4_P_LOCAL 3    /* a 2nd controller on this machine */
#define MU_F4_P_FRIEND 4   /* a person online (for the guest: the host) */
static const char* const mu_f4_partner_names[5] = { "find", "invite", "cpu", "local", "friend" };
static int mu_f4_partner_state(const MuLobbyView* v, int mine)
{
    const MuLobbySeat* s = &v->seats[mine == 1 ? 0 : 1];
    if (s->kind == MU_LOBBY_KIND_CPU) return MU_F4_P_CPU;
    if (s->kind == MU_LOBBY_KIND_LOCAL) return MU_F4_P_LOCAL;
    if (s->kind == MU_LOBBY_KIND_HUMAN) return MU_F4_P_FRIEND;
    return (v->flags & MU_LOBBY_F_INVITING) ? MU_F4_P_INVITE : MU_F4_P_FIND;
}

/* Doubles: the word on the partner door's tag (where Melee says HMN, CPU or N/A) and the caption on the plate
 * under its portrait, in the host's (seat 0) view while the seat is not a friend's; NULL keeps Melee's own tag.
 * The caption is for an empty seat on Find or Invite (an invite out too); P2 and CPU have none. `want` is
 * mu_lobby_partner_want(). Drawn by mu_f4_partner_marks. */
static const char* mu_f4_partner_tag(const MuLobbyView* v, int mine, int want, const char** caption)
{
    const unsigned kind = v->seats[1].kind;
    *caption = NULL;
    if (!(v->mode & MU_LOBBY_M_DOUBLES) || !(v->flags & MU_LOBBY_F_KNOWN) || mine != 0 || kind == MU_LOBBY_KIND_HUMAN)
        return NULL;
    if (kind == MU_LOBBY_KIND_LOCAL) return "P2";
    /* A CPU seat the host has just tabbed away from (CPU, then Find) is still a CPU until the room catches up: the
     * tab's choice wins, so the gray N/A tab never reads CPU. mu_f4_partner_sync moves the choice to CPU whenever a
     * CPU does sit down, so the two only disagree after the host's own press. */
    if (kind == MU_LOBBY_KIND_CPU && want == MU_LOBBY_WANT_CPU) return "CPU";
    if ((v->flags & MU_LOBBY_F_INVITING) || want == MU_LOBBY_WANT_INVITE) {
        *caption = "Invite a friend";
        return "FRIEND";
    }
    if (want == MU_LOBBY_WANT_P2) return "P2";
    if (want == MU_LOBBY_WANT_CPU) return "CPU";
    *caption = "Find online";
    return "FIND";
}

/* What the Doubles panel says, before it is drawn (mu_f4_panel_text). */
typedef struct {
    char line[3][32];            /* the fighter, the search, the partner */
    unsigned char spin[3];       /* each line's spinner: 0 none, 1 waiting, 2 done */
    const char* gray;            /* the gray row: what B or the tab does now */
    char error[MU_LOBBY_NOTE];   /* a problem, in red over the lines (they are cleared, as Slippi's) */
    char gray_text[40];          /* the gray row with the player's own key (mu_f4_panel_keys) */
} MuF4Panel;

/* A name or note in the panel: the letters Melee's font draws (letters, digits, space . : - , '), else a space. */
static void mu_f4_panel_copy(char* out, size_t size, const char* s)
{
    size_t i;
    for (i = 0; i + 1 < size && s[i]; ++i) {
        const char c = s[i];
        out[i] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
                 c == ':' || c == '-' || c == ',' || c == '\'' ? c : ' ';
    }
    out[i] = 0;
}

/* The Doubles panel's words (DESIGN section 4's states), from the room's view, the hint this player was just
 * given (noted: a refusal or a press that did nothing, for MU_F4_NOTE frames) or the room's block_hint, the
 * partner tab, and why the last setup fell through (note, "" for none). Every line is at most 30 letters. The
 * rules refusals (fixed rules, unranked, host only) only buzz: the steady lines stay. */
static void mu_f4_panel_text(const MuLobbyView* v, unsigned hint, int noted, int want, const char* note, MuF4Panel* p)
{
    const MuLobbySeat* me;
    const MuLobbySeat* partner;
    int host, open;
    memset(p, 0, sizeof *p);
    p->gray = "";
    if (!v || !(v->flags & MU_LOBBY_F_KNOWN) || v->my_seat >= MU_LOBBY_DOUBLES_SEATS) {
        strcpy(p->line[0], "Waiting for players");   /* the room's state hasn't arrived yet */
        p->spin[0] = 1;
        return;
    }
    if (note && note[0]) {
        mu_f4_panel_copy(p->error, sizeof p->error, note);
        return;
    }
    switch (hint) {
    case MU_LOBBY_HINT_MISMATCH:
    case MU_LOBBY_HINT_TEAM_MISMATCH:
    case MU_LOBBY_HINT_UNSUPPORTED:
        strcpy(p->error, "Version mismatch");
        return;
    case MU_LOBBY_HINT_CLOSED:
        strcpy(p->error, "Lobby closed. Hold B to exit");
        return;
    case MU_LOBBY_HINT_TAKEN:
    case MU_LOBBY_HINT_CHANGED:
    case MU_LOBBY_HINT_REFUSED:
        if (!noted) break;
        strcpy(p->error, "Try again");
        return;
    default:
        break;
    }
    me = &v->seats[v->my_seat];
    partner = &v->seats[v->my_seat == 1 ? 0 : 1];
    host = (v->flags & MU_LOBBY_F_HOST) != 0;
    open = partner->kind == MU_LOBBY_KIND_OPEN;
    /* The fighter. */
    if (me->flags & MU_LOBBY_S_PICKED) {
        strcpy(p->line[0], "Character selected");
        p->spin[0] = 2;
    } else {
        strcpy(p->line[0], "Select your character");
        p->spin[0] = 1;
    }
    /* The search. */
    p->spin[1] = 1;
    if (v->mode & MU_LOBBY_M_SEARCHING) {
        strcpy(p->line[1], "Searching for opponents");
        p->gray = "Press B to cancel";
    } else if ((v->flags & MU_LOBBY_F_STARTED) || hint == MU_LOBBY_HINT_STARTING || hint == MU_LOBBY_HINT_IN_MATCH) {
        strcpy(p->line[1], "Match found");
        p->spin[1] = 2;
    } else if (noted && hint == MU_LOBBY_HINT_SEARCH_STOPPED) {
        strcpy(p->line[1], "Search stopped");
        p->spin[1] = 0;
    } else if (host && (v->flags & MU_LOBBY_F_INVITING)) {
        strcpy(p->line[1], "Waiting for players");
        p->gray = "Press B to cancel";
    } else if (hint == MU_LOBBY_HINT_START_SEARCH || hint == MU_LOBBY_HINT_START_PARTNER ||
               hint == MU_LOBBY_HINT_SEARCH_STOPPED || hint == MU_LOBBY_HINT_FIXED_RULES ||
               hint == MU_LOBBY_HINT_UNRANKED_ONLY || hint == MU_LOBBY_HINT_HOST_ONLY) {
        if (host && open && want == MU_LOBBY_WANT_INVITE) strcpy(p->line[1], "Press START to invite");
        else if (host && open && (want == MU_LOBBY_WANT_P2 || want == MU_LOBBY_WANT_CPU))
            strcpy(p->line[1], "Waiting for players");   /* Player 2 or the CPU is sitting down */
        else strcpy(p->line[1], "Press START to search");
    } else if (hint == MU_LOBBY_HINT_PICK && !(me->flags & MU_LOBBY_S_PICKED)) {
        p->spin[1] = 0;   /* the fighter's line says it */
    } else {
        strcpy(p->line[1], "Waiting for players");   /* the partner's pick, a game not ready, the host */
    }
    /* The partner. */
    p->spin[2] = 2;
    if (partner->kind == MU_LOBBY_KIND_HUMAN) {
        char name[MU_LOBBY_NAME];
        mu_f4_panel_copy(name, sizeof name, partner->name);
        if (name[0]) sprintf(p->line[2], "Partner: %s", name);   /* 9 + at most 15: within 30 */
        else strcpy(p->line[2], "Partner: a friend");
    } else if (partner->kind == MU_LOBBY_KIND_LOCAL) {
        strcpy(p->line[2], "Partner: Player 2");
    } else if (partner->kind == MU_LOBBY_KIND_CPU) {
        strcpy(p->line[2], "Partner: CPU");
    } else if (v->flags & MU_LOBBY_F_INVITING) {
        strcpy(p->line[2], "Inviting a friend");
        p->spin[2] = 1;
    } else if (host && want == MU_LOBBY_WANT_INVITE) {
        strcpy(p->line[2], "Partner: invite a friend");
        p->spin[2] = 0;
    } else if (host && want == MU_LOBBY_WANT_P2) {
        strcpy(p->line[2], "Partner: Player 2");
        p->spin[2] = 1;
    } else if (host && want == MU_LOBBY_WANT_CPU) {
        strcpy(p->line[2], "Partner: CPU");
        p->spin[2] = 1;
    } else {
        strcpy(p->line[2], "Partner: find online");
        p->spin[2] = 0;
    }
    /* The tab, while it turns (mu_friends4_partner_cycle's locks, roughly: not on a friend). */
    if (!p->gray[0] && host && partner->kind != MU_LOBBY_KIND_HUMAN && !(v->flags & MU_LOBBY_F_STARTED) &&
        !(v->mode & MU_LOBBY_M_SEARCHING))
        p->gray = "A on the door's tab changes it";
}

/* One subtext, written only when its words or colour change. */
static void mu_f4_panel_put(int sub, const char* s, const GXColor* color)
{
    char out[sizeof mu_f4.panel_drawn[0]];
    GXColor c = *color;
    if (mu_f4.panel == NULL) return;
    strncpy(out, s, sizeof out - 1);
    out[sizeof out - 1] = 0;
    if (strcmp(out, mu_f4.panel_drawn[sub]) == 0 &&
        (out[0] == 0 || memcmp(&mu_f4.panel_color[sub], &c, sizeof c) == 0))
        return;
    if (out[0] && sub != MU_F4_ST_LINE1 + 1 && sub != MU_F4_ST_LINE1 + 3 && sub != MU_F4_ST_LINE1 + 5 &&
        strcmp(out, mu_f4.panel_drawn[sub]) != 0)
        OSReport("doubles: panel %d \"%s\"\n", sub, out);   /* every row but the spinners, once per change */
    strcpy(mu_f4.panel_drawn[sub], out);
    mu_f4.panel_color[sub] = c;
    ++mu_f4.panel_draws;
    HSD_SisLib_803A74F0(mu_f4.panel, sub, &c);
    HSD_SisLib_803A70A0(mu_f4.panel, sub, "%s", out);
}

/* The Doubles panel, every frame (doubles: 0 blanks it, as behind the curtain): the partner door's state goes
 * to the log once per change; each subtext is written only when it changes, and the spinners turn every
 * MU_F4_SPIN_FRAMES frames as Slippi's do. */
/* "Press START", "Press B", "Hold B" with a keyboard player's own keys (Enter, X by default): the
 * panel's words are the GameCube names, which on a keyboard are other keys. A controller (no label
 * from the host) keeps them. Bounded to each buffer; a name that would not fit keeps the old word. */
/* The decomp's MSL string.h (on the game's include path) has no strstr: a plain first-match search. */
static char* mu_f4_find(char* text, const char* word)
{
    size_t i, n = strlen(word);
    if (!text || !n) return NULL;
    for (; *text; ++text) {
        for (i = 0; i < n && text[i] == word[i]; ++i) {}
        if (i == n) return text;
    }
    return NULL;
}
static void mu_f4_key_word(char* text, size_t size, const char* word, const char* key)
{
    char out[MU_LOBBY_NOTE];
    char* at = mu_f4_find(text, word);
    size_t head, word_n, key_n, tail;
    if (!at || !key || !key[0]) return;
    head = (size_t) (at - text);
    word_n = strlen(word);
    key_n = strlen(key);
    tail = strlen(at + word_n);
    if (head + key_n + tail + 1 > size || head + key_n + tail + 1 > sizeof out) return;
    memcpy(out, text, head);
    memcpy(out + head, key, key_n);
    memcpy(out + head + key_n, at + word_n, tail + 1);
    memcpy(text, out, head + key_n + tail + 1);
}
static void mu_f4_panel_keys(MuF4Panel* p, const char* b_key, const char* start_key)
{
    char press_b[24] = "", hold_b[24] = "", press_start[24] = "";
    size_t n;
    if (!p) return;
    /* Each replacement keeps its verb: "Press START" becomes "Press Enter", never just "Enter". */
    if (b_key && b_key[0] && strlen(b_key) < 16) {
        sprintf(press_b, "Press %s", b_key);
        sprintf(hold_b, "Hold %s", b_key);
    }
    if (start_key && start_key[0] && strlen(start_key) < 16) sprintf(press_start, "Press %s", start_key);
    mu_f4_key_word(p->line[1], sizeof p->line[1], "Press START", press_start);
    mu_f4_key_word(p->error, sizeof p->error, "Hold B", hold_b);
    if (p->gray && p->gray[0]) {
        n = strlen(p->gray);
        if (n < sizeof p->gray_text) {
            memcpy(p->gray_text, p->gray, n + 1);
            mu_f4_key_word(p->gray_text, sizeof p->gray_text, "Press B", press_b);
            p->gray = p->gray_text;
        }
    }
}
static void mu_f4_panel(const MuLobbyView* v, int doubles, unsigned hint, int noted, const char* note, int mine)
{
    MuF4Panel p;
    int i;
    if (doubles && v && (v->flags & MU_LOBBY_F_KNOWN) && v->my_seat < MU_LOBBY_DOUBLES_SEATS) {
        const int state = mu_f4_partner_state(v, mine);
        if (mu_f4.partner_logged != state + 1) {
            OSReport("doubles: partner door %s\n", mu_f4_partner_names[state]);
            mu_f4.partner_logged = (unsigned char) (state + 1);
        }
    }
    if (!doubles || mu_lobby_curtain()) {
        for (i = 0; i < MU_F4_PANEL_SUBS; ++i) mu_f4_panel_put(i, "", &mu_f4_white);
        return;
    }
    mu_f4_panel_text(v, hint, noted, mu_lobby_partner_want(), note, &p);
    {
        /* This machine's pad (the one the select reads): its keyboard keys, if a keyboard feeds it. */
        char b_key[16] = "", start_key[16] = "";
        const int port = gm_801677F0() & 3;
        mu_host_button_label(port, HSD_PAD_B, b_key, sizeof b_key);
        mu_host_button_label(port, HSD_PAD_START, start_key, sizeof start_key);
        mu_f4_panel_keys(&p, b_key, start_key);
    }
    mu_f4_panel_put(MU_F4_ST_HEADER, "Doubles", &mu_f4_white);
    for (i = 0; i < 3; ++i) {
        const int sub = MU_F4_ST_LINE1 + 2 * i;
        mu_f4_panel_put(sub, p.error[0] ? "" : p.line[i], &mu_f4_white);
        if (p.error[0] || p.spin[i] == 0) mu_f4_panel_put(sub + 1, "", &mu_f4_white);
        else if (p.spin[i] == 2) mu_f4_panel_put(sub + 1, mu_f4_spin_done_str, &mu_f4_spin_done);
        else mu_f4_panel_put(sub + 1, mu_f4_spin_str[(mu_f4.panel_frame / MU_F4_SPIN_FRAMES) & 1], &mu_f4_spin_wait);
    }
    mu_f4_panel_put(MU_F4_ST_GRAY, p.error[0] ? "" : p.gray, &mu_f4_gray);
    mu_f4_panel_put(MU_F4_ST_GRAY + 1, "", &mu_f4_gray);
    mu_f4_panel_put(MU_F4_ST_GRAY + 2, "", &mu_f4_gray);
    mu_f4_panel_put(MU_F4_ST_GRAY + 3, "", &mu_f4_white);
    {
        /* The error, wrapped at MU_F4_PANEL_WRAP letters on the last space, on up to four rows. */
        const char* at = p.error;
        for (i = 0; i < 4; ++i) {
            char row[MU_F4_PANEL_WRAP + 1];
            size_t n = strlen(at), cut = n;
            if (n > MU_F4_PANEL_WRAP) {
                for (cut = MU_F4_PANEL_WRAP; cut > 0 && at[cut] != ' '; --cut) {}
                if (cut == 0) cut = MU_F4_PANEL_WRAP;
            }
            memcpy(row, at, cut);
            row[cut] = 0;
            mu_f4_panel_put(MU_F4_ST_ERR1 + i, row, &mu_f4_red);
            at += cut;
            while (*at == ' ') ++at;
        }
    }
    if (++mu_f4.panel_frame >= 2 * MU_F4_SPIN_FRAMES) mu_f4.panel_frame = 0;
}

static void mu_f4_texts(int mine)
{
    const MuLobbyView* v = mu_f4.view_ok ? &mu_f4.view : NULL;
    char buf[96];
    unsigned hint = v ? v->block_hint : MU_LOBBY_HINT_OPENING;
    /* Doubles says everything in the panel; before the first view, the two-door select says so. */
    const int doubles = v ? (v->mode & MU_LOBBY_M_DOUBLES) != 0 : mu_f4.shut;
    int i, noted;
    if (mu_f4.text == NULL) mu_f4_text_make();
    /* A refusal Rooms sent this player shows for a moment (the first read only sets the count). */
    if (v) {
        if (mu_f4.refusal_ok && v->refusal_seq != mu_f4.refusal_seq && v->refusal_hint != MU_LOBBY_HINT_NONE)
            mu_f4_note(v->refusal_hint);
        mu_f4.refusal_seq = v->refusal_seq;
        mu_f4.refusal_ok = 1;
        /* Squad Strike: each person's pick count as the room has it, once per change. */
        for (i = 0; i < 4; ++i) {
            const unsigned n = v->seats[i].kind == MU_LOBBY_KIND_HUMAN && (v->mode & MU_LOBBY_M_SQUAD) ? v->seats[i].lineup_count : 0;
            if (n + 1 == mu_f4.lineup_logged[i]) continue;
            if (n || mu_f4.lineup_logged[i] > 1) OSReport("friends lobby: squad seat %d picks %u/4\n", i, n);
            mu_f4.lineup_logged[i] = (unsigned char) (n + 1);
        }
    }
    /* Why the last setup fell through (a connection dropped before the splash), for a few seconds: once per
     * drop (view.note_seq) and once per text, so two drops in a row with the same reason both show. */
    if (v && v->note[0] && (v->note_seq != mu_f4.note_seq_seen || strcmp(v->note, mu_f4.note_seen) != 0)) {
        memcpy(mu_f4.note_text, v->note, sizeof mu_f4.note_text);
        mu_f4.note_text[sizeof mu_f4.note_text - 1] = 0;
        mu_f4.note_text_frames = 2 * MU_F4_NOTE;
    }
    if (v && v->note[0]) {
        memcpy(mu_f4.note_seen, v->note, sizeof mu_f4.note_seen);
        mu_f4.note_seen[sizeof mu_f4.note_seen - 1] = 0;
        mu_f4.note_seq_seen = v->note_seq;
    } else if (v) {
        mu_f4.note_seen[0] = 0;
    }
    /* Doubles: a stopped search says so for a moment, then the panel offers START again. */
    if (doubles && v && v->block_hint == MU_LOBBY_HINT_SEARCH_STOPPED && !mu_f4.stopped_seen)
        mu_f4_note(MU_LOBBY_HINT_SEARCH_STOPPED);
    mu_f4.stopped_seen = v && v->block_hint == MU_LOBBY_HINT_SEARCH_STOPPED;
    noted = mu_f4.note_frames > 0;
    if (noted) {
        if (!mu_lobby_curtain()) --mu_f4.note_frames;   /* a note isn't spent where nobody can read it */
        hint = mu_f4.note;
    }
    if (doubles) {
        const char* note = "";
        mu_f4_line(0, "");
        if (mu_f4.note_text_frames > 0) {
            if (!mu_lobby_curtain()) --mu_f4.note_text_frames;
            note = mu_f4.note_text;   /* why the last setup fell through, 4 s */
        }
        mu_f4_panel(v, 1, hint, noted, note, mine);
    } else
#if MU_SQUAD
    if (gmSquad_LobbyChosen() && mine >= 0 && mine < 4 && squad_note[mine] && mu_f4.note_frames == 0) {
        mu_f4_line(0, "Squads too big to load together: pick a lighter fighter.");
    } else
#endif
    if (mu_f4.note_text_frames > 0) {
        if (!mu_lobby_curtain()) --mu_f4.note_text_frames;
        mu_f4_line(0, mu_f4.note_text);
    } else {
        /* The host knows START goes on; the line only says what is in the way. */
        if (v && (v->mode & MU_LOBBY_M_SQUAD) && hint == MU_LOBBY_HINT_PICK) {
            unsigned n = v->my_seat < 4 ? v->seats[v->my_seat].lineup_count : 0;
            if (n == 4) for (i = 0; i < 4; ++i)
                if (v->seats[i].kind == MU_LOBBY_KIND_HUMAN && v->seats[i].lineup_count < n) n = v->seats[i].lineup_count;
            sprintf(buf, "Pick 4 fighters first (%u/4).", n);
            mu_f4_line(0, buf);
        } else mu_f4_line(0, hint == MU_LOBBY_HINT_PRESS_START ? "" : mu_lobby_hint_text(hint));
    }
    if (!doubles) mu_f4_panel(v, 0, hint, noted, "", mine);
    if (doubles) {
        mu_f4_line(1, "");   /* Doubles' rules are fixed, so nothing to say here */
    } else if (mu_f4_host() && gmMainLib_GetGameRules()->mode >= 2) {
        mu_f4_line(1, "Coin and Bonus play as Stock");
    } else {
        mu_f4_line(1, "");   /* the select itself shows the rules */
    }
    for (i = 0; i < 4; ++i) {
        const MuLobbySeat* s = v ? &v->seats[i] : NULL;
        buf[0] = 0;
        /* Doubles draws no door labels: the doors and the panel say it, the link's route goes to the log. */
        if (s && i != mine && (v->flags & MU_LOBBY_F_KNOWN) && doubles && s->route != MU_LOBBY_ROUTE_NONE &&
            mu_f4.route_logged[i] != s->route + 1) {
            OSReport("doubles: seat %d link %s %ums\n", i, mu_lobby_route_text(s->route), (unsigned) s->rtt_ms);
            mu_f4.route_logged[i] = (unsigned char) (s->route + 1);
        }
        if (s && i != mine && (v->flags & MU_LOBBY_F_KNOWN) && !doubles) {
            if (s->kind == MU_LOBBY_KIND_OPEN)
                sprintf(buf, "%s", i == mu_f4_inviting_door(v, mine) ? "Inviting..." : "Invite");
            else if (s->kind == MU_LOBBY_KIND_CPU)
                sprintf(buf, "CPU Lv %u", (unsigned) s->level);
            else if (s->route != MU_LOBBY_ROUTE_NONE)   /* a person's link, only once it is up */
                sprintf(buf, "%s %ums", mu_lobby_route_text(s->route), (unsigned) s->rtt_ms);
        }
        mu_f4_line(2 + i, buf);
    }
}

/* Doubles: the partner door's tag and caption plate (mu_f4_partner_tag says the words). Both are SIS texts
 * on the canvas the art uses, which draws over the menu model, made here for the same reason as the curtain
 * (the build runs before the text camera exists) and placed from door 2's own name text, so they follow
 * the door wherever MnSlChr or a skin puts it. Offsets are world units from the middle of that name text,
 * read off the real 96 select at 2560x1920 (33.25 px a unit).
 * The tag: Melee's font, gold as its HMN, on a box filled like the tab's own dark inside (the text's
 * background quad), so Melee's word under it doesn't show; the font squeezes a long word to the box.
 * The plate: Melee's name plate has no part on a shut door, so it is drawn (a light edge, a dark inside)
 * by the caption's render callback, before its words, at the name plate's own size and place. */
#define MU_F4_TAG_LEFT (-6.42f)
#define MU_F4_TAG_RIGHT (-1.45f)
#define MU_F4_TAG_TOP 17.21f
#define MU_F4_TAG_BOTTOM 15.62f
#define MU_F4_TAG_FONT 0.043f        /* a 32-unit glyph cell is 1.38 units: the tab's word height */
#define MU_F4_CAPTION_HALF_W 5.40f
#define MU_F4_CAPTION_HALF_H 1.31f
#define MU_F4_CAPTION_EDGE 0.33f

static void mu_f4_caption_quad(const float* b, float in, GXColor c)
{
    GXSetTevColor(GX_TEVREG0, c);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(b[0] + in, b[1] - in, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(b[2] - in, b[1] - in, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(b[2] - in, b[3] + in, 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(b[0] + in, b[3] + in, 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
}

/* The plate, drawn as the curtain draws its quad; SIS then draws the caption's words over it. */
static void mu_f4_caption_draw(void* object)
{
    GXColor edge = { 153, 153, 153, 255 };
    GXColor inside = { 18, 18, 24, 255 };
    (void) object;
    if (mu_lobby_curtain()) return;
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    mu_f4_caption_quad(mu_f4.caption_box, 0.0f, edge);
    mu_f4_caption_quad(mu_f4.caption_box, MU_F4_CAPTION_EDGE, inside);
}

/* Each build frees the texts (mu_friends4_css_build, the Rules overlay): forget them. Otherwise hide them. */
static void mu_f4_marks_off(int freed)
{
    if (freed) {
        mu_f4.tag = mu_f4.caption = NULL;
        return;
    }
    if (mu_f4.tag != NULL) mu_f4.tag->hidden = 1;
    if (mu_f4.caption != NULL) mu_f4.caption->hidden = 1;
}

/* One line each time what a mark shows changes ("off": Melee's own tag, no plate). */
static void mu_f4_marks_log(const char* what, const char* words, char* logged, size_t size)
{
    const char* now = words != NULL ? words : "-";
    if (strcmp(now, logged) == 0 || (words == NULL && logged[0] == 0)) return;
    if (words != NULL) OSReport("doubles: partner %s \"%s\"\n", what, words);
    else OSReport("doubles: partner %s off\n", what);
    strncpy(logged, now, size - 1);
    logged[size - 1] = 0;
}

/* A mark's words, written only when they change, in its colour. */
static void mu_f4_marks_write(HSD_Text* t, char* drawn, size_t size, const char* words, GXColor colour)
{
    if (strcmp(drawn, words) == 0) return;
    HSD_SisLib_803A70A0(t, 0, "%s", words);
    HSD_SisLib_803A74F0(t, 0, &colour);
    strncpy(drawn, words, size - 1);
    drawn[size - 1] = 0;
}

static HSD_Text* mu_f4_marks_make(void)
{
    HSD_Text* t = HSD_SisLib_803A6754(0, 0);
    if (t == NULL) return NULL;
    t->x4C = 0;                 /* no depth test: over the menu model, as the art */
    t->default_fitting = 1;
    t->default_alignment = 1;
    t->default_kerning = 1;
    t->pos_z = 0.0f;
    t->bg_color.a = 0;
    return t;
}

static void mu_f4_partner_marks(int mine)
{
    static const GXColor gold = { 0xFF, 0xCB, 0x32, 0xFF };
    static const GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
    /* The tab's dark inside: N/A's gray, else the door's colour (Doubles' pair colour, MU_LOBBY_TEAM_MAX). */
    static const GXColor na = { 48, 48, 52, 255 };
    static const GXColor team[3] = { { 80, 26, 18, 255 }, { 26, 26, 84, 255 }, { 22, 66, 22, 255 } };
    const CSSDoor* door = &mnCharSel_803F0DFC.doors[1];
    const CSSTagData* name = mnCharSel_803F0E8C[1].data;
    const char* tag = NULL;
    const char* caption = NULL;
    float cx = 0.0f, cy = 0.0f;
    if (!mu_f4.view_ok && !mu_lobby_curtain()) return;   /* one failed read keeps the marks as they are, no flicker */
    if (mu_f4.view_ok && name != NULL && name->text != NULL && !mu_lobby_curtain()) {
        tag = mu_f4_partner_tag(&mu_f4.view, mine, mu_lobby_partner_want(), &caption);
        cx = name->text->pos_x + name->text->box_size_x * name->text->font_size.x / 2.0f;
        cy = -(name->text->pos_y + 16.0f * name->text->font_size.y);   /* a glyph cell is 32 units */
    }
    if (tag != NULL && door->p_kind == Gm_PKind_Cpu && strcmp(tag, "CPU") == 0) tag = NULL;   /* Melee's own says it */
    if (mu_f4.art == MU_F4_ART_NONE) caption = NULL;   /* only under the Find / Invite portrait */
    if (!mu_lobby_curtain()) {   /* behind it they are only hidden: no line each match */
        mu_f4_marks_log("tag", tag, mu_f4.tag_logged, sizeof mu_f4.tag_logged);
        mu_f4_marks_log("plate", caption, mu_f4.caption_logged, sizeof mu_f4.caption_logged);
    }

    if (tag != NULL && mu_f4.tag == NULL && (mu_f4.tag = mu_f4_marks_make()) != NULL) {
        HSD_Text* t = mu_f4.tag;
        t->font_size.x = t->font_size.y = MU_F4_TAG_FONT;
        t->box_size_x = (MU_F4_TAG_RIGHT - MU_F4_TAG_LEFT) / MU_F4_TAG_FONT;
        t->box_size_y = (MU_F4_TAG_TOP - MU_F4_TAG_BOTTOM) / MU_F4_TAG_FONT;
        HSD_SisLib_803A6B98(t, t->box_size_x / 2.0f, (t->box_size_y - 32.0f) / 2.0f, " ");
        mu_f4.tag_drawn[0] = 0;
    }
    if (mu_f4.tag != NULL && tag == NULL) {
        mu_f4.tag->hidden = 1;
    } else if (mu_f4.tag != NULL) {
        HSD_Text* t = mu_f4.tag;
        t->pos_x = cx + MU_F4_TAG_LEFT;
        t->pos_y = -(cy + MU_F4_TAG_TOP);
        t->bg_color = door->p_kind == Gm_PKind_NA || door->team >= 3 ? na : team[door->team];
        mu_f4_marks_write(t, mu_f4.tag_drawn, sizeof mu_f4.tag_drawn, tag, gold);
        t->hidden = 0;
    }

    if (caption != NULL && mu_f4.caption == NULL && (mu_f4.caption = mu_f4_marks_make()) != NULL) {
        HSD_Text* t = mu_f4.caption;
        t->font_size.x = name->text->font_size.x;   /* the name's own size and box (mncharsel's tag text) */
        t->font_size.y = name->text->font_size.y;
        t->box_size_x = name->text->box_size_x;
        t->box_size_y = name->text->box_size_y;
        t->render_callback = mu_f4_caption_draw;
        HSD_SisLib_803A6B98(t, t->box_size_x / 2.0f, 0.0f, " ");
        mu_f4.caption_drawn[0] = 0;
    }
    if (mu_f4.caption != NULL && caption == NULL) {
        mu_f4.caption->hidden = 1;
    } else if (mu_f4.caption != NULL) {
        HSD_Text* t = mu_f4.caption;
        t->pos_x = name->text->pos_x;
        t->pos_y = name->text->pos_y;
        mu_f4.caption_box[0] = cx - MU_F4_CAPTION_HALF_W;
        mu_f4.caption_box[1] = cy + MU_F4_CAPTION_HALF_H;
        mu_f4.caption_box[2] = cx + MU_F4_CAPTION_HALF_W;
        mu_f4.caption_box[3] = cy - MU_F4_CAPTION_HALF_H;
        mu_f4_marks_write(t, mu_f4.caption_drawn, sizeof mu_f4.caption_drawn, caption, white);
        t->hidden = 0;
    }
}

/* Apply room format and seat changes before sending any local selection. */
static void mu_f4_sync_mode(CSSData* css, int mine)
{
    /* The mode the log last named (0 none, 1 Free-for-all, 2 Team Battle, 3 Squad Strike). It outlives a visit,
     * so coming back from Rules or the stage select doesn't log the mode again: one line per real change. */
    static unsigned char mu_f4_mode_logged;
    if (mu_f4.view_ok && mu_f4.view.my_seat < 4) {
        const unsigned mode = (mu_f4.view.mode & MU_LOBBY_M_SQUAD) | (mu_f4.view.rules.teams ? 1 : 0);
        const int first = mu_f4.mode_seen == 0xFF;
        if (mu_f4.mode_seen != mode || mu_f4.seat_seen != mu_f4.view.my_seat) {
            {
                const unsigned char label = (mode & MU_LOBBY_M_SQUAD) ? 3 : (mode & 1) ? 2 : 1;
                if (label != mu_f4_mode_logged) {
                    OSReport("friends lobby: mode %s\n", label == 3 ? "Squad Strike" : label == 2 ? "Team Battle" : "Free-for-all");
                    mu_f4_mode_logged = label;
                }
            }
            mu_f4.rules_sync = !first || (mode & MU_LOBBY_M_SQUAD);
            mu_f4.rules_drawn_ok = 0;
            if (!first) mu_f4.mode_pending = 0;
#if MU_SQUAD
            gmSquad_LobbyChoose((mode & MU_LOBBY_M_SQUAD) != 0);
            if (gmSquad_LobbyChosen() || (!first && (mu_f4.mode_seen & MU_LOBBY_M_SQUAD))) {
                const MuLobbySeat* me = &mu_f4.view.seats[mine];
                int j;
                gmSquad_ClearAll();
                mu_f4.sent[0] = 0xFF;
                mu_f4.lineup_sent[0] = 0xFF;
                if (gmSquad_LobbyChosen()) {
                    for (j = 0; j < me->lineup_count && j < GMSQUAD_EXTRA; ++j) {
                        gmSquad_rows[mine][j].ckind = me->lineup[j][0];
                        gmSquad_rows[mine][j].color = mu_squad_color(me->lineup[j][0], me->lineup[j][1]);
                    }
                }
                if (gmSquad_LobbyChosen() && me->lineup_count == 4) {
                    MuLobbySeat last = *me;
                    last.flags |= MU_LOBBY_S_PICKED;
                    last.pick_ch = me->lineup[3][0]; last.pick_color = me->lineup[3][1]; last.pick_team = 0;
                    mu_f4_draw(css, mine, &last); /* silent restoration, as ordinary Friends picks */
                } else {
                    mu_f4_draw(css, mine, me);
                    squad_regrab(mine, mine);
                }
                gmSquad_coin_pos[mine] = me->lineup_count == 4 ? GMSQUAD_EXTRA : GMSQUAD_NONE;
                squad_coin_holder[mine] = squad_coin_held(mine) ? mine + 1 : 0;
            }
#endif
            mu_f4.mode_seen = (unsigned char) mode;
            mu_f4.seat_seen = mu_f4.view.my_seat;
        }
        if (mu_f4.mode_pending && mu_f4.refusal_ok && mu_f4.view.refusal_seq != mu_f4.refusal_seq)
            mu_f4.mode_pending = 0;
    }
}

/* Once a frame (mu_css_state_frame, after the doubles frame), and at the end of OnEnter. */
static void mu_friends4_css_frame(void)
{
    CSSData* css = mnCharSel_804D6CB0;
    const MuLobbyView* v;
    int mine;
    if (css == NULL) return;
    if (!mu_lobby_css()) {
        mu_f4_restore();
        return;
    }
    if (mnCharSel_804D6CF6 >= 3) {
        /* The Rules overlay or name entry: they free every text; the next build makes them anew. */
        mu_f4.text = mu_f4.panel = NULL;
        mu_f4_marks_off(1);
        mu_f4_shut_forget();
        return;
    }
    v = mu_lobby_read();
    mu_f4.view_ok = v != NULL;
    if (v) mu_f4.view = *v;
    mu_lobby_curtain_frame(v);
    if (!mu_f4.curtain_made) {
        /* Here, not in the build: the build runs before the screen's text camera exists. Moved to the end of the
         * last list that camera draws, so it covers the coins, hands and every text. */
        HSD_Text* curtain = HSD_SisLib_803A6754(0, 0);
        HSD_SisLib_803A6B98(curtain, 0.0f, 0.0f, " ");
        curtain->render_callback = mu_f4_curtain_draw;
        HSD_GObjGXLink_80390908(curtain->entity, 4, 0xFF);
        mu_f4.curtain_made = 1;
    }
    /* Only a seated guest with a pick follows: someone who sat down during the stage select stays here.
     * A match that has started is never followed back there, even if the frame closing the stage select was lost. */
    if (!mu_f4.leaving && mu_f4.view_ok && (mu_f4.view.mode & MU_LOBBY_M_STAGE_SELECT) &&
        !(mu_f4.view.flags & (MU_LOBBY_F_HOST | MU_LOBBY_F_STARTED)) && mu_f4.view.my_seat < MU_LOBBY_SEATS &&
        (mu_f4.view.seats[mu_f4.view.my_seat].flags & MU_LOBBY_S_PICKED)) {
        OSReport("friends lobby: the host went to the stage select, following\n");
        mu_f4.leaving = 1;
        mu_lobby_watch_stage();
        mnCharSel_804D6CF6 = 1;
        mnCharSel_804D6CF2 = 0xFF;
        gm_801A4B60();
    }
    mine = mu_f4_mine();
    mu_f4_log_a(mine);
    mu_f4_sync_mode(css, mine);
    mu_f4_bounds(mine);
    if (mu_f4.view_ok && (mu_f4.view.flags & MU_LOBBY_F_KNOWN) && mu_f4.view.my_seat < MU_LOBBY_SEATS) {
        mu_f4_pair_team(css, mine);    /* first: the pick and hover below carry the pair's colour */
        mu_f4_local(css, mine);
        if (mu_lobby_local_owned(&mu_f4.view)) mu_f4_local(css, 1);
        else mu_f4.partner_sent[0] = 0xFF;
        mu_f4_doors(css, mine);        /* the view first: the host's doors then see what the room has */
        mu_f4_partner_sync(css, mine);
        mu_f4_pair_paint(css, mine);
        mu_f4_host_doors(css, mine);
        mu_f4_rules(css);
        mu_f4.rules_sync = 0;
#if MU_SQUAD
        squad_lobby_heading();
#endif
        mu_f4_invite(mine);
        mu_f4_lock_in(mine);
        mu_f4_art_frame(mine);
    } else {
        mu_f4.art = MU_F4_ART_NONE;
        CSSDoor* door = &mnCharSel_803F0DFC.doors[mine];
        if (door->p_kind != Gm_PKind_Human) {
            door->p_kind = door->p_kind_prev = Gm_PKind_Human;
            css->vs.start.players[mine].slot_type = Gm_PKind_Human;
            if (mu_friends_css_frames >= 2) mnCharSel_8025DB34((u8) mine);
        }
    }
    mu_f4_own_drawn(mine);
    if (mu_friends_css_frames >= 1) mu_f4_texts(mine);
    if (mu_friends_css_frames >= 1) mu_f4_partner_marks(mine);   /* after the portrait (mu_f4.art) */
    {
        int i;
        for (i = 0; i < 4; ++i) mu_f4.kind_seen[i] = mnCharSel_803F0DFC.doors[i].p_kind;
    }
    /* Doubles: two doors, where Melee puts them on the far left, from the first frame on (OnEnter's call,
     * before anything is drawn), after this frame's redraws; a read that failed leaves them as they are. */
    if (mu_f4.view_ok ? mu_f4_doubles() : mu_f4.shut) mu_f4_shut();
    else mu_f4_unshut();
}
