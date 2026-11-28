/* Shared native predictor, included by mu_online.c and the window-free rollback harness.
 * Slippi policy stays on its original branches; console compares every controller field. */
/* Section 10. *predicted_players gets a bit per remote index whose pad for this frame was kept as a
 * prediction (REG_PREDICTED_PLAYERS, for section 11). */
static int load_opponent_inputs(PADStatus* stat, int frame, int predicting_allowed, unsigned int* predicted_players)
{
    int count, remote_port = 0, have_all = 1;
    u8* rxb = mu_online.rxb;
    *predicted_players = 0;
    for (count = 0; count < online_remotes(); count++, remote_port++) {
        int latest;
        const u8* slots = RX_INPUTS(count);
        const u8* src = slots;   /* entry k is the pad of frame (latest - k) */
        if (remote_port == mu_online.local_index) {
            remote_port++;
        }
        latest = (int) be32(RX_FRAME_NUM(count));
        if (latest - frame >= 0) {
            src = slots + (latest - frame) * PAD_SIZE;
        } else if (predicting_allowed && (mu_online.console_active || frame >= START_SYNC_FRAME - mu_online.delay) &&
                   !mu_online.game_over)
        {
            /* Predicting (Slippi 3.7.0): the host's pad for this frame among the frames after the
             * latest one (the latest input, or a newer input suggestion from that player), the
             * latest input past those, or an early pad the other player sent for a frame up to this
             * one. The pad kept for the comparison is the one the game gets, so a wrong prediction
             * always rolls back. The copy also clears the kept pad's PAD_RNG_ROLLBACK_MARKER: that
             * byte of a remote pad is always zero. */
            unsigned int ahead = (unsigned int) (frame - latest - 1);
            u8 w = mu_online.predicted_write[count];
            const u8* early = early_pad(count, frame, latest);
            have_all = 0;
            /* Console rollback fills no host predictions: it keeps predicting the latest pad. */
            if (!mu_online.console_active && ahead < RXB_INPUTS_COUNT) {
                src = RX_PREDICTED(count) + ahead * PAD_SIZE;
            }
            if (early != NULL) {
                src = early;
                mu_online.early_used++;
            }
            memcpy(mu_online.predicted[count][w], src, PAD_SIZE);
            mu_online.predicted_early[count][w] = (u8) (early != NULL);
            *predicted_players |= 1u << count;
            mu_online.predicted_write[count] = (u8) ((w + 1) % PREDICTED_INPUTS_COUNT);
            if (!mu_online.player_predicting[count]) {
                mu_online.player_savestate_frame[count] = frame;
                mu_online.player_predicting[count] = 1;
                mu_online.predicted_read[count] = w;
                if (!mu_online.savestate_predicting) {
                    mu_online.savestate_frame = frame;
                    mu_online.savestate_predicting = 1;
                }
            }
        }
        if (remote_port < online_ports()) {
            wire_to_pad(&stat[remote_port], src);
            /* A slot with no player has no pad to load, and on the oldest frame of the longest
             * rollback the index above runs past its inputs. Section 11 reads every slot's RNG
             * bits, so these stay zero. Console rollback has no RNG bits: its analog A is real. */
            if (!mu_online.console_active && count >= rxb[RXB_OPNT_COUNT]) {
                stat[remote_port].analogA = 0;
            }
        }
    }
    if (have_all) {
        mu_online.finalized = frame;
    }
    return have_all;
}

/* TriggerSendInput section 11 (Slippi 3.7.0): this frame's RNG bits, and the pad bits taken out of
 * the inputs. A player could hold an input until a seed they like comes up to choose the result of
 * a random special (turnip, hammer, misfire). So on the frame a player newly presses B with the
 * stick toward one of those, the bits that came with the other players' pads of that frame (every
 * player's, when several do it on the same frame) go into the top byte of that frame's seed
 * (sync_rng_proc). Runs for every frame the game is given, re-simulated ones included, and keeps
 * the result by frame. predicted_players: the remote indices whose pad here is a kept prediction. */
static void prepare_rng_bits(PADStatus* stat, int frame, unsigned int predicted_players)
{
    unsigned int prev_b_held =
        mu_online.b_held_history[(unsigned int) (frame - 1) & (RNG_HISTORY_COUNT - 1)];
    unsigned int b_held = 0, rolling = 0, ports_used, seed_bits = 0;
    unsigned long long pad_bits = 0;   /* 8 bits per port, port 1 at the bottom */
    int port, count;

    for (port = 0; port < online_ports(); port++) {
        PADStatus* pad = &stat[port];
        int x;
        pad_bits |= (unsigned long long) pad->analogA << (8 * port);
        pad->analogA = 0;
        if (!(pad->button & 0x200)) {
            continue;
        }
        b_held |= 1u << port;
        if (prev_b_held & (1u << port)) {
            continue;   /* not a fresh B press */
        }
        /* Whether this character rolls something with B and the stick selects that special. The
         * game takes side-B at |x| >= 0.6 and down-B at y <= -0.55 after dividing by 80. These
         * are raw values and its clamp can only shrink them, so a press that passes might not
         * roll but one that fails never does. */
        switch ((int) Player_GetPlayerCharacter(port)) {   /* PlayerBlock_LoadExternalCharID */
        case CKind_Peach:
            if (pad->stickY <= DOWN_B_MAX_STICK_Y) {
                rolling |= 1u << port;
            }
            break;
        case CKind_GameWatch:
        case CKind_Luigi:
            x = pad->stickX < 0 ? -pad->stickX : pad->stickX;
            if (x >= SIDE_B_MIN_STICK_X) {
                rolling |= 1u << port;
            }
            break;
        default:
            break;
        }
    }
    mu_online.b_held_history[(unsigned int) frame & (RNG_HISTORY_COUNT - 1)] = (u8) b_held;

    if (rolling != 0) {
        /* Everyone's bits but the rolling player's, or everyone's when several roll. With one
         * player rolling, that player's opponent always has the bits this needs even when
         * predicting a press, so a correctly predicted press needs no rollback. */
        ports_used = (1u << online_ports()) - 1;
        if (((rolling - 1) & rolling) == 0) {
            ports_used &= ~rolling;
        }
        for (port = 0; port < online_ports(); port++) {
            if (ports_used & (1u << port)) {
                seed_bits ^= (unsigned int) (pad_bits >> (8 * port)) & 0xFF;
            }
        }
        mu_online.rng_bit_frames++;
        /* A predicted player whose bits were just used had no real bits for this frame, and
         * section 9 only compares inputs: mark the kept prediction so that it forces a rollback.
         * The write index is one past the prediction kept for this frame. */
        for (count = 0; count < online_remotes(); count++) {
            int p = count < mu_online.local_index ? count : count + 1;
            int idx;
            if (!(predicted_players & (1u << count)) || !(ports_used & (1u << p))) {
                continue;
            }
            idx = mu_online.predicted_write[count] - 1;
            if (idx < 0) {
                idx += PREDICTED_INPUTS_COUNT;
            }
            mu_online.predicted[count][idx][PAD_RNG_ROLLBACK_MARKER] = 1;
        }
    }
    /* For sync_rng_proc when the engine processes this frame. */
    mu_online.rng_seed_bits_history[(unsigned int) frame & (RNG_HISTORY_COUNT - 1)] = (u8) seed_bits;
}

static int used_slot(int frame)
{
    int slot = frame % USED_PADS_COUNT;
    return slot < 0 ? slot + USED_PADS_COUNT : slot;
}

static void record_used_pads(const PADStatus* stat, int frame, int confirmed)
{
    int slot = used_slot(frame), p;
    mu_online.used_frame[slot] = frame;
    mu_online.used_confirmed[slot] = (u8) confirmed;
    for (p = 0; p < online_ports(); p++) {
        pad_to_wire(mu_online.used_pads[slot][p], &stat[p]);
    }
}

/* Whether two pads differ the way the game would act on (the comparison Slippi's prediction check
 * makes: the buttons it reads, both sticks, the triggers outside their dead zone). */
static int pads_differ(const u8* a, const u8* b)
{
    int t;
    if (mu_online.console_active) return memcmp(a, b, 11) != 0;
    if ((a[0] & 0x1F) != (b[0] & 0x1F) || (a[1] & 0x7F) != (b[1] & 0x7F) || memcmp(a + 2, b + 2, 4) != 0) {
        return 1;
    }
    for (t = 6; t < 8; t++) {
        if (a[t] <= 42 && b[t] <= 42) {
            continue;
        }
        if (a[t] != b[t]) {
            return 1;
        }
    }
    return 0;
}

/* Early inputs: a frame already run on a guess whose early pad, arrived since, says otherwise. The
 * game then rolls back now instead of when the confirmed pad comes, so the correction shows a few
 * milliseconds sooner and goes back fewer frames. The confirmed pad still decides: if it differs
 * from the early one, that is one more ordinary rollback. Returns 1 when such a frame exists. */
/* MELEE_EARLY_TRACE=1 (tests): every wrong early guess is logged with both pads. */
static int early_trace_on(void)
{
    static int value = -1;
    if (value < 0) {
        const char* v = getenv("MELEE_EARLY_TRACE");
        value = v != NULL && v[0] == '1';
    }
    return value;
}

/* On by default (owner 11-22): above about 41 ms one way, an early pad arrives after its frame was
 * already run on a guess, so prediction alone never uses it; this is what makes it count there. It
 * only rolls back when the early pad contradicts the guess at the game's action lines.
 * MELEE_EARLY_ROLLBACK=0 turns it off (tests). */
static int early_rollback_on(void)
{
    static int value = -1;
    if (value < 0) {
        const char* v = getenv("MELEE_EARLY_ROLLBACK");
        value = !(v != NULL && v[0] == '0');
    }
    return value;
}

/* Zone-level difference between two pads: a button edge, a stick crossing one of the game's
 * action lines, a shield drop, or a trigger crossing the light-shield line. Mirrors the host's
 * early-input judge (early_input_rules.h, all edges). */
static int side_of(int v, int line)
{
    return line < 0 ? v <= line : v >= line;
}
static int axis_crossed(int a, int b, const signed char* lines, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (side_of(a, lines[i]) != side_of(b, lines[i])) {
            return 1;
        }
    }
    return 0;
}
static int pads_contradict_zone(const u8* a, const u8* b)
{
    static const signed char main_x[4] = {-64, -23, 23, 64}, main_y[4] = {-56, -23, 23, 53};
    static const signed char c_x[4] = {-64, -23, 23, 64}, c_y[4] = {-53, -23, 23, 53};
    const unsigned int ba = ((unsigned int) a[0] << 8) | a[1], bb = ((unsigned int) b[0] << 8) | b[1];
    const int shielding_a = (ba & 0x0070) || a[6] >= 43 || a[7] >= 43;
    const int shielding_b = (bb & 0x0070) || b[6] >= 43 || b[7] >= 43;
    if ((ba ^ bb) & (0x1300 | 0x0C70)) {
        return 1;
    }
    if (axis_crossed((s8) a[2], (s8) b[2], main_x, 4) || axis_crossed((s8) a[3], (s8) b[3], main_y, 4) ||
        axis_crossed((s8) a[4], (s8) b[4], c_x, 4) || axis_crossed((s8) a[5], (s8) b[5], c_y, 4)) {
        return 1;
    }
    if ((shielding_a || shielding_b) && side_of((s8) a[3], -53) != side_of((s8) b[3], -53)) {
        return 1;
    }
    if ((a[6] >= 43) != (b[6] >= 43) || (a[7] >= 43) != (b[7] >= 43)) {
        return 1;
    }
    return 0;
}
/* MELEE_EARLY_RB_ANY=1: the old byte compare (any difference starts the early rollback). */
static int early_rb_any(void)
{
    static int value = -1;
    if (value < 0) {
        const char* v = getenv("MELEE_EARLY_RB_ANY");
        value = v != NULL && v[0] == '1';
    }
    return value;
}

static int early_contradicts(int count)
{
    int latest = (int) be32(RX_FRAME_NUM(count));
    int f = mu_online.player_savestate_frame[count];
    u8 r = mu_online.predicted_read[count];
    if (early_trace_on()) {
        const u8* any = early_pad(count, 0x7FFFFFFF, latest);
        if (any != NULL) {
            logf_("early check: remote %d latest %d savestate %d read %d write %d game frame %d",
                  count, latest, f, (int) r, (int) mu_online.predicted_write[count], (int) mu_online.frame);
        }
    }
    while (r != mu_online.predicted_write[count]) {
        const u8* early = early_pad(count, f, latest);
        if (early != NULL && early_trace_on()) {
            logf_("early check: remote %d frame %d early %02x%02x %d,%d guess %02x%02x %d,%d zone %d",
                  count, f, early[0], early[1], (s8) early[2], (s8) early[3],
                  mu_online.predicted[count][r][0], mu_online.predicted[count][r][1],
                  (s8) mu_online.predicted[count][r][2], (s8) mu_online.predicted[count][r][3],
                  pads_contradict_zone(early, mu_online.predicted[count][r]));
        }
        if (early != NULL && (early_rb_any() ? pads_differ(early, mu_online.predicted[count][r])
                                               : pads_contradict_zone(early, mu_online.predicted[count][r]))) {
            return 1;
        }
        r = (u8) ((r + 1) % PREDICTED_INPUTS_COUNT);
        f++;
    }
    return 0;
}

/* Section 9: compare earlier predictions with inputs that have now arrived. Returns 1 when a
 * rollback was triggered. */
static int check_predictions(int frame)
{
    u8* rxb = mu_online.rxb;
    int rollback_required = mu_online.rollback_active;
    int count, savestate, values_set = 0;

    if (!mu_online.savestate_predicting) {
        memset(mu_online.player_predicting, 0, sizeof mu_online.player_predicting);
        return 0;
    }
    for (count = 0; count < online_remotes(); count++) {
        while (mu_online.player_predicting[count]) {
            int latest = (int) be32(RX_FRAME_NUM(count));
            int ss = mu_online.player_savestate_frame[count];
            int offset = latest - ss;
            const u8* actual;
            const u8* predicted;
            int t, differ = 0;
            if (offset < 0) {
                break;   /* the inputs for the savestate frame have not arrived yet */
            }
            if (!mu_online.console_active && ss <= mu_online.stable_finalized) {
                if (latest > ss) {
                    goto inputs_match;   /* already finalized: advance without comparing */
                }
                break;
            }
            actual = RX_INPUTS(count) + offset * PAD_SIZE;
            predicted = mu_online.predicted[count][mu_online.predicted_read[count]];
            /* Slippi 3.7.0: section 11 marks a prediction when that frame's RNG used this player's
             * bits before they arrived. The frame is redone with them, even if the inputs match. */
            if (!mu_online.console_active && predicted[PAD_RNG_ROLLBACK_MARKER] != 0) {
                mu_online.rng_rollbacks++;
                rollback_required = 1;
                break;
            }
            if ((actual[0] & 0x1F) != (predicted[0] & 0x1F) ||
                (actual[1] & 0x7F) != (predicted[1] & 0x7F) ||
                memcmp(actual + 2, predicted + 2, 4) != 0)
            {
                differ = 1;
            }
            for (t = 6; t < 8 && !differ; t++) {
                if (actual[t] <= 42 && predicted[t] <= 42) {
                    continue;   /* both in the trigger dead zone */
                }
                if (actual[t] != predicted[t]) {
                    differ = 1;
                }
            }
            if (mu_online.console_active) differ = pads_differ(actual, predicted);
            mu_online.predictions++;
            if (mu_online.predicted_early[count][mu_online.predicted_read[count]]) {
                mu_online.early_scored[count]++;
                if (differ) {
                    mu_online.early_missed[count]++;
                }
                /* Honest play: at most 3.3% wrong in every run with 32 or more scored guesses (32 runs,
                 * 10-23); a peer sending made-up pads is 100% wrong. 24 guesses, over half wrong. */
                if (!mu_online.early_distrusted[count] && mu_online.early_scored[count] >= 24 &&
                    mu_online.early_missed[count] * 2 > mu_online.early_scored[count]) {
                    mu_online.early_distrusted[count] = 1;
                    logf_("online: remote %d early pads ignored for this match (%u of %u early guesses wrong)",
                          count, mu_online.early_missed[count], mu_online.early_scored[count]);
                }
                if (differ) {
                    mu_online.early_wrong++;
                    if (early_trace_on() && mu_online.early_wrong <= 300) {
                        logf_("early wrong: remote %d frame %d latest %d game frame %d guess %02x%02x %d,%d %d,%d %d %d actual %02x%02x %d,%d %d,%d %d %d",
                              count, ss, latest, frame,
                              predicted[0], predicted[1], (s8) predicted[2], (s8) predicted[3], (s8) predicted[4], (s8) predicted[5], predicted[6], predicted[7],
                              actual[0], actual[1], (s8) actual[2], (s8) actual[3], (s8) actual[4], (s8) actual[5], actual[6], actual[7]);
                    }
                } else {
                    mu_online.early_right++;
                }
            }
            if (differ) {
                mu_online.mispredictions++;
                rollback_required = 1;
                break;
            }
        inputs_match:
            mu_online.player_savestate_frame[count] = ss + 1;
            mu_online.predicted_read[count] =
                (u8) ((mu_online.predicted_read[count] + 1) % PREDICTED_INPUTS_COUNT);
            if (mu_online.predicted_read[count] == mu_online.predicted_write[count]) {
                break;   /* caught up with the prediction */
            }
        }
    }
    if (!mu_online.console_active && !rollback_required && early_rollback_on() && mu_online.rxb_size > RXB_SIZE) {
        for (count = 0; count < online_remotes(); count++) {
            if (mu_online.player_predicting[count] == 1 && early_contradicts(count)) {
                rollback_required = 1;
                mu_online.early_rollbacks++;
                break;
            }
        }
    }
    /* The savestate frame is the lowest frame any remote is still predicted from. */
    savestate = mu_online.stable_finalized;
    for (count = 0; count < online_remotes(); count++) {
        if (mu_online.player_predicting[count] != 1) {
            continue;
        }
        if (!values_set || mu_online.player_savestate_frame[count] < savestate) {
            savestate = mu_online.player_savestate_frame[count];
        }
        values_set = 1;
    }
    mu_online.savestate_frame = savestate;
    mu_online.finalized = mu_online.console_active ? savestate - 1 : savestate;
    if (savestate > (int) be32(rxb + RXB_SMALLEST_LATEST)) {
        mu_online.finalized = (int) be32(rxb + RXB_SMALLEST_LATEST);
    }
    if (rollback_required) {
        {
            unsigned long long mu_ticks_since_start(void);
            if (!mu_online.console_active) mu_online.rollback_start_ticks = mu_ticks_since_start();
        }
        if (!mu_online.console_active && trace_renew()) {
            unsigned long long mu_ticks_since_start(void);
            logf_("rollback trigger frame %d retrace %d tbms %d", frame,
                  (int) mu_online_abi_retrace_count(), (int) (mu_ticks_since_start() / 40500));
        }
        mu_online.selftest_rollback = 0;
        mu_online.rollback_active = 1;
        mu_online.rollback_should_load = 1;
        mu_online.rollback_end_frame = frame;
        mu_online.frame = frame + 1;
        mu_online.rollbacks++;
        if ((unsigned int) (frame - savestate) > mu_online.max_depth) {
            mu_online.max_depth = (unsigned int) (frame - savestate);
        }
        return 1;
    }
    for (count = 0; count < online_remotes(); count++) {
        if (mu_online.player_predicting[count] == 1 &&
            mu_online.predicted_read[count] == mu_online.predicted_write[count])
        {
            mu_online.player_predicting[count] = 0;
        }
    }
    for (count = 0; count < online_remotes(); count++) {
        if (mu_online.player_predicting[count] == 1) {
            return 0;
        }
    }
    mu_online.savestate_predicting = 0;
    return 0;
}

