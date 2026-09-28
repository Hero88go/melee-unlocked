/* Bounded, opt-in M7 diagnostic. No RNG state or simulation control is changed. */
#include "mu_shim.h"

char* getenv(const char* name);
int snprintf(char* dst, __SIZE_TYPE__ size, const char* format, ...);

static unsigned float_bits(float value)
{
    unsigned bits;
    __builtin_memcpy(&bits, &value, sizeof bits);
    return bits;
}

static unsigned raw_be32(const void* address)
{
    const unsigned char* p = (const unsigned char*) address;
    return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16) |
           ((unsigned) p[2] << 8) | (unsigned) p[3];
}

static int in_range(unsigned* retrace);

static const void* g_root_anim_jobj;
static int g_root_anim_id;
static unsigned g_root_anim_frame;

void mu_trace_native_root_anim_begin(const void* jobj, int motion,
                                    int anim_id, float anim_frame)
{
    unsigned retrace;
    g_root_anim_jobj = NULL;
    if (motion != 0x32 || !jobj || !in_range(&retrace)) return;
    g_root_anim_jobj = jobj;
    g_root_anim_id = anim_id;
    g_root_anim_frame = float_bits(anim_frame);
}

void mu_trace_native_root_anim_channel(const void* jobj, int type,
                                      float sample, float prior_translation)
{
    unsigned retrace;
    if (jobj != g_root_anim_jobj || type != 7 || !in_range(&retrace)) return;
    char line[208];
    snprintf(line, sizeof line,
             "[root-anim-channel-native] retrace=%u motion=50 anim=%d frame=%08X type=%d sample=%08X prior_z=%08X",
             retrace, g_root_anim_id, g_root_anim_frame, type,
             float_bits(sample), float_bits(prior_translation));
    mu_host->log(line);
}

void mu_trace_native_root_fobj(const void* jobj, int type, unsigned flags,
                               unsigned op, unsigned op_intrp,
                               unsigned frac_value, unsigned frac_slope,
                               int startframe, unsigned fterm, float time,
                               float p0, float p1, float d0, float d1,
                               float sample)
{
    unsigned retrace;
    if (jobj != g_root_anim_jobj || type != 7 || !in_range(&retrace)) return;
    char line[320];
    snprintf(line, sizeof line,
             "[root-fobj-native] retrace=%u motion=50 anim=%d frame=%08X type=%d flags=%02X state=%u op=%u intrp=%u frac=%u/%u start=%d term=%u time=%08X p0=%08X p1=%08X d0=%08X d1=%08X sample=%08X",
             retrace, g_root_anim_id, g_root_anim_frame, type, flags,
             flags & 0x0F, op, op_intrp, frac_value, frac_slope,
             startframe, fterm, float_bits(time), float_bits(p0),
             float_bits(p1), float_bits(d0), float_bits(d1),
             float_bits(sample));
    mu_host->log(line);
}

void mu_trace_native_root_anim_end(void)
{
    g_root_anim_jobj = NULL;
}

static unsigned parse_decimal(const char** cursor)
{
    unsigned value = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10 + (unsigned) (*(*cursor)++ - '0');
    }
    return value;
}

static int in_range(unsigned* retrace)
{
    static int initialized, enabled;
    static unsigned first, last;
    if (!initialized) {
        const char* text = getenv("MELEE_TRACE_NATIVE_RNG");
        initialized = 1;
        if (text) {
            first = parse_decimal(&text);
            if (*text++ == ':') {
                last = parse_decimal(&text);
                enabled = !*text && first <= last && last - first <= 120;
            }
        }
    }
    if (!enabled || !mu_host || !mu_host->vi_retrace_count || !mu_host->log) return 0;
    *retrace = mu_host->vi_retrace_count();
    return *retrace >= first && *retrace <= last;
}

void mu_trace_native_rng(unsigned before, unsigned after, void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[rng-native] retrace=%u caller=%p before=%08X after=%08X",
             retrace, caller, before, after);
    mu_host->log(line);
}

void mu_trace_native_effect(const char* phase, void* generator)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line, "[effect-native] retrace=%u phase=%s generator=%p",
             retrace, phase, generator);
    mu_host->log(line);
}

void mu_trace_native_generator(const char* phase, int link, int bank, int index, int limit)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[180];
    snprintf(line, sizeof line,
             "[generator-native] retrace=%u phase=%s link=%d bank=%d index=%d limit=%d",
             retrace, phase, link, bank, index, limit);
    mu_host->log(line);
}

void mu_trace_native_itemdrop(const void* item_gobj, const void* position,
                             const void* velocity, int check,
                             const void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[192];
    snprintf(line, sizeof line,
             "[itemdrop-native] retrace=%u gobj=%p pos=%p vel=%p check=%d caller=%p",
             retrace, item_gobj, position, velocity, check, caller);
    mu_host->log(line);
}

void mu_trace_native_item_spawn(const void* parent, const void* spawned,
                                int kind, const void* position,
                                const void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[176];
    snprintf(line, sizeof line,
             "[item-spawn-native] retrace=%u parent=%p spawned=%p kind=%d pos=%p caller=%p",
             retrace, parent, spawned, kind, position, caller);
    mu_host->log(line);
}

void mu_trace_native_item_spawn_attempt(int kind, int position_valid,
                                        float x, float y, float z,
                                        const void* spawned)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[208];
    snprintf(line, sizeof line,
             "[item-spawn-attempt-native] retrace=%u kind=%d valid=%d pos=%08X,%08X,%08X gobj=%p",
             retrace, kind, position_valid, float_bits(x), float_bits(y),
             float_bits(z), spawned);
    mu_host->log(line);
}

void mu_trace_native_item_effect(const void* item_gobj, int effect_id,
                                int joint, const void* origin,
                                const void* spread, int mode, float scale,
                                const void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[item-effect-native] retrace=%u gobj=%p effect=%X joint=%d origin=%p spread=%p mode=%d scale=%08X caller=%p",
             retrace, item_gobj, effect_id, joint, origin, spread, mode,
             float_bits(scale), caller);
    mu_host->log(line);
}

void mu_trace_native_map_effect(const void* item_gobj, int collision_flags,
                                int item_kind, int effect_suppressed,
                                const void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[176];
    snprintf(line, sizeof line,
             "[map-effect-native] retrace=%u gobj=%p flags=%X item=%d suppressed=%d caller=%p",
             retrace, item_gobj, collision_flags, item_kind,
             effect_suppressed, caller);
    mu_host->log(line);
}

void mu_trace_native_map_collision(const void* item_gobj, int collision_flags,
                                   int item_kind, float x, float y, float z,
                                   const void* caller)
{
    unsigned retrace;
    if (item_kind != 12 || !in_range(&retrace)) return;
    char line[208];
    snprintf(line, sizeof line,
             "[item-map-collision-native] retrace=%u gobj=%p flags=%X kind=%d pos=%08X,%08X,%08X caller=%p",
             retrace, item_gobj, collision_flags, item_kind,
             float_bits(x), float_bits(y), float_bits(z), caller);
    mu_host->log(line);
}

void mu_trace_native_item_process_kind12(const char* phase,
                                         const void* item_gobj, int kind,
                                         int collision_result, float x,
                                         float y, float z, float vx, float vy,
                                         float vz)
{
    unsigned retrace;
    if (kind != 12 || !in_range(&retrace)) return;
    char line[240];
    snprintf(line, sizeof line,
             "[item-sword-process-native] retrace=%u phase=%s gobj=%p result=%d pos=%08X,%08X,%08X vel=%08X,%08X,%08X",
             retrace, phase, item_gobj, collision_result, float_bits(x),
             float_bits(y), float_bits(z), float_bits(vx), float_bits(vy),
             float_bits(vz));
    mu_host->log(line);
}

void mu_trace_native_food_physics(const char* phase, const void* item_gobj,
                                  float gravity, float speed_max,
                                  float y, float velocity_y)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[192];
    snprintf(line, sizeof line,
             "[item-food-physics-native] retrace=%u phase=%s gobj=%p gravity=%08X max=%08X y=%08X vy=%08X",
             retrace, phase, item_gobj, float_bits(gravity),
             float_bits(speed_max), float_bits(y), float_bits(velocity_y));
    mu_host->log(line);
}

void mu_trace_native_sword_spawned(const char* phase, const void* gobj,
                                   const void* item, const void* article,
                                   const void* attributes, int state,
                                   float launch_speed, float velocity_y)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[512];
    snprintf(line, sizeof line,
             "[item-sword-spawn-native] retrace=%u phase=%s gobj=%p item=%p article=%p attrs=%p state=%d launch=%08X vy=%08X article-slots=%08X/%08X/%08X/%08X/%08X/%08X attrs-words=%08X/%08X/%08X/%08X/%08X/%08X/%08X",
             retrace, phase, gobj, item, article, attributes, state,
             float_bits(launch_speed), float_bits(velocity_y),
             raw_be32(article), raw_be32((const char*) article + 4),
             raw_be32((const char*) article + 8),
             raw_be32((const char*) article + 12),
             raw_be32((const char*) article + 16),
             raw_be32((const char*) article + 20), raw_be32(attributes),
             raw_be32((const char*) attributes + 4),
             raw_be32((const char*) attributes + 8),
             raw_be32((const char*) attributes + 12),
             raw_be32((const char*) attributes + 16),
             raw_be32((const char*) attributes + 20),
             raw_be32((const char*) attributes + 24));
    mu_host->log(line);
}

void mu_trace_native_item_motion(const char* phase, const void* gobj,
                                 int kind, int state, int has_physics,
                                 int physics_enabled, float y,
                                 float velocity_y)
{
    unsigned retrace;
    if (kind != 12 || !in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[item-motion-native] retrace=%u phase=%s gobj=%p kind=%d state=%d has-physics=%d enabled=%d y=%08X vy=%08X",
             retrace, phase, gobj, kind, state, has_physics, physics_enabled,
             float_bits(y), float_bits(velocity_y));
    mu_host->log(line);
}

void mu_trace_native_onett(unsigned gobj_id, unsigned flag, int car, unsigned state_a,
                           int wait_a, int sub_a, float speed_a, int next,
                           unsigned state_b, int wait_b, int sub_b, float speed_b,
                           float car_x, float next_x)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[320];
    snprintf(line, sizeof line,
             "[onett-native] retrace=%u gobj=%u flag=%u car=%d a=%u wait=%d sub=%d speed=%g x=%g next=%d b=%u wait_b=%d sub_b=%d speed_b=%g next_x=%g",
             retrace, gobj_id, flag, car, state_a, wait_a, sub_a, speed_a, car_x,
             next, state_b, wait_b, sub_b, speed_b, next_x);
    mu_host->log(line);
}

void mu_trace_native_magnify(int slot, int counter, float percent,
                             float camera_scale, int offscreen, int more_flags)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[320];
    snprintf(line, sizeof line,
             "[magnify-fighter-native] retrace=%u slot=%d counter=%d percent=%g camera=%g offscreen=%d more=%d",
             retrace, slot, counter, percent, camera_scale, offscreen, more_flags);
    mu_host->log(line);
}

void mu_trace_native_magnify_render(int phase, int slot0_offscreen,
                                   int slot1_offscreen)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[magnify-render-native] retrace=%u phase=%d slot0=%d slot1=%d",
             retrace, phase, slot0_offscreen, slot1_offscreen);
    mu_host->log(line);
}

void mu_trace_native_magnify_cobj(int phase, int result, int slot)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[144];
    snprintf(line, sizeof line,
             "[magnify-cobj-native] retrace=%u phase=%d result=%d slot=%d",
             retrace, phase, result, slot);
    mu_host->log(line);
}

void mu_trace_native_magnify_gate(int slot, int ignore_offscreen,
                                 int fighter_present, int camera_ok,
                                 int visible_ok)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[magnify-gate-native] retrace=%u slot=%d ignore=%d present=%d camera=%d visible=%d",
             retrace, slot, ignore_offscreen, fighter_present, camera_ok,
             visible_ok);
    mu_host->log(line);
}

void mu_trace_native_camera_gate(void* fighter_gobj, void* camera_cobj,
                                 void* current_cobj, int path,
                                 int offscreen_flag, int result, int screen_x,
                                 int screen_y, float bone_x, float bone_y,
                                 float bone_z)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[camera-visibility-native] retrace=%u gobj=%p camera=%p current=%p path=%d offscreen=%d result=%d screen=%d,%d bone=%g,%g,%g",
             retrace, fighter_gobj, camera_cobj, current_cobj, path,
             offscreen_flag, result, screen_x, screen_y, bone_x, bone_y,
             bone_z);
    mu_host->log(line);
}

void mu_trace_native_world_to_screen(void* cobj, const float* world,
                                     const float* eye, const float* up,
                                     const float* target, const float* view_z,
                                     const float* screen, int d)
{
    unsigned retrace;
    if (!in_range(&retrace) || !world || !screen ||
        world[0] < 89.0f || world[0] > 97.0f ||
        world[1] < 37.0f || world[1] > 43.0f) return;
    char line[320];
    snprintf(line, sizeof line,
             "[world-to-screen-native] retrace=%u cobj=%p d=%d world=%.6g,%.6g,%.6g eye=%s%.6g,%.6g,%.6g up=%s%.6g,%.6g,%.6g target=%s%.6g,%.6g,%.6g viewz=%s%.7g,%.7g,%.7g,%.7g screen=%.6g,%.6g,%.6g",
             retrace, cobj, d, world[0], world[1], world[2],
             eye ? "" : "?", eye ? eye[0] : 0.0f, eye ? eye[1] : 0.0f, eye ? eye[2] : 0.0f,
             up ? "" : "?", up ? up[0] : 0.0f, up ? up[1] : 0.0f, up ? up[2] : 0.0f,
             target ? "" : "?", target ? target[0] : 0.0f, target ? target[1] : 0.0f, target ? target[2] : 0.0f,
             view_z ? "" : "?", view_z ? view_z[0] : 0.0f, view_z ? view_z[1] : 0.0f,
             view_z ? view_z[2] : 0.0f, view_z ? view_z[3] : 0.0f,
             screen[0], screen[1], screen[2]);
    mu_host->log(line);
}

void mu_trace_native_camera_transform(void* camera, int mode, int slot,
                                      float pitch, float yaw, float offset_x,
                                      float offset_y, float quake_x,
                                      float quake_y, float quake_scale,
                                      const float* state)
{
    unsigned retrace;
    if (!in_range(&retrace) || !state) return;
    char line[384];
    snprintf(line, sizeof line,
             "[camera-transform-native] retrace=%u camera=%p mode=%d slot=%d pitch=%.7g yaw=%.7g translation=%.7g,%.7g quake=%.7g,%.7g*%.7g interest=%.7g,%.7g,%.7g target_interest=%.7g,%.7g,%.7g position=%.7g,%.7g,%.7g target_position=%.7g,%.7g,%.7g fov=%.7g/%.7g",
             retrace, camera, mode, slot, pitch, yaw, offset_x, offset_y,
             quake_x, quake_y, quake_scale,
             state[0], state[1], state[2], state[3], state[4], state[5],
             state[6], state[7], state[8], state[9], state[10], state[11],
             state[12], state[13]);
    mu_host->log(line);
}

void mu_trace_native_camera_quake(const char* phase, const float* offset,
                                  const float* translation, float scale,
                                  float camera_speed, float bounds_z,
                                  float fov, int game_mode, int is_1p,
                                  const float* quake_coeffs,
                                  float one_player_scale, float stage_zoom,
                                  float stage_max_depth)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[320];
    snprintf(line, sizeof line,
             "[camera-quake-native] retrace=%u phase=%s offset=%s%.7g,%.7g translation=%.7g,%.7g scale=%.7g speed=%.7g bounds_z=%.7g fov=%.7g game_mode=%d one_player=%d quake_coeffs=%.7g,%.7g,%.7g,%.7g one_player_scale=%.7g stage_zoom=%.7g stage_max=%.7g",
             retrace, phase ? phase : "?", offset ? "" : "?",
             offset ? offset[0] : 0.0f, offset ? offset[1] : 0.0f,
             translation ? translation[0] : 0.0f,
             translation ? translation[1] : 0.0f, scale, camera_speed,
             bounds_z, fov, game_mode, is_1p,
             quake_coeffs ? quake_coeffs[0] : 0.0f,
             quake_coeffs ? quake_coeffs[1] : 0.0f,
             quake_coeffs ? quake_coeffs[2] : 0.0f,
             quake_coeffs ? quake_coeffs[3] : 0.0f, one_player_scale,
             stage_zoom, stage_max_depth);
    mu_host->log(line);
}

void mu_trace_native_magnify_bounds(int slot, float x, float left, float right)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    unsigned x_bits = float_bits(x);
    unsigned left_bits = float_bits(left);
    unsigned right_bits = float_bits(right);
    char line[192];
    snprintf(line, sizeof line,
             "[magnify-bounds-native] retrace=%u slot=%d x=%g/%08X left=%g/%08X right=%g/%08X",
             retrace, slot, x, x_bits, left, left_bits, right, right_bits);
    mu_host->log(line);
}

void mu_trace_native_item_timer(int countdown, unsigned table_weight, unsigned table_size)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[item-timer-native] retrace=%u countdown=%d weight=%u size=%u",
             retrace, countdown, table_weight, table_size);
    mu_host->log(line);
}

void mu_trace_native_item_pick(const void* table, unsigned table_weight,
                               unsigned table_size, void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[192];
    snprintf(line, sizeof line,
             "[item-pick-native] retrace=%u table=%p weight=%u size=%u caller=%p",
             retrace, table, table_weight, table_size, caller);
    mu_host->log(line);
}

void mu_trace_native_item_init(int mode, int stage, int low, int high,
                               float random, float scale, int countdown)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[item-init-native] retrace=%u mode=%d stage=%d low=%d high=%d random=%g scale=%g countdown=%d",
             retrace, mode, stage, low, high, random, scale, countdown);
    mu_host->log(line);
}

void mu_trace_native_sound(int sfx_id, void* caller)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[fighter-sfx-native] retrace=%u id=%d caller=%p",
             retrace, sfx_id, caller);
    mu_host->log(line);
}

void mu_trace_native_ax_sound(int sfx_id)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[128];
    snprintf(line, sizeof line,
             "[ax-sfx-native] retrace=%u id=%d channel=6",
             retrace, sfx_id);
    mu_host->log(line);
}

void mu_trace_native_audio_start(int sfx_id, int volume, int pan, int track,
                                 int channel)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[160];
    snprintf(line, sizeof line,
             "[audio-start-native] retrace=%u id=%d vol=%d pan=%d track=%d channel=%d",
             retrace, sfx_id, volume, pan, track, channel);
    mu_host->log(line);
}

int mu_trace_native_ax_enabled(void)
{
    unsigned retrace;
    return in_range(&retrace);
}

int mu_experiment_defer_native_sound(int sound_id)
{
    static int initialized, enabled;
    if (!initialized) {
        const char* text = getenv("MELEE_TEST_DEFER_NATIVE_AX_RETRACE");
        enabled = text && text[0] == '1' && text[1] == '\0';
        initialized = 1;
    }
    return enabled &&
        (sound_id == 0 || sound_id == 455 || sound_id == 166 ||
         sound_id == 258 || sound_id == 390005);
}

unsigned mu_native_retrace_count(void)
{
    return mu_host && mu_host->vi_retrace_count
        ? mu_host->vi_retrace_count() : 0;
}

/* Diagnostic (M9 screen-shake evidence), off unless MELEE_TRACE_QUAKE is set: every retrace a quake
 * offset reaches Camera_ApplyQuake, what was requested and what the camera was moved by. */
void mu_trace_native_quake_applied(float requested_x, float requested_y, float applied_x,
                                   float applied_y, int no_shake)
{
    static int initialized, enabled;
    char line[160];
    if (!initialized) {
        initialized = 1;
        enabled = getenv("MELEE_TRACE_QUAKE") != 0;
    }
    if (!enabled || (requested_x == 0.0f && requested_y == 0.0f) || !mu_host || !mu_host->log)
        return;
    snprintf(line, sizeof line, "[quake-native] retrace=%u requested=%.7g,%.7g applied=%.7g,%.7g no_shake=%d",
             mu_native_retrace_count(), requested_x, requested_y, applied_x, applied_y, no_shake);
    mu_host->log(line);
}

/* Diagnostic, off unless MELEE_TRACE_EFFECTS is set: every effect spawn with the match frame, to diff
 * against the recompiled build's hooks on the same two functions (main.cpp trace_effect_spawn). */
unsigned int gm_GetFrameCount(void);
void mu_trace_native_effect_spawn(int async, int gfx_id)
{
    static int initialized, enabled;
    char line[96];
    if (!initialized) {
        initialized = 1;
        enabled = getenv("MELEE_TRACE_EFFECTS") != 0;
    }
    if (!enabled || !mu_host || !mu_host->log)
        return;
    snprintf(line, sizeof line, "[effect-spawn] match=%u kind=%s id=%d", gm_GetFrameCount(),
             async ? "async" : "sync", gfx_id);
    mu_host->log(line);
}

void mu_trace_native_quake_request(int kind, int have_model_set)
{
    char line[96];
    if (!getenv("MELEE_TRACE_QUAKE") || !mu_host || !mu_host->log)
        return;
    snprintf(line, sizeof line, "[quake-request-native] retrace=%u kind=%d model_set=%d",
             mu_native_retrace_count(), kind, have_model_set);
    mu_host->log(line);
}

void mu_trace_native_ax_tick(int driver_tick)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[128];
    snprintf(line, sizeof line, "[ax-driver-clock-native] retrace=%u tick=%d",
             retrace, driver_tick);
    mu_host->log(line);
}

void mu_trace_native_ax_queue(int driver_tick, int sound_id, int voice_id,
                              int frame, unsigned flags, const void* command,
                              unsigned depth)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[ax-driver-queue-native] retrace=%u tick=%d depth=%u id=%d vid=%d frame=%d flags=%08X cmd=%p",
             retrace, driver_tick, depth, sound_id, voice_id, frame, flags,
             command);
    mu_host->log(line);
}

void mu_trace_native_sound_command(const void* command, int behavior,
                                   unsigned sfx_id)
{
    unsigned retrace;
    if (!in_range(&retrace) || !command) return;
    const unsigned char* bytes = (const unsigned char*) command;
    char line[192];
    snprintf(line, sizeof line,
             "[fighter-sfx-cmd-native] retrace=%u command=%p behavior=%d sfx=%08X bytes=%02X%02X%02X%02X",
             retrace, command, behavior, sfx_id,
             bytes[0], bytes[1], bytes[2], bytes[3]);
    mu_host->log(line);
}

void mu_trace_native_synth_start(int id, int vol, int vol2, int pan, int priority,
                                  int itd, int group, float pitch1, float pitch2,
                                  float main, float auxa, float auxb)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[256];
    snprintf(line, sizeof line,
             "[synth-start-native] retrace=%u id=%d vol=%d vol2=%d pan=%d priority=%d itd=%d group=%d pitch=%08X/%08X mix=%08X/%08X/%08X",
             retrace, id, vol, vol2, pan, priority, itd, group,
             float_bits(pitch1), float_bits(pitch2), float_bits(main),
             float_bits(auxa), float_bits(auxb));
    mu_host->log(line);
}

void mu_trace_native_synth_voice(int id, int voice_index, int voice_count,
                                 int sample_rate,
                                 const unsigned short* words)
{
    unsigned retrace;
    size_t pos;
    int i;
    char line[320];
    if ((id != 0 && id != 239 && id != 111 && id != 176 && id != 1389) || !words ||
        !in_range(&retrace)) return;
    pos = (size_t) snprintf(line, sizeof line,
                            "[synth-voice-native] retrace=%u id=%d voice=%d/%d rate=%d words=",
                            retrace, id, voice_index, voice_count, sample_rate);
    for (i = 0; i < 31 && pos + 4 < sizeof line; ++i) {
        int written = snprintf(line + pos, sizeof line - pos, "%04X",
                               words[i]);
        if (written <= 0) break;
        pos += (size_t) written;
    }
    mu_host->log(line);
}

void mu_trace_native_ground_transform_input(int special, float offset_z,
                                            float facing,
                                            float ground_velocity)
{
    unsigned retrace;
    if (!in_range(&retrace)) return;
    char line[192];
    snprintf(line, sizeof line,
             "[ground-phys-native] retrace=%u phase=transform-input special=%d offset_z=%08X facing=%08X gr=%08X",
             retrace, special, float_bits(offset_z), float_bits(facing),
             float_bits(ground_velocity));
    mu_host->log(line);
}

void mu_trace_native_root_delta(const char* phase, int motion, int anim_id,
                                float anim_frame, float current_z,
                                float previous_z, float delta_z)
{
    unsigned retrace;
    if (motion != 0x32 || !in_range(&retrace)) return;
    char line[240];
    snprintf(line, sizeof line,
             "[root-delta-native] retrace=%u phase=%s motion=%d anim=%d frame=%08X current=%08X previous=%08X delta=%08X",
             retrace, phase, motion, anim_id, float_bits(anim_frame),
             float_bits(current_z), float_bits(previous_z),
             float_bits(delta_z));
    mu_host->log(line);
}

void mu_trace_native_root_translation(const char* phase, int motion,
                                      int anim_id, float anim_frame,
                                      int use_attr_scale, float value_z,
                                      float scale)
{
    unsigned retrace;
    if (motion != 0x32 || !in_range(&retrace)) return;
    char line[224];
    snprintf(line, sizeof line,
             "[root-translation-native] retrace=%u phase=%s motion=%d anim=%d frame=%08X attr_scale=%d z=%08X scale=%08X",
             retrace, phase, motion, anim_id, float_bits(anim_frame),
             use_attr_scale, float_bits(value_z), float_bits(scale));
    mu_host->log(line);
}

void mu_trace_native_ground_physics(const char* phase, int motion,
                                    float friction, float facing,
                                    float ground_velocity, float accel1,
                                    float accel2, float velocity_x,
                                    float self_accel_x, float normal_x,
                                    float normal_y)
{
    unsigned retrace;
    if (motion != 0x32 || !in_range(&retrace)) return;
    char line[320];
    snprintf(line, sizeof line,
             "[ground-phys-native] retrace=%u phase=%s motion=%d friction=%.9g facing=%.9g gr=%08X e4=%08X e8=%08X vx=%08X ax=%08X normal=%08X/%08X",
             retrace, phase, motion, friction, facing,
             float_bits(ground_velocity), float_bits(accel1),
             float_bits(accel2), float_bits(velocity_x),
             float_bits(self_accel_x), float_bits(normal_x),
             float_bits(normal_y));
    mu_host->log(line);
}

/* Diagnostic (M4 per-voice audio), off unless MELEE_TRACE_AX_VOICES is set: every sound request,
 * disc-stream start and first stream data for the whole run, to place each voice's cause on the AX
 * frame timeline next to the host's per-voice trace. The host adds the timebase to "[ax-" lines. */
static int ax_voice_trace_enabled(void)
{
    static int initialized, enabled;
    if (!initialized) {
        initialized = 1;
        enabled = getenv("MELEE_TRACE_AX_VOICES") != 0;
    }
    return enabled && mu_host && mu_host->log;
}

void mu_trace_native_ax_request(int sfx_id, int volume, int pan, int track, int channel)
{
    char line[160];
    if (!ax_voice_trace_enabled())
        return;
    snprintf(line, sizeof line,
             "[ax-request-native] retrace=%u id=%d vol=%d pan=%d track=%d channel=%d",
             mu_native_retrace_count(), sfx_id, volume, pan, track, channel);
    mu_host->log(line);
}

void mu_trace_native_ax_stream(const char* phase, int entrynum, unsigned aram_base)
{
    char line[128];
    if (!ax_voice_trace_enabled())
        return;
    snprintf(line, sizeof line, "[ax-stream-native] retrace=%u phase=%s entry=%d base=%08X",
             mu_native_retrace_count(), phase, entrynum, aram_base);
    mu_host->log(line);
}

void mu_trace_native_ax_bank(int entrynum, int bank_id, unsigned aram_base, unsigned size)
{
    char line[128];
    if (!ax_voice_trace_enabled())
        return;
    snprintf(line, sizeof line, "[ax-bank-native] retrace=%u entry=%d bank=%d base=%08X size=%08X",
             mu_native_retrace_count(), entrynum, bank_id, aram_base, size);
    mu_host->log(line);
}
