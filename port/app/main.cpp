// Native Melee port entry point.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <functional>
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "host.h"
#include "gecko_data.h"
#include "slippi_playback.h"
#include "render_observer.h"
#include "exi_slippi.h"
#include "slippi_online.h"
#include "slippi_net.h"
#include "jukebox.h"
#include "audio.h"
#include "guest_registry.h"
#include "gx_backend.h"
#include "gx_core.h"
#include "render_options.h"
#include "pc_settings.h"
#include "texture_pack.h"
#include "cosmetic_mods.h"
#include "disc_skin_scan.h"
#include "hackpack_ai.h"
#include "mod_scan.h"
#include "threaded_backend.h"
#include "window.h"
#include "lcancel.h"
#include "jukebox.h"
#include "user_gecko.h"
#include "updater.h"
#include "discord_presence.h"
#include "replay_control.h"
namespace app { int run_settings_window(gx::RenderOptions& options, unsigned long standby_parent, std::function<void()> reload); }
#ifdef MELEE_SOURCE_PORT
#include "source_host.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Bounded translated-side diagnostic for exact M7 RNG triage. The register slot
// is HSD_RandSeedPtr in retail; log the pointed-to seed before each call so
// the caller and actual RNG stream can be compared with the native trace.
#ifndef MELEE_SOURCE_PORT
static uint32_t g_rng_trace_first = 0, g_rng_trace_last = 0;
static uint32_t g_root_anim_jobj = 0;
static void trace_onett_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30)) return;
  const uint32_t gp = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(gp, 0x12Cu)) return;
  auto as_float = [](uint32_t bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; };
  const int car = static_cast<int8_t>(host::rd8(gp + 0x104u));
  const int next = static_cast<int8_t>(host::rd8(gp + 0x118u));
  auto car_x = [&](int index) {
    if (index < 0 || index >= 4) return 0.0f;
    const uint32_t jobj = host::rd32(gp + 0xC8u + 4u * static_cast<uint32_t>(index));
    return host::try_ptr(jobj, 0x3C) ? as_float(host::rd32(jobj + 0x38u)) : 0.0f;
  };
  host::log("[onett-legacy] retrace=%u gobj=%u flag=%u car=%d a=%u wait=%d sub=%d speed=%g x=%g next=%d b=%u wait_b=%d sub_b=%d speed_b=%g next_x=%g",
            retrace, host::rd32(gp + 0x14u), !!(host::rd8(gp + 0xC4u) & 0x80u),
            car, host::rd8(gp + 0x105u), static_cast<int32_t>(host::rd32(gp + 0x108u)),
            static_cast<int32_t>(host::rd32(gp + 0x110u)), as_float(host::rd32(gp + 0x114u)),
            car_x(car), next, host::rd8(gp + 0x119u),
            static_cast<int32_t>(host::rd32(gp + 0x11Cu)),
            static_cast<int32_t>(host::rd32(gp + 0x124u)),
            as_float(host::rd32(gp + 0x128u)), car_x(next));
}
static void trace_magnify_fighter_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30)) return;
  const uint32_t fp = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(fp, 0x1920)) return;
  const uint32_t slot = host::rd8(fp + 0x0Cu);
  const uint32_t magnify_state = 0x804A1DE0u + 0x20u + slot * 0x10u;
  const uint32_t more_flags = 0x80453080u + slot * 0xE90u + 0xADu;
  if (!host::try_ptr(magnify_state, 1) || !host::try_ptr(more_flags, 1)) return;
  auto as_float = [](uint32_t bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; };
  host::log("[magnify-fighter-legacy] retrace=%u slot=%u counter=%d percent=%g camera=%g offscreen=%u more=%02X",
            retrace, slot, static_cast<int32_t>(host::rd32(fp + 0x1910u)),
            as_float(host::rd32(fp + 0x1830u)),
            as_float(host::rd32(0x80452F24u)), host::rd8(magnify_state) >> 7,
             host::rd8(more_flags));
}
static void trace_magnify_render_entry(ppc::Context&) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  constexpr uint32_t players = 0x804A1E00u;
  if (!host::try_ptr(players, 0x20)) return;
  host::log("[magnify-render-legacy] retrace=%u slot0=%u slot1=%u",
            retrace, host::rd8(players) >> 7, host::rd8(players + 0x10u) >> 7);
}
static void trace_fighter_bone_position_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30)) return;
  const uint32_t fp = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(fp, 0x894u)) return;
  const uint32_t cm = host::rd32(fp + 0x890u);
  if (!host::try_ptr(cm, 0x28u)) return;
  int slot = -1;
  for (int i = 0; i < 6; ++i) {
    const uint32_t entities = 0x80453080u + static_cast<uint32_t>(i) * 0xE90u + 0xB0u;
    if (host::try_ptr(entities, 8) &&
        (host::rd32(entities) == gobj || host::rd32(entities + 4u) == gobj)) {
      slot = i;
      break;
    }
  }
  if (slot < 0 || slot > 1) return;
  auto as_float = [](uint32_t bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; };
  const uint32_t stage = 0x8049E6C8u;
  const float x = as_float(host::rd32(cm + 0x1Cu));
  const float cam_x = as_float(host::rd32(stage + 0x10u));
  const float left = as_float(host::rd32(stage)) + cam_x;
  const float right = as_float(host::rd32(stage + 4u)) + cam_x;
  uint32_t x_bits, left_bits, right_bits;
  std::memcpy(&x_bits, &x, sizeof x_bits);
  std::memcpy(&left_bits, &left, sizeof left_bits);
  std::memcpy(&right_bits, &right, sizeof right_bits);
  host::log("[magnify-bounds-legacy] retrace=%u slot=%d caller=%08X x=%g/%08X left=%g/%08X right=%g/%08X",
            retrace, slot, context.lr, x, x_bits, left, left_bits, right,
            right_bits);
}
static void trace_cobj_set_current_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  host::log("[magnify-cobj-legacy] retrace=%u cobj=%08X caller=%08X",
            retrace, context.r[3], context.lr);
}
static void trace_magnify_gate_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  host::log("[magnify-gate-legacy] retrace=%u gate=%08X gobj=%08X caller=%08X",
            retrace, context.last_pc, context.r[3], context.lr);
}
static void trace_camera_visibility_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30)) return;
  int slot = -1;
  for (int i = 0; i < 2; ++i) {
    const uint32_t entities = 0x80453080u + static_cast<uint32_t>(i) * 0xE90u + 0xB0u;
    if (host::try_ptr(entities, 8) &&
        (host::rd32(entities) == gobj || host::rd32(entities + 4u) == gobj)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;
  const uint32_t fp = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(fp, 0x2230u)) return;
  const uint32_t cm = host::rd32(fp + 0x890u);
  if (!host::try_ptr(cm, 0x28u)) return;
  const uint32_t camera_gobj = host::rd32(0x80452C68u);
  const uint32_t camera_cobj = host::try_ptr(camera_gobj, 0x2Cu)
      ? host::rd32(camera_gobj + 0x28u) : 0;
  const uint32_t current_addr = context.r[13] - 16452u;
  const uint32_t current_cobj = host::try_ptr(current_addr, 4)
      ? host::rd32(current_addr) : 0;
  auto as_float = [](uint32_t bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; };
  host::log("[camera-visibility-legacy] retrace=%u slot=%d caller=%08X cam_gobj=%08X cam_cobj=%08X current=%08X flags=%02X/%02X/%02X bone=%g,%g,%g screen=%d,%d",
            retrace, slot, context.lr, camera_gobj, camera_cobj, current_cobj,
            host::rd8(fp + 0x221Fu), host::rd8(fp + 0x2229u),
            host::rd8(fp + 0x2220u), as_float(host::rd32(cm + 0x1Cu)),
            as_float(host::rd32(cm + 0x20u)), as_float(host::rd32(cm + 0x24u)),
            static_cast<int32_t>(host::rd32(fp + 0x2188u)),
            static_cast<int32_t>(host::rd32(fp + 0x218Cu)));
}
static void trace_camera_project_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last || context.r[6] != 1) return;
  const uint32_t camera_gobj = host::rd32(0x80452C68u);
  if (!host::try_ptr(camera_gobj, 0x2Cu)) return;
  const uint32_t cobj = host::rd32(camera_gobj + 0x28u);
  if (context.r[3] != cobj || !host::try_ptr(cobj, 0x30u)) return;
  const uint32_t world = context.r[4];
  const uint32_t eye_wobj = host::rd32(cobj + 0x24u);
  const uint32_t target_wobj = host::rd32(cobj + 0x28u);
  if (!host::try_ptr(world, 12) || !host::try_ptr(eye_wobj, 0x18u) ||
      !host::try_ptr(target_wobj, 0x18u)) return;
  auto as_float = [](uint32_t bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; };
  const uint32_t flags = host::rd32(cobj + 8u);
  host::log("[camera-project-legacy] retrace=%u cobj=%08X world=%g,%g,%g eye=%g,%g,%g up=%08X:%g,%g,%g target=%g,%g,%g viewport=%g,%g,%g,%g scissor=%d,%d,%d,%d",
            retrace, cobj,
            as_float(host::rd32(world)), as_float(host::rd32(world + 4u)),
            as_float(host::rd32(world + 8u)),
            as_float(host::rd32(eye_wobj + 0xCu)), as_float(host::rd32(eye_wobj + 0x10u)),
            as_float(host::rd32(eye_wobj + 0x14u)), flags,
            as_float(host::rd32(cobj + 0x2Cu)), as_float(host::rd32(cobj + 0x30u)),
            as_float(host::rd32(cobj + 0x34u)),
            as_float(host::rd32(target_wobj + 0xCu)), as_float(host::rd32(target_wobj + 0x10u)),
            as_float(host::rd32(target_wobj + 0x14u)),
            as_float(host::rd32(cobj + 0xCu)), as_float(host::rd32(cobj + 0x10u)),
            as_float(host::rd32(cobj + 0x14u)), as_float(host::rd32(cobj + 0x18u)),
            static_cast<int16_t>(host::rd16(cobj + 0x1Cu)),
            static_cast<int16_t>(host::rd16(cobj + 0x1Eu)),
            static_cast<int16_t>(host::rd16(cobj + 0x20u)),
             static_cast<int16_t>(host::rd16(cobj + 0x22u)));
}
static void trace_camera_transform_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  constexpr uint32_t camera = 0x80452C68u;
  if (!host::try_ptr(camera, 0x39Cu) || context.r[3] != host::rd32(camera)) return;
  auto f = [camera](uint32_t offset) {
    float value;
    const uint32_t bits = host::rd32(camera + offset);
    std::memcpy(&value, &bits, sizeof value);
    return value;
  };
  host::log("[camera-transform-legacy] retrace=%u camera=%08X mode=%d slot=%d pitch=%.7g yaw=%.7g translation=%.7g,%.7g quake=%.7g,%.7g*%.7g interest=%.7g,%.7g,%.7g target_interest=%.7g,%.7g,%.7g position=%.7g,%.7g,%.7g target_position=%.7g,%.7g,%.7g fov=%.7g/%.7g",
            retrace, camera, static_cast<int32_t>(host::rd32(camera + 4u)),
            static_cast<int8_t>(host::rd8(camera + 0x2C4u)),
            f(0x2C8u), f(0x2CCu), f(0x84u), f(0x88u),
            f(0xA4u), f(0xA8u), f(0xACu),
            f(0x14u), f(0x18u), f(0x1Cu), f(0x20u), f(0x24u), f(0x28u),
            f(0x2Cu), f(0x30u), f(0x34u), f(0x38u), f(0x3Cu), f(0x40u),
            f(0x44u), f(0x48u));
}
static void trace_camera_quake(ppc::Context& context, const char* phase) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  constexpr uint32_t camera = 0x80452C68u;
  if (!host::try_ptr(camera, 0x39Cu)) return;
  auto f = [camera](uint32_t offset) {
    float value;
    const uint32_t bits = host::rd32(camera + offset);
    std::memcpy(&value, &bits, sizeof value);
    return value;
  };
  float bounds_z = 0.0f;
  float fov = 0.0f;
  if (std::strcmp(phase, "apply") == 0 &&
      host::try_ptr(context.r[3], 0x18u) && host::try_ptr(context.r[4], 0x38u)) {
    uint32_t bounds_bits = host::rd32(context.r[3] + 0x14u);
    uint32_t fov_bits = host::rd32(context.r[4] + 0x30u);
    std::memcpy(&bounds_z, &bounds_bits, sizeof bounds_z);
    std::memcpy(&fov, &fov_bits, sizeof fov);
  }
  const uint32_t camera_globals = 0x803BCCA0u;
  const uint32_t state_machine = 0x80479D30u;
  const uint32_t stage_info = 0x8049E6C8u;
  auto global_f = [camera_globals](uint32_t offset) {
    float value;
    const uint32_t bits = host::rd32(camera_globals + offset);
    std::memcpy(&value, &bits, sizeof value);
    return value;
  };
  const int game_mode = host::try_ptr(state_machine, 1u)
      ? host::rd8(state_machine) : -1;
  const int is_1p = game_mode == 3 || game_mode == 4 || game_mode == 5 ||
      game_mode == 0x0F || game_mode == 0x1C ||
      (game_mode >= 0x20 && game_mode <= 0x26) || game_mode == 0x2B;
  const bool have_camera_globals = host::try_ptr(camera_globals, 0xECu) != nullptr;
  float stage_zoom = 0.0f;
  float stage_max_depth = 0.0f;
  if (host::try_ptr(stage_info, 0x3Cu)) {
    uint32_t zoom_bits = host::rd32(stage_info + 0x34u);
    uint32_t max_bits = host::rd32(stage_info + 0x38u);
    std::memcpy(&stage_zoom, &zoom_bits, sizeof stage_zoom);
    std::memcpy(&stage_max_depth, &max_bits, sizeof stage_max_depth);
  }
  host::log("[camera-quake-legacy] retrace=%u phase=%s offset=%.7g,%.7g translation=%.7g,%.7g scale=%.7g speed=%.7g bounds_z=%.7g fov=%.7g game_mode=%d one_player=%d quake_coeffs=%.7g,%.7g,%.7g,%.7g one_player_scale=%.7g stage_zoom=%.7g stage_max=%.7g state=%08X",
            retrace, phase, f(0xA4u), f(0xA8u), f(0x84u), f(0x88u),
            f(0xACu), f(0x2BCu), bounds_z, fov, game_mode, is_1p,
            have_camera_globals ? global_f(0x54u) : 0.0f,
            have_camera_globals ? global_f(0x58u) : 0.0f,
            have_camera_globals ? global_f(0x5Cu) : 0.0f,
            have_camera_globals ? global_f(0x60u) : 0.0f,
            have_camera_globals ? global_f(0xE8u) : 0.0f,
            stage_zoom, stage_max_depth, context.r[3]);
}
static void trace_camera_apply_quake_entry(ppc::Context& context) {
  trace_camera_quake(context, "apply");
}
static void trace_camera_translation_reset_entry(ppc::Context& context) {
  trace_camera_quake(context, "reset");
}
static void trace_item_timer_entry(ppc::Context&) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  constexpr uint32_t timer = 0x804A0E30u;
  if (!host::try_ptr(timer, 0x10)) return;
  host::log("[item-timer-legacy] retrace=%u countdown=%d weight=%u size=%u",
            retrace, static_cast<int32_t>(host::rd32(timer)),
            host::rd16(timer + 0xCu), host::rd8(timer + 0x4u));
}
static void trace_item_pick_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t table = context.r[3];
  if (!host::try_ptr(table, 0x10)) return;
  host::log("[item-pick-legacy] retrace=%u table=%08X caller=%08X weight=%u size=%u",
            retrace, table, context.lr, host::rd16(table + 0x8u), host::rd8(table));
}
static void trace_item_position_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t position = context.r[3];
  if (!host::try_ptr(position, 12)) return;
  host::log("[item-position-legacy] retrace=%u caller=%08X pos=%08X,%08X,%08X",
            retrace, context.lr, host::rd32(position), host::rd32(position + 4u),
            host::rd32(position + 8u));
}
static void trace_item_create_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t spawn = context.r[3];
  if (!host::try_ptr(spawn, 0x4Cu)) return;
  host::log("[item-create-legacy] retrace=%u caller=%08X spawn=%08X kind=%d pos=%08X,%08X,%08X prev=%08X,%08X,%08X vel=%08X,%08X,%08X",
            retrace, context.lr, spawn, static_cast<int32_t>(host::rd32(spawn + 8u)),
            host::rd32(spawn + 0x14u), host::rd32(spawn + 0x18u),
            host::rd32(spawn + 0x1Cu), host::rd32(spawn + 0x20u),
            host::rd32(spawn + 0x24u), host::rd32(spawn + 0x28u),
            host::rd32(spawn + 0x2Cu), host::rd32(spawn + 0x30u),
            host::rd32(spawn + 0x34u));
}
static void trace_item_sword_spawned_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t item = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(item, 0xD0u) || host::rd32(item + 0x10u) != 12u) return;
  const uint32_t article = host::rd32(item + 0xC4u);
  const uint32_t attrs = host::try_ptr(article, 8u) ? host::rd32(article + 4u) : 0;
  host::log("[item-sword-spawn-legacy] retrace=%u phase=entry gobj=%08X item=%08X article=%08X attrs=%08X launch=%08X vy=%08X",
            retrace, gobj, item, article, attrs,
            host::try_ptr(attrs, 0x10u) ? host::rd32(attrs + 0xCu) : 0u,
            host::rd32(item + 0x44u));
  if (host::try_ptr(article, 0x18u) && host::try_ptr(attrs, 0x1Cu)) {
    host::log("[item-sword-data-legacy] retrace=%u article-slots=%08X/%08X/%08X/%08X/%08X/%08X attrs-words=%08X/%08X/%08X/%08X/%08X/%08X/%08X",
              retrace, host::rd32(article), host::rd32(article + 4u),
              host::rd32(article + 8u), host::rd32(article + 12u),
              host::rd32(article + 16u), host::rd32(article + 20u),
              host::rd32(attrs), host::rd32(attrs + 4u),
              host::rd32(attrs + 8u), host::rd32(attrs + 12u),
              host::rd32(attrs + 16u), host::rd32(attrs + 20u),
              host::rd32(attrs + 24u));
  }
}
static void trace_item_sword_state_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t item = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(item, 0x58u) || host::rd32(item + 0x10u) != 12u) return;
  host::log("[item-sword-state-legacy] retrace=%u state=%d flags=%08X y=%08X vy=%08X",
            retrace, static_cast<int32_t>(context.r[4]), context.r[5],
            host::rd32(item + 0x50u), host::rd32(item + 0x44u));
}
static void trace_item_sword_proc_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t item = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(item, 0x58u) || host::rd32(item + 0x10u) != 12u) return;
  host::log("[item-sword-proc-legacy] retrace=%u pc=%08X caller=%08X gobj=%08X item=%08X pos=%08X,%08X,%08X vel=%08X,%08X,%08X",
            retrace, context.last_pc, context.lr, gobj, item,
            host::rd32(item + 0x4Cu), host::rd32(item + 0x50u),
            host::rd32(item + 0x54u), host::rd32(item + 0x40u),
            host::rd32(item + 0x44u), host::rd32(item + 0x48u));
}
static void trace_item_sword_physics_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t item = host::rd32(gobj + 0x2Cu);
  if (!host::try_ptr(item, 0x58u) || host::rd32(item + 0x10u) != 12u) return;
  host::log("[item-sword-physics-legacy] retrace=%u caller=%08X gobj=%08X gravity=%08X max=%08X y=%08X vy=%08X",
            retrace, context.lr, gobj,
            ppc::double_to_float_bits(context.f[1].ps0),
            ppc::double_to_float_bits(context.f[2].ps0),
            host::rd32(item + 0x50u), host::rd32(item + 0x44u));
}
static void trace_item_init_entry(ppc::Context&) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  constexpr uint32_t controller = 0x8046B6A0u;
  const int mode = static_cast<int8_t>(host::rd8(controller + 0x24C8u + 0xBu));
  const int stage = host::rd16(controller + 0x24C8u + 0xEu);
  const uint32_t common = host::rd32(0x804D6D28u);
  int low = 0, high = 0;
  if (mode >= 0 && mode < 8 && host::try_ptr(common + 0xFCu + 8u * mode, 8)) {
    low = static_cast<int32_t>(host::rd32(common + 0xFCu + 8u * mode));
    high = static_cast<int32_t>(host::rd32(common + 0x100u + 8u * mode));
  }
  host::log("[item-init-legacy] retrace=%u mode=%d stage=%d low=%d high=%d common=%08X",
            retrace, mode, stage, low, high, common);
}
static void trace_fighter_sfx_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  host::log("[fighter-sfx-legacy] retrace=%u id=%d caller=%08X",
            retrace, static_cast<int32_t>(context.r[4]), context.lr);
}
static void trace_ax_sfx_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  host::log("[ax-sfx-legacy] retrace=%u id=%d channel=6 caller=%08X",
            retrace, static_cast<int32_t>(context.r[3]), context.lr);
}
// MELEE_TRACE_EFFECTS: every effect spawn with the match frame, to diff against the native game's
// trace at the same two functions (efSync_Spawn, efAsync_Spawn; mu_rng_trace.c).
static void trace_effect_spawn(const char* kind, int32_t gfx_id) {
  uint32_t major = 0, minor = 0, match_frame = 0;
  host::current_scene(&major, &minor, &match_frame);
  host::log("[effect-spawn] match=%u kind=%s id=%d", match_frame, kind, gfx_id);
}
static void trace_ef_sync_entry(ppc::Context& context) { trace_effect_spawn("sync", (int32_t)context.r[3]); }
static void trace_ef_async_entry(ppc::Context& context) { trace_effect_spawn("async", (int32_t)context.r[6]); }
static void trace_audio_start_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  host::log("[audio-start-legacy] retrace=%u id=%d vol=%d pan=%d track=%d channel=%d caller=%08X",
            retrace, static_cast<int32_t>(context.r[3]),
            static_cast<int32_t>(context.r[4]), static_cast<int32_t>(context.r[5]),
            static_cast<int32_t>(context.r[6]), static_cast<int32_t>(context.r[7]),
            context.lr);
}
// MELEE_TRACE_AX_VOICES (M4 diagnostic): every HSD_AudioSFXStartParam request for the whole run with
// the timebase, to place it on the AX frame timeline next to the per-voice trace (ax_ucode.h).
// MELEE_TEST_AXLIST: while a sound starts, the caller's saved return address (caller r1 + 0xC) must not
// change; the first function entered after it did is reported with the interpreter's recent history.
static uint32_t g_axw_slot = 0, g_axw_value = 0, g_axw_r1 = 0, g_axw_prev = 0;
static bool g_axw_reported = false;
static void ax_watch_any(ppc::Context& c, uint32_t pc) {
  if (!g_axw_slot || g_axw_reported) return;
  if (c.r[1] >= g_axw_r1) { g_axw_slot = 0; return; }   // back at the caller's level: the call is over
  const uint32_t now = host::rd32(g_axw_slot);
  if (now != g_axw_value) {
    g_axw_reported = true;
    host::log("[axwatch] retrace=%u slot %08X changed %08X -> %08X; entering %08X %s, previous entry %08X %s, r1=%08X",
              host::retrace_count(), g_axw_slot, g_axw_value, now, pc, host::symbol_name(pc), g_axw_prev, host::symbol_name(g_axw_prev), c.r[1]);
    ppc::interpreter_dump_recent(c);
  }
  g_axw_prev = pc;
}
static void ax_watch_after_callback(uint32_t addr) {
  if (!g_axw_slot || g_axw_reported) return;
  const uint32_t now = host::rd32(g_axw_slot);
  if (now == g_axw_value) return;
  g_axw_reported = true;
  host::log("[axwatch] retrace=%u slot %08X changed %08X -> %08X during the host callback %08X %s (last entry %08X %s)",
            host::retrace_count(), g_axw_slot, g_axw_value, now, addr, host::symbol_name(addr), g_axw_prev, host::symbol_name(g_axw_prev));
  ppc::interpreter_dump_recent(*host::cpu);
}
static void ax_watch_start(ppc::Context& c) {
  if (g_axw_reported || c.lr != 0x80023860u) return;   // the sound request call that crashed (lbAudioAx_800237A8)
  g_axw_r1 = c.r[1]; g_axw_slot = c.r[1] + 0xC; g_axw_value = host::rd32(g_axw_slot); g_axw_prev = 0x8038CFF4u;
}
static void check_ax_free_list(ppc::Context& context) {
  ax_watch_start(context);
  static bool reported = false;
  if (reported) return;
  uint32_t node = host::rd32(0x804DB6A0u - 0x3F10u);
  auto bad = [](uint32_t p) { return p && (p < 0x80400000u || p >= 0x804E0000u || (p & 3)); };
  for (int i = 0; node && i < 256; ++i) {
    const uint32_t other = host::rd32(node);
    if (bad(node) || bad(other)) {
      reported = true;
      host::log("[axlist] retrace=%u bad free node %08X (link0 %08X) at depth %d (sound id %d)", host::retrace_count(), node,
                bad(node) ? 0u : other, i, (int32_t)context.r[3]);
      ppc::interpreter_dump_recent(context);
      return;
    }
    node = host::rd32(node + 4);
  }
}
static void trace_ax_request_entry(ppc::Context& context) {
  host::log("[ax-request-legacy] retrace=%u tb=%llu id=%d vol=%d pan=%d track=%d channel=%d",
            host::retrace_count(), static_cast<unsigned long long>(context.tb),
            static_cast<int32_t>(context.r[3]), static_cast<int32_t>(context.r[4]),
            static_cast<int32_t>(context.r[5]), static_cast<int32_t>(context.r[6]),
            static_cast<int32_t>(context.r[7]));
}
static void trace_ax_stream_start_entry(ppc::Context& context) {   // HSD_SynthPStreamStart(entrynum, ...)
  host::log("[ax-stream-legacy] retrace=%u tb=%llu phase=start entry=%d", host::retrace_count(),
            static_cast<unsigned long long>(context.tb), static_cast<int32_t>(context.r[3]));
}
static void trace_ax_stream_data_entry(ppc::Context& context) {    // HSD_SynthPStreamFirstHakoDataCallback
  host::log("[ax-stream-legacy] retrace=%u tb=%llu phase=first-data entry=%d base=%08X", host::retrace_count(),
            static_cast<unsigned long long>(context.tb), static_cast<int32_t>(host::rd32(0x804D7764u)),
            host::rd32(0x804D7780u) + (host::rd32(0x804D7768u) << 16));
}
static void trace_ax_bank_entry(ppc::Context& context) {           // HSD_SynthSFXSampleLoadCallback
  const uint32_t bank = host::rd32(0x804C2A64u);                    // HSD_Synth_804C2A60[0].bankID
  if (host::rd32(0x804D7738u) != 0 || bank >= 32u) return;          // a cancelled load relocates nothing
  host::log("[ax-bank-legacy] retrace=%u tb=%llu entry=%d bank=%u base=%08X size=%08X", host::retrace_count(),
            static_cast<unsigned long long>(context.tb), static_cast<int32_t>(host::rd32(0x804C2A60u)), bank,
            host::rd32(0x804C2B60u + 4u * bank), host::rd32(0x804C2AC4u));
}
static void trace_ax_driver_callback_entry(ppc::Context&) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const int32_t driver_tick = static_cast<int32_t>(host::rd32(0x804D778Cu));
  uint32_t node = host::rd32(0x804D7794u);
  host::log("[ax-driver-clock-legacy] retrace=%u tick=%d head=%08X",
            retrace, driver_tick, node);
  for (uint32_t depth = 0; node && depth < 96 && host::try_ptr(node, 0x38u);
       ++depth, node = host::rd32(node + 4u)) {
    const uint32_t id = host::rd16(node + 0x16u);
    if (id == 166u || id == 258u || id == (390005u & 0xFFFFu)) {
      host::log("[ax-driver-queue-legacy] retrace=%u tick=%d depth=%u id=%u vid=%d frame=%d flags=%08X cmd=%08X",
                retrace, driver_tick, depth, id,
                static_cast<int32_t>(host::rd32(node + 0x10u)),
                static_cast<int32_t>(host::rd32(node + 0x30u)),
                host::rd32(node + 8u), host::rd32(node + 0x2Cu));
    }
  }
}
static void trace_fighter_sfx_command_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t command_info = context.r[4];
  if (!host::try_ptr(command_info, 0xCu)) return;
  const uint32_t command = host::rd32(command_info + 0x8u);
  if (!host::try_ptr(command, 0xCu)) return;
  host::log("[fighter-sfx-cmd-legacy] retrace=%u command=%08X words=%08X/%08X/%08X caller=%08X",
            retrace, command, host::rd32(command), host::rd32(command + 4u),
            host::rd32(command + 8u), context.lr);
}
static void trace_synth_start_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  uint32_t bits[5];
  for (int i = 0; i < 5; ++i) {
    const float value = static_cast<float>(context.f[i + 1].ps0);
    std::memcpy(&bits[i], &value, sizeof value);
  }
  host::log("[synth-start-legacy] retrace=%u id=%d vol=%d vol2=%d pan=%d priority=%d itd=%d group=%d pitch=%08X/%08X mix=%08X/%08X/%08X",
            retrace, static_cast<int32_t>(context.r[3]), context.r[4],
            context.r[5], context.r[6], context.r[7], context.r[8], context.r[9],
            bits[0], bits[1], bits[2], bits[3], bits[4]);
}
static void trace_synth_voice_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  const int32_t sfx_id = static_cast<int32_t>(context.r[3]);
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last ||
      (sfx_id != 0 && sfx_id != 239 && sfx_id != 111 &&
       sfx_id != 176 && sfx_id != 1389)) return;
  const uint32_t id = context.r[3];
  uint32_t entry = host::rd32(0x804C29E0u + 4u * (id & 0x1Fu));
  while (entry && host::try_ptr(entry, 0x10u)) {
    if (host::rd32(entry + 4u) == id) break;
    entry = host::rd32(entry);
  }
  if (!entry || !host::try_ptr(entry, 0x50u)) return;
  const uint32_t voice_count = host::rd32(entry + 8u);
  const uint32_t sample_rate = host::rd32(entry + 0xCu);
  host::log("[synth-entry-legacy] retrace=%u id=%u voices=%u rate=%u entry=%08X",
            retrace, id, voice_count, sample_rate, entry);
  for (uint32_t voice = 0; voice < voice_count && voice < 2; ++voice) {
    const uint32_t block = entry + 0x10u + voice * 0x40u;
    if (!host::try_ptr(block, 0x40u)) return;
    char words[129];
    for (uint32_t i = 0; i < 32; ++i) {
      std::snprintf(words + i * 4u, sizeof words - i * 4u, "%04X",
                    host::rd16(block + i * 2u));
    }
    host::log("[synth-voice-legacy] retrace=%u id=%u voice=%u/%u rate=%u words=%s",
              retrace, id, voice, voice_count, sample_rate, words);
  }
}
static void trace_ground_friction_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t fp = host::rd32(gobj + 0x2Cu);
  if (!fp || !host::try_ptr(fp, 0x850u) || host::rd32(fp + 0x10u) != 0x32u) return;
  const uint8_t special_byte = host::rd8(fp + 0x594u);
  const float facing = static_cast<float>(context.f[2].ps0);
  uint32_t facing_bits = 0;
  std::memcpy(&facing_bits, &facing, sizeof facing_bits);
  host::log("[ground-phys-legacy] retrace=%u phase=transform-input special=%u flag_byte=%02X offset_z=%08X facing=%08X gr=%08X",
            retrace, (special_byte & 0x80u) != 0, special_byte,
            host::rd32(fp + 0x6ACu), facing_bits, host::rd32(fp + 0xECu));
  host::log("[ground-phys-legacy] retrace=%u phase=before motion=%u friction=%.9g facing=%.9g gr=%08X e4=%08X e8=%08X vx=%08X ax=%08X normal=%08X/%08X caller=%08X",
            retrace, host::rd32(fp + 0x10u), context.f[1].ps0, context.f[2].ps0,
            host::rd32(fp + 0xECu), host::rd32(fp + 0xE4u), host::rd32(fp + 0xE8u),
            host::rd32(fp + 0x80u), host::rd32(fp + 0x74u),
            host::rd32(fp + 0x844u), host::rd32(fp + 0x848u), context.lr);
}
static void trace_ground_accel_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t fp = context.r[3];
  if (!fp || !host::try_ptr(fp, 0x850u) || host::rd32(fp + 0x10u) != 0x32u) return;
  host::log("[ground-phys-legacy] retrace=%u phase=calc-entry gr=%08X friction=%.9g e4=%08X caller=%08X",
            retrace, host::rd32(fp + 0xECu), context.f[1].ps0,
            host::rd32(fp + 0xE4u), context.lr);
}
static void trace_ground_self_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t gobj = context.r[3];
  if (!host::try_ptr(gobj, 0x30u)) return;
  const uint32_t fp = host::rd32(gobj + 0x2Cu);
  if (!fp || !host::try_ptr(fp, 0x850u) || host::rd32(fp + 0x10u) != 0x32u) return;
  host::log("[ground-phys-legacy] retrace=%u phase=self-entry gr=%08X e4=%08X vx=%08X normal=%08X/%08X caller=%08X",
            retrace, host::rd32(fp + 0xECu), host::rd32(fp + 0xE4u),
            host::rd32(fp + 0x80u), host::rd32(fp + 0x844u),
            host::rd32(fp + 0x848u), context.lr);
}
static void trace_root_delta_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t output = context.r[5];
  if (output < 0x6A4u) return;
  const uint32_t fp = output - 0x6A4u;
  if (!host::try_ptr(fp, 0x6B0u) || host::rd32(fp + 0x10u) != 0x32u) return;
  if (context.lr < 0x8006E054u || context.lr >= 0x8006E7B8u) return;
  const uint32_t current = context.r[3];
  const uint32_t previous = context.r[4];
  if (!host::try_ptr(current, 12u) || !host::try_ptr(previous, 12u)) return;
  host::log("[root-delta-legacy] retrace=%u phase=before-diff current=%08X previous=%08X delta_before=%08X caller=%08X",
            retrace, host::rd32(current + 8u), host::rd32(previous + 8u),
            host::rd32(output + 8u), context.lr);
}
static void trace_model_scale_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t fp = context.r[3];
  if (!host::try_ptr(fp, 0x850u) || host::rd32(fp + 0x10u) != 0x32u) return;
  if (context.lr < 0x8006E054u || context.lr >= 0x8006E7B8u) return;
  const uint32_t joint_z = host::try_ptr(g_root_anim_jobj, 0x44u)
                               ? host::rd32(g_root_anim_jobj + 0x40u)
                               : 0;
  host::log("[root-translation-legacy] retrace=%u phase=raw-before-scale flag_byte=%02X z=%08X joint_z=%08X model_scale_y=%08X attr_model_scale=%08X caller=%08X",
            retrace, host::rd8(fp + 0x594u), host::rd32(fp + 0x694u),
            joint_z, host::rd32(fp + 0x38u), host::rd32(fp + 0x19Cu),
            context.lr);
}
static void trace_root_anim_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last) return;
  const uint32_t fp = context.r[3];
  if (!host::try_ptr(fp, 0x850u) || host::rd32(fp + 0x10u) != 0x32u ||
      !host::try_ptr(context.r[5], 0x44u)) {
    g_root_anim_jobj = 0;
    return;
  }
  g_root_anim_jobj = context.r[5];
}
static void trace_root_anim_channel_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last ||
      context.r[3] != g_root_anim_jobj || context.r[4] != 7u ||
      !host::try_ptr(context.r[5], 4u) ||
      !host::try_ptr(g_root_anim_jobj, 0x44u)) return;
  host::log("[root-anim-channel-legacy] retrace=%u type=%u sample=%08X prior_z=%08X caller=%08X",
            retrace, context.r[4], host::rd32(context.r[5]),
            host::rd32(g_root_anim_jobj + 0x40u), context.lr);
}
static void trace_root_fobj_entry(ppc::Context& context) {
  const uint32_t retrace = host::retrace_count();
  const uint32_t fobj = context.r[3];
  if (retrace < g_rng_trace_first || retrace > g_rng_trace_last ||
      context.r[4] != g_root_anim_jobj || !host::try_ptr(fobj, 0x30u) ||
      host::rd8(fobj + 0x13u) != 7u) return;
  const uint32_t time = host::rd32(fobj + 0x1Cu);
  const uint32_t p0 = host::rd32(fobj + 0x20u);
  const uint32_t p1 = host::rd32(fobj + 0x24u);
  const uint32_t d0 = host::rd32(fobj + 0x28u);
  const uint32_t d1 = host::rd32(fobj + 0x2Cu);
  host::log("[root-fobj-legacy] retrace=%u type=%u flags=%02X state=%u op=%u intrp=%u frac=%u/%u start=%d term=%u time=%08X p0=%08X p1=%08X d0=%08X d1=%08X caller=%08X",
            retrace, host::rd8(fobj + 0x13u), host::rd8(fobj + 0x10u),
            host::rd8(fobj + 0x10u) & 0x0Fu, host::rd8(fobj + 0x11u),
            host::rd8(fobj + 0x12u), host::rd8(fobj + 0x14u),
            host::rd8(fobj + 0x15u),
            static_cast<int16_t>(host::rd16(fobj + 0x18u)),
            host::rd16(fobj + 0x1Au), time, p0, p1, d0, d1, context.lr);
}
#endif
#include <algorithm>
#include <atomic>
#include <csignal>
#include <exception>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ppc { void init_dispatch(); }

static void usage() {
  std::printf("melee_port --iso <path> [--frames N] [--fast] [--headless] [--scale N|auto] [--window WxH] [--vsync]\n"
              "           [--dlss-mode 0..5 --frame-generation[=2x|3x|4x|5x|6x|dynamic]] [--ssao 0..1]\n"
              "           [--aspect auto|73:60|4:3|16:9|stretch] [--widescreen|--true-widescreen]\n"
              "           [--fps N|monitor|unlocked] [--frame-mode extrapolate|interpolate|authored|off] [--threaded-renderer]\n"
              "           [--fullscreen] [--backend d3d12|d3d11] [--dlss off|dlaa|quality|balanced|performance|ultra] [--frame-times out.csv] [--music 0-100|--no-music] [--volume 0-100] [--audio-dump out.wav]\n"
              "           [--settings-path file --load-settings --import-cosmetic file|--scan-disc-skins mod.iso|--select-cosmetic id|--enable-project-effects|--restore-vanilla-cosmetics|--cosmetic-status]\n"
              "           [--capture out.ppm --capture-frame N] [--trace-calls] [--quiet]\n");
  std::printf("           [--lobby-direct NAME#123 --lobby-character 0..255 --lobby-status-file path]\n");
  std::printf("           [--replay <file.slp> [--as-experienced | --trace <file.trace>]]\n");
#ifdef MELEE_SOURCE_PORT
  std::printf("           [--card-self-test <new scratch directory>]\n"
              "           [--replay <file.slp> --replay-dir <directory>] [--record-native]\n"
              "           [--mod-profile <name>] [--mod-dir <disc files directory>]... [--mod-iso <patched ISO>]... [--mod-gci <save.gci>]...\n");
#endif
}

#ifndef MELEE_SOURCE_PORT
// --rng-seed for the recompiled guest: seed once at the first VS item-setup boundary, before
// it_8026D018 draws the ambient item-spawn delay. Keep the scripted-input origin at the first
// GS_VS frame callback. ppc::add_entry_hook also sees direct translated calls.
static void install_rng_seed_hook() {
  if (!host::options.rng_seed_set) return;
  ppc::add_entry_hook(0x8026D018u, [](ppc::Context&) {
    // HSD_RandSeedPtr (0x804D5F94) holds the address of the actual seed word (usually the
    // static `seed` at 0x804D5F90); write through the indirection, exactly as digest_state()
    // reads through it, rather than overwriting the pointer variable itself.
    const uint32_t seed_addr = host::rd32(0x804D5F94u);
    if (seed_addr && host::try_ptr(seed_addr, 4)) host::wr32(seed_addr, host::options.rng_seed);
    host::log("rng-seed: forced %08X at retrace %u (VS item-setup boundary)",
              host::options.rng_seed, host::retrace_count());
  });
  ppc::add_entry_hook(0x8016D800u, [](ppc::Context&) {
    static bool marked = false;
    if (marked) return;
    marked = true;
    host::input_mark_match_start();
  });
}
#endif

// Windows hands out ~15.6 ms timer granularity by default, so every pacing sleep (the 60 Hz
// retrace, the presentation deadline, the audio device wait) overshoots by up to a frame. One
// millisecond is what games ask for, and it is what makes 60 Hz land on 60 Hz.
struct TimerResolution {
  bool raised = timeBeginPeriod(1) == TIMERR_NOERROR;
  ~TimerResolution() { if (raised) timeEndPeriod(1); }
};

#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif

// Crash report: a native crash used to close the console with nothing on screen. Any unhandled
// exception now leaves melee_port_crash.txt (exception, module offset, last guest functions), a
// minidump, the same lines in melee_port.log, and a dialog pointing at them when a player launched it.
static bool g_crash_dialog = true;

// --profile: samples where the simulation thread is ~1000 times a second and logs the hottest
// functions at exit. Translated game functions are named through the dispatch table, everything
// else through the executable's debug symbols. The sample buffer is reserved up front: the sampler
// must not allocate while the simulation thread (which may hold the heap lock) is suspended.
struct SimProfiler {
  HANDLE target = nullptr;
  std::atomic<bool> running{false};
  std::thread sampler;
  bool render_thread = false;   // --profile-render: after warm-up, sample the busiest other thread instead
  DWORD main_thread_id = 0;
  // CPU time (100 ns units) of every other thread of this process, by thread id.
  std::unordered_map<DWORD, uint64_t> thread_times() {
    std::unordered_map<DWORD, uint64_t> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te{}; te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == main_thread_id || te.th32ThreadID == GetCurrentThreadId()) continue;
      HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
      if (!h) continue;
      FILETIME c, e, k, u;
      if (GetThreadTimes(h, &c, &e, &k, &u))
        out[te.th32ThreadID] = (((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime);
      CloseHandle(h);
    }
    CloseHandle(snap);
    return out;
  }
  std::vector<uint64_t> samples;
  std::vector<uint64_t> returns;   // [rsp] at the sample: the caller when the sample is in a leaf system routine
  std::vector<uint32_t> sample_frames;
  std::vector<uint64_t> sample_cycles;   // target thread cycles since the previous sample (0: it was waiting)
  void start() {
    main_thread_id = GetCurrentThreadId();
    if (!render_thread) DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS);
    samples.reserve(1u << 22);
    returns.reserve(1u << 22);
    sample_frames.reserve(1u << 22);
    sample_cycles.reserve(1u << 22);
    running = true;
    sampler = std::thread([this] {
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
      if (render_thread) {
        Sleep(8000);   // past boot and menus into the match
        auto before = thread_times();
        Sleep(1000);
        auto after = thread_times();
        DWORD busiest = 0; uint64_t most = 0;
        for (auto& kv : after) { auto b = before.find(kv.first); uint64_t d = kv.second - (b == before.end() ? 0 : b->second); if (d > most) { most = d; busiest = kv.first; } }
        if (busiest) target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, busiest);
        host::log("profile: sampling thread %lu (%.0f%% of a core over 1 s)", busiest, most / 1e5);
        if (!target) return;
      }
      ULONG64 last_cycles = 0;
      while (running.load(std::memory_order_relaxed) && samples.size() < samples.capacity()) {
        if (SuspendThread(target) != (DWORD)-1) {
          CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_CONTROL;
          ULONG64 cycles = 0; QueryThreadCycleTime(target, &cycles);
          if (GetThreadContext(target, &ctx)) {
            samples.push_back(ctx.Rip);
            returns.push_back(*(uint64_t*)ctx.Rsp);
            sample_frames.push_back(host::profiler_frame_id());
            sample_cycles.push_back(cycles - last_cycles);
          }
          last_cycles = cycles;
          ResumeThread(target);
        }
        Sleep(1);
      }
    });
  }
  void report() {
    if (!running) return;
    running = false;
    if (sampler.joinable()) sampler.join();
    // MELEE_PROFILE_SAMPLES=<csv>: the raw samples, for offline symbolization of code this report
    // cannot name (the Source Port's GCC game DLL has no PDB; tools/profile_symbolize.py reads them).
    if (const char* path = std::getenv("MELEE_PROFILE_SAMPLES"); path && *path) {
      if (FILE* f = std::fopen(path, "w")) {
        std::fprintf(f, "# exe %llx\n", (unsigned long long)(uintptr_t)GetModuleHandleA(nullptr));
        HMODULE mods[512]; DWORD needed = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed))
          for (DWORD m = 0; m < std::min<DWORD>(needed / sizeof(HMODULE), 512); ++m) {
            MODULEINFO mi{}; char name[MAX_PATH] = "";
            if (K32GetModuleInformation(GetCurrentProcess(), mods[m], &mi, sizeof mi) && K32GetModuleBaseNameA(GetCurrentProcess(), mods[m], name, MAX_PATH))
              std::fprintf(f, "# module %llx %lx %s\n", (unsigned long long)(uintptr_t)mi.lpBaseOfDll, (unsigned long)mi.SizeOfImage, name);
          }
        std::fprintf(f, "frame,rip,ret,cycles\n");
        for (size_t i = 0; i < samples.size(); ++i)
          std::fprintf(f, "%u,%llx,%llx,%llu\n", sample_frames[i], (unsigned long long)samples[i], (unsigned long long)returns[i], (unsigned long long)sample_cycles[i]);
        std::fclose(f);
      }
    }
    std::vector<std::pair<uintptr_t, uint32_t>> fns;
    fns.reserve(guest::fn_table_count);
    for (size_t i = 0; i < guest::fn_table_count; ++i) fns.push_back({(uintptr_t)guest::fn_table[i].fn, guest::fn_table[i].addr});
    std::sort(fns.begin(), fns.end());
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    std::unordered_map<std::string, uint64_t> hits, slow_hits, system_callers;
    const auto& slow_frames = host::slow_sim_frames();
    const uintptr_t exe_base = (uintptr_t)GetModuleHandleA(nullptr);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(exe_base + ((IMAGE_DOS_HEADER*)exe_base)->e_lfanew);
    const uintptr_t exe_end = exe_base + nt->OptionalHeader.SizeOfImage;
    std::unordered_map<uint64_t, std::string> host_names;
    auto host_name = [&](uint64_t addr) -> std::string {
      auto cached = host_names.find(addr);
      if (cached != host_names.end()) return cached->second;
      char buf[sizeof(SYMBOL_INFO) + 256]{}; SYMBOL_INFO* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255;
      DWORD64 disp = 0;
      std::string name = SymFromAddr(process, addr, &disp, sym) ? std::string(sym->Name) : "?";
      return host_names.emplace(addr, name).first->second;
    };
    uint64_t guest_hits = 0, slow_guest_hits = 0, slow_total = 0;
    for (size_t si = 0; si < samples.size(); ++si) {
      const uint64_t rip = samples[si];
      const bool slow = !render_thread &&
          std::binary_search(slow_frames.begin(), slow_frames.end(), sample_frames[si]);
      std::string label;
      auto it = std::upper_bound(fns.begin(), fns.end(), std::make_pair((uintptr_t)rip, UINT32_MAX));
      if (it != fns.begin() && rip < fns.back().first + 0x10000) {
        --it;
        ++guest_hits;
        if (slow) ++slow_guest_hits;
        label = std::string("game ") + host::symbol_name(it->second);
      } else {
        const bool in_exe = rip >= exe_base && rip < exe_end;
        label = std::string(in_exe ? "host " : "system ") + host_name(rip);
        // A system DLL has no symbols here, so its exported names are only nearest guesses; the
        // return address shows which of our functions called into it.
        if (!in_exe && returns[si] >= exe_base && returns[si] < exe_end) ++system_callers[host_name(returns[si])];
      }
      ++hits[label];
      if (slow) { ++slow_hits[label]; ++slow_total; }
    }
    std::vector<std::pair<uint64_t, std::string>> top;
    for (auto& kv : hits) top.push_back({kv.second, kv.first});
    std::sort(top.rbegin(), top.rend());
    const double total = (double)std::max<size_t>(1, samples.size());
    host::log("profile: %zu samples of the %s thread, game code %.1f%%, runtime %.1f%%", samples.size(), render_thread ? "busiest non-simulation" : "simulation",
              100.0 * guest_hits / total, 100.0 * (samples.size() - guest_hits) / total);
    for (size_t i = 0; i < top.size() && i < 40; ++i) host::log("profile: %5.1f%%  %s", 100.0 * top[i].first / total, top[i].second.c_str());
    if (!render_thread && !slow_frames.empty()) {
      std::vector<std::pair<uint64_t, std::string>> slow_top;
      for (auto& kv : slow_hits) slow_top.push_back({kv.second, kv.first});
      std::sort(slow_top.rbegin(), slow_top.rend());
      const double selected = (double)std::max<uint64_t>(1, slow_total);
      host::log("profile slow frames: %zu frames, %llu samples, game code %.1f%%, runtime %.1f%%",
                slow_frames.size(), (unsigned long long)slow_total,
                100.0 * slow_guest_hits / selected, 100.0 * (slow_total - slow_guest_hits) / selected);
      for (size_t i = 0; i < slow_top.size() && i < 25; ++i)
        host::log("profile slow: %5.1f%%  %s", 100.0 * slow_top[i].first / selected, slow_top[i].second.c_str());
    }
    std::vector<std::pair<uint64_t, std::string>> callers;
    for (auto& kv : system_callers) callers.push_back({kv.second, kv.first});
    std::sort(callers.rbegin(), callers.rend());
    for (size_t i = 0; i < callers.size() && i < 15; ++i) host::log("profile: %5.1f%%  system call from %s", 100.0 * callers[i].first / total, callers[i].second.c_str());
  }
};
static SimProfiler g_profiler;
static bool g_profile = false;
// What an unhandled C++ exception said. The thrown object and its type list are in the exception
// record (the Visual C++ layout for 64-bit programs: every pointer in the type list is an offset
// from the image base in parameter 3). Only an object that is a std::exception is read.
static void cxx_exception_text(const EXCEPTION_RECORD* er, char* out, size_t size) {
  out[0] = 0;
  if (er->ExceptionCode != 0xE06D7363u || er->NumberParameters < 4) return;
  __try {
    const uint8_t* base = (const uint8_t*)er->ExceptionInformation[3];
    const uint8_t* object = (const uint8_t*)er->ExceptionInformation[1];
    const int32_t* throw_info = (const int32_t*)er->ExceptionInformation[2];
    if (!base || !object || !throw_info) return;
    const int32_t* types = (const int32_t*)(base + throw_info[3]);
    for (int32_t i = 0; i < types[0] && i < 16; ++i) {
      const int32_t* type = (const int32_t*)(base + types[1 + i]);
      const char* name = (const char*)(base + type[1]) + 2 * sizeof(void*);
      if (i == 0) std::snprintf(out, size, "type %s", name);
      if (std::strcmp(name, ".?AVexception@std@@") != 0 || type[3] != -1) continue;
      const std::exception* e = (const std::exception*)(object + type[2]);
      const char* first = (const char*)(base + ((const int32_t*)(base + types[1]))[1]) + 2 * sizeof(void*);
      std::snprintf(out, size, "%s: %s", first, e->what());
      return;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// The call stack of the thread that stopped, as module+offset lines (no symbols are shipped).
static void crash_stack(const CONTEXT* at, FILE* f) {
  CONTEXT c = *at;
  for (int frame = 0; frame < 32 && c.Rip; ++frame) {
    HMODULE module = nullptr; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)c.Rip, &module))
      GetModuleFileNameA(module, name, MAX_PATH);
    const char* leaf = std::strrchr(name, '\\') ? std::strrchr(name, '\\') + 1 : name;
    char line[400];
    std::snprintf(line, sizeof line, "stack %2d: %s+0x%llX", frame, leaf, (unsigned long long)(c.Rip - (DWORD64)module));
    host::log("%s", line);
    if (f) std::fprintf(f, "%s\n", line);
    DWORD64 image = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
    if (!function) {   // a leaf function: the return address is at the top of the stack
      __try { c.Rip = *(const DWORD64*)c.Rsp; c.Rsp += 8; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
      continue;
    }
    void* handler_data = nullptr; DWORD64 establisher = 0;
    __try { RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, function, &c, &handler_data, &establisher, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
  }
}
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
  static volatile LONG entered = 0;
  if (InterlockedExchange(&entered, 1)) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* er = info->ExceptionRecord;
  HMODULE module = nullptr; char module_name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)er->ExceptionAddress, &module))
    GetModuleFileNameA(module, module_name, MAX_PATH);
  const uintptr_t offset = (uintptr_t)er->ExceptionAddress - (uintptr_t)module;
  char head[512];
  std::snprintf(head, sizeof head, "CRASH: exception %08lX at %p (%s+0x%llX), version %s", er->ExceptionCode, er->ExceptionAddress,
                std::strrchr(module_name, '\\') ? std::strrchr(module_name, '\\') + 1 : module_name, (unsigned long long)offset, MELEE_PORT_VERSION);
  host::log("%s", head);
  char said[400];
  cxx_exception_text(er, said, sizeof said);
  if (said[0]) host::log("exception text: %s", said);
  if (FILE* f = std::fopen("melee_port_crash.txt", "w")) {
    std::fprintf(f, "%s\n", head);
    if (said[0]) std::fprintf(f, "exception text: %s\n", said);
    crash_stack(info->ContextRecord, f);
    host::log_flush();
    if (host::cpu) {
      std::fprintf(f, "last guest function %08X %s, lr %08X\nrecent guest functions (oldest first):\n", host::cpu->last_pc, host::symbol_name(host::cpu->last_pc), host::cpu->lr);
      for (uint32_t i = 0; i < 64; ++i) { uint32_t pc = host::cpu->trace[(host::cpu->trace_pos + i) & 63]; if (pc) std::fprintf(f, "  %08X %s\n", pc, host::symbol_name(pc)); }
    }
    std::fclose(f);
  }
  HANDLE dump = CreateFileA("melee_port_crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, &mei, nullptr, nullptr);
    CloseHandle(dump);
  }
  if (g_crash_dialog) {
    std::string text = std::string(head) + "\n\nMelee Unlocked crashed. The launcher can send this report: it asks once you close this "
                       "message. If you started the game without the launcher, please send melee_port.log, melee_port_crash.txt and "
                       "melee_port_crash.dmp from the game folder with your bug report (https://github.com/hero88go/melee-unlocked/issues).";
    MessageBoxA(nullptr, text.c_str(), "Melee Unlocked", MB_ICONERROR | MB_OK);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// host::die (a guest fault, including one in a mod's own code, or any other fatal error) exits
// without an exception, so it writes the same report here: melee_port_crash.txt, a minidump of the
// process, and the dialog that points at the launcher's Send button.
static void die_report(const char* message) {
  char head[640];
  std::snprintf(head, sizeof head, "FATAL: %s, version %s", message, MELEE_PORT_VERSION);
  if (FILE* f = std::fopen("melee_port_crash.txt", "w")) {
    std::fprintf(f, "%s\n", head);
    if (host::cpu) {
      std::fprintf(f, "last guest function %08X %s, lr %08X\nrecent guest functions (oldest first):\n", host::cpu->last_pc, host::symbol_name(host::cpu->last_pc), host::cpu->lr);
      for (uint32_t i = 0; i < 64; ++i) { uint32_t pc = host::cpu->trace[(host::cpu->trace_pos + i) & 63]; if (pc) std::fprintf(f, "  %08X %s\n", pc, host::symbol_name(pc)); }
    }
    std::fclose(f);
  }
  HANDLE dump = CreateFileA("melee_port_crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump != INVALID_HANDLE_VALUE) {
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, nullptr, nullptr, nullptr);
    CloseHandle(dump);
  }
  if (g_crash_dialog) {
    std::string text = std::string(head) + "\n\nMelee Unlocked stopped with an error. The launcher can send this report: it asks once you "
                       "close this message. If you started the game without the launcher, please send melee_port.log, "
                       "melee_port_crash.txt and melee_port_crash.dmp from the game folder with your bug report "
                       "(https://github.com/hero88go/melee-unlocked/issues).";
    MessageBoxA(nullptr, text.c_str(), "Melee Unlocked", MB_ICONERROR | MB_OK);
  }
}

// MELEE_TEST_CRASH=<frame>: a deliberate crash during that frame, for the crash report gates. Only a
// hidden or headless run arms it (players never start one). It is an ordinary access violation, so
// melee_port_crash.txt, the minidump and the log line come from crash_filter exactly as for a real
// crash. It fires on a thread of its own, which leaves the frame loop untouched; in a --fast run it
// can land a frame or two late, and the log line names the frame it hit.
static void arm_test_crash(bool automated) {
  const char* text = std::getenv("MELEE_TEST_CRASH");
  if (!text || !*text) return;
  char* end = nullptr;
  const unsigned long frame = std::strtoul(text, &end, 10);
  if (!automated || !end || *end || frame == 0) {
    host::log("test: MELEE_TEST_CRASH ignored (%s)", automated ? "not a frame number" : "hidden runs only");
    return;
  }
  host::log("test: crash armed for frame %lu", frame);
  std::thread([frame] {
    // The Source Port counts profiler frames, the Static Recomp retraces: whichever moves. The armed
    // line is written again once the game runs, since the Static Recomp opens its log after this point.
    const auto now = [] { return std::max(host::profiler_frame_id(), host::retrace_count()); };
    while (now() < 1) Sleep(1);
    host::log("test: crash armed for frame %lu", frame);
    while (now() < frame) Sleep(1);
    host::log("test: forced crash at frame %u", now());
    // MELEE_TEST_CRASH_KIND=throw: an unhandled C++ exception instead, for the report's text line.
    if (const char* kind = std::getenv("MELEE_TEST_CRASH_KIND"); kind && std::strcmp(kind, "throw") == 0)
      throw std::runtime_error("test exception text");
    volatile int* volatile target = nullptr;
    *target = 1;
  }).detach();
}

// Records the disc this run used, next to the launcher's own settings. However the game was
// started (a batch file, a shortcut, the launcher, a development command line), the launcher can
// then offer that disc instead of leaving Play greyed out with an empty box.
static void remember_iso(const std::string& iso) {
  char full[MAX_PATH];
  if (!GetFullPathNameA(iso.c_str(), MAX_PATH, full, nullptr)) return;
  char* local = nullptr; size_t n = 0;
  if (_dupenv_s(&local, &n, "LOCALAPPDATA") != 0 || !local) return;
  std::string dir = std::string(local) + "\\MeleeUnlocked";
  free(local);
  CreateDirectoryA(dir.c_str(), nullptr);
  FILE* f = std::fopen((dir + "\\launcher.ini").c_str(), "w");
  if (!f) return;
  std::fprintf(f, "iso=%s\n", full);
  std::fclose(f);
}

// The runtime and the translated game are built for AVX2. Without it the first AVX2 instruction
// kills the process before anything is logged, so say so plainly instead (this file is not AVX2).
static bool cpu_has_avx2() {
  int r[4];
  __cpuid(r, 0); if (r[0] < 7) return false;
  __cpuid(r, 1); const bool osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
  if (!osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
  __cpuidex(r, 7, 0); return (r[1] >> 5) & 1;
}

// ...and the check in melee_main is too late to catch it. The runtime and guest libraries are built
// for AVX2, and their C++ static initialisers run before main does, so a CPU without AVX2 died
// during CRT startup: exit code 0xC000001D, no log file written, and the message below never shown.
// A player reported exactly that, and the empty folder they were asked to find the log in is what
// gave it away.
//
// The initialiser slots run in this order: XCC (compiler), XCL (library), XCU (user). Every static
// constructor in the runtime and guest libraries is XCU, so XCC is ahead of all of them and is
// still after the C runtime has set itself up.
//
// The first attempt used .CRT$XIB, which is a slot the CRT uses for its own early initialisation,
// and it ran before the CRT was ready. That is a plausible way to get a wrong answer out of a
// check that is correct everywhere else, which is why the message below prints what the processor
// actually reported rather than only the conclusion.
static int __cdecl check_avx2_before_anything_else() {
#ifndef MELEE_NEEDS_AVX2
  return 0;   // built for an older baseline: nothing here to require
#else
  if (cpu_has_avx2()) return 0;
  // Say which processor and which of the four conditions failed. A player who is told "your CPU is
  // too old" and believes otherwise has no way to settle it, and neither do we: this makes the
  // screenshot itself the answer, instead of a round of guessing about what machine it is.
  char brand[64] = "unknown";
  int r[4];
  __cpuid(r, 0x80000000);
  if ((unsigned)r[0] >= 0x80000004u) {
    for (int i = 0; i < 3; ++i) { __cpuid(r, 0x80000002 + i); std::memcpy(brand + i * 16, r, 16); }
    brand[48] = '\0';
  }
  __cpuid(r, 0);
  const int max_leaf = r[0];
  int leaf1[4] = {};
  if (max_leaf >= 1) __cpuid(leaf1, 1);
  const bool osxsave = (leaf1[2] >> 27) & 1, avx = (leaf1[2] >> 28) & 1;
  const unsigned long long xcr0 = osxsave ? _xgetbv(0) : 0;
  int leaf7[4] = {};
  if (max_leaf >= 7) __cpuidex(leaf7, 7, 0);
  const bool avx2 = max_leaf >= 7 && ((leaf7[1] >> 5) & 1);

  // wsprintfA rather than snprintf: this runs from a CRT initialiser slot, before the C runtime
  // has finished setting itself up, and wsprintfA lives in user32 with no such dependency. The
  // last thing this should do is fault inside the code explaining a fault.
  char msg[768];
  wsprintfA(msg,
                "Melee Unlocked needs a processor with AVX2, and this one reports that it does not "
                "have it.\n\nAVX2 means Intel Core 4th generation (Haswell, 2013) or newer, or AMD "
                "Ryzen or newer. Every version of Melee Unlocked has been built for it; older "
                "versions crashed here without a message instead of showing this one.\n\n"
                "Processor: %s\n"
                "AVX: %s   AVX2: %s   OS support: %s\n\n"
                "If you believe this is wrong, send this window to the developer: these four values "
                "say exactly what the processor reported.",
                brand, avx ? "yes" : "no", avx2 ? "yes" : "no",
                (osxsave && (xcr0 & 6) == 6) ? "yes" : "no");
  MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
  ExitProcess(3);
  return 0;
#endif
}
#pragma section(".CRT$XCC", long, read)
__declspec(allocate(".CRT$XCC")) static int (__cdecl* g_avx2_guard)() = check_avx2_before_anything_else;

// Built for the Windows subsystem so double-clicking the game does not open a terminal alongside it.
// Anything started from a command line still prints there: this reattaches to the parent console when
// one exists, so `melee_port.exe --help` and scripted runs behave exactly as before.
static void attach_parent_console() {
  // Never take over output that is already going somewhere. A script running `--version` hands us a
  // pipe, and reopening CONOUT$ over it sends the answer to the terminal instead of back to the
  // caller, which is how this first broke the release packaging.
  const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
  if (existing && existing != INVALID_HANDLE_VALUE && GetFileType(existing) != FILE_TYPE_UNKNOWN) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE* f = nullptr;
  freopen_s(&f, "CONOUT$", "w", stdout);
  freopen_s(&f, "CONOUT$", "w", stderr);
  freopen_s(&f, "CONIN$", "r", stdin);
}

static int melee_main(int argc, char** argv);

// Both entry points exist so the executable links whichever subsystem it is built for: WinMain for the
// windowed build (no terminal alongside the game), main if it is ever built as a console program.
// A C++ exception that nothing handles (a file name the system cannot convert, a damaged file a
// parser gives up on) used to end the game with a bare exception code. It is reported with its own
// text instead, so the crash report says what failed.
[[noreturn]] static void report_unhandled() {
  try {
    if (std::exception_ptr error = std::current_exception()) std::rethrow_exception(error);
  } catch (const std::exception& error) {
    host::die("the game stopped on an internal error: %s", error.what());
  } catch (...) {
    host::die("the game stopped on an internal error of an unknown kind");
  }
  host::die("the game stopped itself (abort)");
}
static int guarded_main(int argc, char** argv) {
  // The same for an exception that escapes any other thread: the C++ runtime ends the program
  // there (terminate, then abort) without passing through the crash report. The terminate handler
  // is per thread, the abort signal is not, so the report is written from both.
  std::set_terminate([] { report_unhandled(); });
  std::signal(SIGABRT, [](int) { report_unhandled(); });
  try {
    return melee_main(argc, argv);
  } catch (const std::exception& error) {
    host::die("the game stopped on an internal error: %s", error.what());
  }
}
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  attach_parent_console();
  return guarded_main(__argc, __argv);
}
int main(int argc, char** argv) { return guarded_main(argc, argv); }

static int melee_main(int argc, char** argv) {
  if (!cpu_has_avx2()) {
    const char* msg = "Melee Unlocked needs a CPU with AVX2 (Intel Haswell 2013 or newer, AMD Ryzen or newer). This CPU does not support it.";
    std::fprintf(stderr, "%s\n", msg);
    MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
    return 3;
  }
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--version") { std::printf("%s\n", MELEE_PORT_VERSION); return 0; }
#ifdef MELEE_SOURCE_PORT
  if (!source_port::reserve_memory()) return 1;
#endif
  TimerResolution timer_resolution;
  // This thread runs the game (and, without --threaded-renderer, draws it). At normal priority any
  // busy program on the PC (a compile, a browser tab, an encoder) takes turns with it, and one lost
  // turn is a late 60 Hz tick: replays on a loaded PC went from ~400 ticks over 16.7 ms to under 5
  // with the game above normal. The tick sleeps most of each frame, so the rest of the PC keeps
  // the remaining time; the render thread and the audio thread are raised where they start.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  // Windows power throttling (EcoQoS) off for this process: on CPUs with performance and efficiency
  // cores, Windows can move a throttled process onto the efficiency cores, and the game then runs
  // slow for as long as it stays there. The game sleeps most of each frame, so this costs no power
  // while it waits. Ignored by Windows versions without the setting.
  {
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;   // execution speed never throttled
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof throttling);
  }
  host::Options& o = host::options;
  bool headless = false, hidden = false, threaded = false, fps_requested = false;
  bool scripted = false, allow_matchmaking = false;   // automated runs stay off Slippi's servers
  std::string card_self_test_dir;
  std::string replay_arg;   // Source engine --replay
  std::string cosmetic_import, cosmetic_scan_disc, cosmetic_select;
  bool cosmetic_enable_effects = false, cosmetic_restore = false, cosmetic_status = false;
  gx::RenderOptions gfx;
#ifdef MELEE_SOURCE_PORT
  gfx.native_source = true;
#endif
  bool automated = false, explicit_frame_mode = false, settings_window_only = false, load_settings = false;
  bool explicit_card_dir = false;
  unsigned long settings_standby = 0;   // the launcher's process id: start hidden, show when it asks
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--hidden" || arg == "--headless") automated = true;
    if (arg == "--settings-path" && i+1 < argc) gfx.settings_path = argv[++i];
    // Loading the settings logs, and the log file opens on its first line: without this the lines
    // before the full parse went to melee_port.log and so did the rest of the run.
    else if (arg == "--log-file" && i+1 < argc) host::options.log_file = argv[++i];
    if (arg == "--frame-mode") explicit_frame_mode = true;
    if (arg == "--settings-window") settings_window_only = true;
    if (arg == "--settings-standby" && i+1 < argc) settings_standby = std::strtoul(argv[++i], nullptr, 10);
    // --settings-tab <name>: the tab the panel opens on (the launcher's "Choose skins" asks for mods).
    if (arg == "--settings-tab" && i+1 < argc) {
      static const char* const tabs[] = {"video", "audio", "game", "controls", "overlays", "customize", "gecko", "mods"};
      const std::string wanted = argv[++i];
      for (int t = 0; t < 8; ++t) if (wanted == tabs[t]) gx::settings_set_initial_tab(t);
    }
    if (arg == "--load-settings") load_settings = true;
    if (arg == "--card-dir") explicit_card_dir = true;
  }
  gfx.pc_settings = !automated;
  g_crash_dialog = !automated;
  o.no_gc_adapter = automated;   // a hidden test run must not take the adapter from a game the player is running
  if (std::getenv("MELEE_NO_GC_ADAPTER")) o.no_gc_adapter = true;   // same, for a visible test window
  SetUnhandledExceptionFilter(crash_filter);
  host::set_die_hook(die_report);
  if (!automated) {
    // Interpolate by default: it never overshoots a stop, so menus, cursors and stage geometry stay
    // on one timeline. Predict avoids its one tick of delay but can overshoot and snap back.
    // Set before the settings file is read, so a saved "subframe" (Off in the Low spec preset) wins
    // over this default and an explicit --frame-mode, parsed below, still wins over both.
    if (!explicit_frame_mode) gfx.subframe = gx::SubFrameMode::Authored;   // Predict (the default since 0.5.5)
    // A person launching the game wants to hear it. The zero default is there for automated runs,
    // which never reach this branch, and it used to be hidden by the launcher passing --volume 70 on
    // every start; that override was removed because it also overwrote the player's saved settings,
    // which left anyone without a saved volume silent. Set before the file is read, so a saved
    // volume still wins, and an explicit --volume below wins over both.
    o.volume = 70;
    // Unlocked is the whole point of the port, so a fresh install must not present at 60. The
    // struct default is 60 for automated runs, which never reach this branch. The launcher used to
    // pass --fps unlocked on every start, which also overrode what the player had saved (0.2.1 and
    // 0.2.2), so it was removed from the launcher; without a default here a new player got a 60 Hz
    // build with sub-frame animation on and no way to tell why it felt wrong. Set before the
    // settings file is read, so a saved cap (60 in the Low spec preset) still wins, and an explicit
    // --fps below wins over both.
    //
    // Monitor rate, not uncapped. 0.3.0 defaulted this to uncapped, which renders as fast as the
    // hardware can and does it hardest on the menus, where there is almost nothing to draw: a
    // player reported the fans winding up to a jet engine a minute after reaching the main menu and
    // settling once a match loaded. Frames beyond the refresh rate are never shown, so all of that
    // heat and power bought nothing, and free-running also paces worse than following the display.
    // Following the monitor is still unlocked in the sense that matters: a 144 Hz display gets 144.
    gfx.fps_cap = -1;   // -1 = follow the monitor, 0 = uncapped
    gx::load_pc_settings(gfx, o.volume);
    source_port::mods::set_auto_detect(true);
    // Opt-in, and only ever from a saved setting: an automated or headless run never gets here, so
    // it can never publish. With the setting off no thread is started and no pipe is opened.
    if (gfx.discord_presence) { host::discord::configure(gfx.discord_app_id); host::discord::enable(true); }
    threaded = true;
  } else if (load_settings) {
    // Test runs of the settings themselves (tools/m3_option_runs.py): read --settings-path the way a
    // normal launch does, before the flags below, so an explicit flag still wins. The panel stays off
    // and Discord is never started from an automated run.
    gx::load_pc_settings(gfx, o.volume);
    gfx.settings_open = false;
  }
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
    if (a == "--iso") o.iso = next();
    else if (a == "--mod-base-iso") o.mod_base_iso = next();   // Static Recomp: --iso is a mod disc, this is the vanilla one
    else if (a == "--state-trace") o.state_trace = next();
    else if (a == "--state-digest") o.state_digest = next();
    else if (a == "--frames") o.frames = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--fast") o.fast = true;
    else if (a == "--headless") headless = true;
    else if (a == "--hidden") hidden = true;
    else if (a == "--threaded-renderer") threaded = true;
    else if (a == "--fps") {   // display rate: N or "unlocked"; enables the render thread
      std::string v = next(); gfx.fps_cap = v == "unlocked" ? 0 : v == "monitor" ? -1 : std::atoi(v.c_str());
      if (v != "unlocked" && v != "monitor" && (gfx.fps_cap < 1 || v.find_first_not_of("0123456789") != std::string::npos)) { usage(); return 2; }
      threaded = true;
      fps_requested = true;
    }
    else if (a == "--frame-mode") {
      std::string v = next();
      if (v == "extrapolate") gfx.subframe = gx::SubFrameMode::Extrapolate;
      else if (v == "interpolate") gfx.subframe = gx::SubFrameMode::Interpolate;
      else if (v == "authored") gfx.subframe = gx::SubFrameMode::Authored;
      else if (v == "authored-interpolate") gfx.subframe = gx::SubFrameMode::AuthoredInterpolate;
      else if (v == "off") gfx.subframe = gx::SubFrameMode::Off;
      else { usage(); return 2; }
      threaded = true;
    }
    else if (a == "--scale") { std::string v = next(); gfx.efb_scale = v == "auto" ? 0 : std::atoi(v.c_str()); if (v != "auto" && gfx.efb_scale < 1) { usage(); return 2; } }
    else if (a == "--window") { if (std::sscanf(next(), "%dx%d", &gfx.window_w, &gfx.window_h) != 2 || gfx.window_w < 320 || gfx.window_h < 240) { usage(); return 2; } gfx.window_pinned = true; }
    // Presentation only, so it cannot desync and the two players in a match may differ.
    else if (a == "--aspect") { std::string v = next();
      gfx.aspect = v == "auto" ? gx::AspectMode::Auto : v == "73:60" || v == "native" ? gx::AspectMode::Native
                 : v == "4:3" ? gx::AspectMode::Force4_3 : v == "16:9" ? gx::AspectMode::Force16_9
                 : v == "stretch" ? gx::AspectMode::Stretch : (gx::AspectMode)-1;
      if ((int)gfx.aspect < 0) { std::fprintf(stderr, "--aspect auto|73:60|4:3|16:9|stretch\n"); return 2; } }
    else if (a == "--settings-path") gfx.settings_path = next();
    else if (a == "--import-cosmetic") cosmetic_import = next();
    // Hidden tests: list a mod disc's costumes as skins (needs --iso to compare against), and pick
    // one by the id --cosmetic-status prints. Both exit without starting the game.
    else if (a == "--scan-disc-skins") cosmetic_scan_disc = next();
    else if (a == "--select-cosmetic") cosmetic_select = next();
    else if (a == "--enable-project-effects") cosmetic_enable_effects = true;
    else if (a == "--restore-vanilla-cosmetics") cosmetic_restore = true;
    else if (a == "--cosmetic-status") cosmetic_status = true;
    else if (a == "--pc-settings-open") { gfx.pc_settings = true; gfx.settings_open = true; }
    else if (a == "--load-settings") {}
    // The overlay layer without the panel. An automated run turns the UI off entirely, which also
    // takes the on-screen overlays with it; this brings them back without capturing the pad, so a
    // scripted run can screenshot an overlay.
    else if (a == "--pc-settings") gfx.pc_settings = true;
    else if (a == "--fullscreen") gfx.fullscreen = true;
    else if (a == "--backend") { std::string v = next();
      if (v == "d3d11" || v == "dx11" || v == "11") gfx.api = gx::RenderApi::D3D11;
      else if (v == "d3d12" || v == "dx12" || v == "12") gfx.api = gx::RenderApi::D3D12;
      else { std::fprintf(stderr, "--backend d3d12|d3d11\n"); return 2; } }
#ifdef GX_DLSS5
    else if (a == "--dlss5") gfx.dlss5 = true;                  // EXPERIMENTAL (gx_dlss5.h); needs --dlss
#endif
    else if (a == "--dlss") { std::string v = next(); gfx.dlss_mode = v == "off" ? 0 : v == "dlaa" ? 1 : v == "quality" ? 2 : v == "balanced" ? 3 : v == "performance" ? 4 : v == "ultra" ? 5 : v == "xess-aa" ? 6 : v == "xess-ultra" ? 7 : v == "xess-quality" ? 8 : v == "xess-balanced" ? 9 : v == "xess-performance" ? 10 : -1;
      if (gfx.dlss_mode < 0) { std::fprintf(stderr, "--dlss off|dlaa|quality|balanced|performance|ultra\n"); return 2; } }
    else if (a == "--frame-generation") gfx.frame_generation_mode = 1;   // backward-compatible 2x shorthand
    else if (a.rfind("--frame-generation=", 0) == 0) {
      const std::string v = a.substr(std::strlen("--frame-generation="));
      gfx.frame_generation_mode = v == "2x" ? 1 : v == "3x" ? 2 : v == "4x" ? 3 :
                                  v == "dynamic" ? 4 : v == "5x" ? 5 : v == "6x" ? 6 : -1;
      if (gfx.frame_generation_mode < 0) {
        std::fprintf(stderr, "--frame-generation=2x|3x|4x|5x|6x|dynamic\n");
        return 2;
      }
    }
    else if (a == "--dlss-mode") {
      char* end = nullptr; long mode = std::strtol(next(), &end, 10);
      if (!end || *end || mode < 0 || mode > 5) { std::fprintf(stderr, "--dlss-mode must be 0..5\n"); return 2; }
      gfx.dlss_mode = (int)mode;
    }
    else if (a == "--ssao") {
      char* end = nullptr; float amount = std::strtof(next(), &end);
      if (!end || *end || !std::isfinite(amount) || amount < 0.0f || amount > 1.0f) { std::fprintf(stderr, "--ssao must be 0..1\n"); return 2; }
      gfx.screen_space_ao = amount;
    }
    else if (a == "--reflex") gfx.reflex_mode = 2;
    else if (a == "--dlss-jitter-sign") gfx.dlss_jitter_sign = (float)std::atof(next());
    else if (a == "--frame-times") gfx.frame_times = next();
    else if (a == "--vsync") gfx.vsync = true;
    else if (a == "--flicker-scan") gfx.flicker_scan = true;
    else if (a == "--pin-phase") gfx.pin_phase = std::atof(next());
    else if (a == "--capture") gfx.capture_path = next();
    else if (a == "--capture-frame") gfx.capture_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-every") gfx.capture_every = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-burst") gfx.capture_burst = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-sim-frame") gfx.capture_sim_frame = std::strtoull(next(), nullptr, 0);
    else if (a == "--script") { if (!host::input_load_script(next())) { std::fprintf(stderr, "cannot load input script\n"); return 1; } scripted = true; }
    else if (a == "--allow-matchmaking") allow_matchmaking = true;
    else if (a == "--dump") gfx.dump_path = next();
    else if (a == "--shader-cache") gfx.shader_cache = next();
    else if (a == "--trace-func") { const char* spec = next(); uint32_t addr = (uint32_t)std::strtoul(spec, nullptr, 16); uint32_t limit = 40;
      if (const char* colon = std::strchr(spec, ':')) limit = (uint32_t)std::strtoul(colon + 1, nullptr, 10);
      if (!addr) { std::string name(spec, std::strchr(spec, ':') ? std::strchr(spec, ':') - spec : std::strlen(spec));
        for (size_t i = 0; i < guest::name_table_count; ++i) if (name == guest::name_table[i].name) { addr = guest::name_table[i].addr; break; } }
      if (!addr) { std::fprintf(stderr, "unknown function %s\n", spec); return 2; }
      ppc::add_trace_func(addr, limit); }
    else if (a == "--sys-dir") o.sys_dir = next();
    else if (a == "--replay-dir") o.replay_dir = next();
    else if (a == "--lab-dir") o.lab_dir = next();
    else if (a == "--lab-view") gfx.lab_view = true;
    else if (a == "--card-dir") o.card_dir = next();
    // Explicit test opt-in; ordinary hidden replay and online runs never scan Mods.
    else if (a == "--scan-mods") source_port::mods::set_auto_detect(true);
    else if (a == "--log-file") o.log_file = next();
#ifdef MELEE_SOURCE_PORT
    else if (a == "--replay") replay_arg = next();   // the native game plays this .slp (loaded after the flags)
#else
    else if (a == "--replay") slippi::playback::set_replay(next());   // playback build: play this .slp
#endif
    // Replay viewing, both engines: show the session as the player had it, from the trace file an
    // online match leaves beside its replay. --trace names the file; --as-experienced looks for
    // "<replay>.trace" beside the replay. Without a usable trace the replay plays normally.
    else if (a == "--trace") replay_control::set_trace_file(next());
    else if (a == "--as-experienced") replay_control::set_as_experienced(true);
    else if (a == "--user-dir") slippi::online::config().user_dir = next();
    else if (a == "--lobby-direct") {
      std::string code = next();
      const auto hash = code.find('#');
      bool valid = hash > 0 && hash < code.size() - 1 && code.size() <= 18;
      for (size_t i = 0; i < code.size(); ++i)
        if (i != hash && !((code[i] >= '0' && code[i] <= '9') || (i < hash && code[i] >= 'A' && code[i] <= 'Z'))) valid = false;
      if (!valid) { std::fprintf(stderr, "Invalid lobby Direct code\n"); return 2; }
      slippi::online::config().lobby_code = code;
    }
    else if (a == "--lobby-character") {
      const std::string value = next(); char* end = nullptr;
      long character = std::strtol(value.c_str(), &end, 10);
      if (value.empty() || *end || character < 0 || character > 255) { std::fprintf(stderr, "Invalid lobby character\n"); return 2; }
      slippi::online::config().lobby_character = (int)character;
    }
    else if (a == "--lobby-status-file") slippi::online::config().lobby_status_file = next();
    else if (a == "--online-delay") slippi::online::config().delay = std::atoi(next());
    else if (a == "--chat") { std::string v = next(); slippi::online::config().chat = v == "off" ? 2 : v == "direct" ? 1 : 0; }
    else if (a == "--netplay-port") slippi::Matchmaking::forced_port = (uint16_t)std::atoi(next());
    else if (a == "--local-peer") {
      // idx:local_port:ip:port[,ip:port[,ip:port]]: 2-4 instances peer directly without the
      // matchmaking server. The remotes are every other player in player-index order.
      std::string v = next(); auto& lp = slippi::Matchmaking::local_peer;
      size_t a1 = v.find(':'), a2 = a1 == std::string::npos ? a1 : v.find(':', a1 + 1);
      bool ok = a1 != std::string::npos && a2 != std::string::npos;
      lp.remotes.clear();
      if (ok) {
        lp.local_index = std::atoi(v.substr(0, a1).c_str()); lp.local_port = (uint16_t)std::atoi(v.substr(a1 + 1, a2 - a1 - 1).c_str());
        for (size_t at = a2 + 1; ok && at <= v.size();) {
          size_t comma = v.find(',', at);
          std::string r = v.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
          size_t c = r.find(':');
          ok = c != std::string::npos && c > 0 && std::atoi(r.substr(c + 1).c_str()) > 0;
          lp.remotes.push_back(r);
          if (comma == std::string::npos) break;
          at = comma + 1;
        }
      }
      ok = ok && lp.local_port > 0 && !lp.remotes.empty() && lp.remotes.size() <= 3 && lp.local_index >= 0 && lp.local_index <= (int)lp.remotes.size();
      if (!ok) { std::fprintf(stderr, "--local-peer idx:port:ip:port[,ip:port[,ip:port]] (2-4 players, idx < players)\n"); return 2; }
      lp.enabled = true; }
    else if (a == "--test-stage") {
      auto& lp = slippi::Matchmaking::local_peer;
      const int stage = std::atoi(next());
      if (stage < 0 || stage > 0xFFFF) { std::fprintf(stderr, "--test-stage requires a legal Slippi stage id\n"); return 2; }
      lp.test_stage = stage;
    }
    else if (a == "--dump-frame") gfx.dump_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--trace-calls") o.trace_calls = true;
    else if (a == "--quiet") o.quiet = true;
    else if (a == "--time-base") o.time_base = std::strtoull(next(), nullptr, 0);
    // Forces HSD_RandSeedPtr at the first GS_VS frame, after scene setup and before match
    // simulation, so a script that reaches that point in both builds sees identical RNG,
    // regardless of how each build's boot timing seeded OSGetTick(). Off by default (0 is a
    // valid seed too, so use rng_seed_set rather than a zero check).
    else if (a == "--rng-seed") { o.rng_seed = (uint32_t)std::strtoul(next(), nullptr, 0); o.rng_seed_set = true; }
    else if (a == "--volume") o.volume = std::atoi(next());
    else if (a == "--music") slippi::jukebox::set_user_volume(std::clamp(std::atoi(next()), 0, 100));
    else if (a == "--no-music") slippi::jukebox::set_user_volume(0);
    else if (a == "--widescreen") { gfx.widescreen = true; gfx.true_widescreen = false; }
    else if (a == "--pal-stock-icons") gecko::option_pal_stock_icons = true;
    else if (a == "--language") {   // game | japanese | english: the Game tab's language choice
      const std::string value = next();
      host::g_game_language.store(value == "japanese" ? 1 : value == "english" ? 2 : 0);
    }
    else if (a == "--no-screen-shake") gecko::option_no_screen_shake = true;
    else if (a == "--vanilla-game") o.vanilla_game = true;
    else if (a == "--gecko-codes") { user_gecko::set_code_patches_allowed(!gfx.native_source); user_gecko::load(next(), {}, false); }   // scripted runs: that file's enabled codes
    // Experimental true 16:9: widens the frustum in the renderer, no game code. Mutually exclusive
    // with --widescreen, so whichever comes last on the command line wins rather than both applying.
    else if (a == "--true-widescreen") { gfx.true_widescreen = true; gfx.widescreen = false; }
    // Texture packs are a settings-panel feature; these exist so automated runs, which start with
    // the panel disabled, can exercise the same paths.
    else if (a == "--custom-textures") gfx.custom_textures = true;
    else if (a == "--dump-textures") gfx.dump_textures = true;
    else if (a == "--sharpness") gfx.sharpness = std::clamp((float)std::atof(next()), 0.0f, 1.0f);
    else if (a == "--ssaa") gfx.ssaa = std::atoi(next()) >= 2 ? 2 : 1;
    else if (a == "--anisotropy") gfx.anisotropy = std::clamp(std::atoi(next()), 1, 16);
    // Hidden runs never load the settings file, so the quality level needs a flag to be measurable.
    else if (a == "--effects") gfx.effects_level = std::clamp(std::atoi(next()), 0, 2);
    else if (a == "--hang-watch") o.hang_watch = std::atof(next());
    else if (a == "--audio-dump") o.audio_dump = next();
    else if (a == "--profile") g_profile = true;
    else if (a == "--input-log") o.input_log = next();
    // L-cancel helpers, both off unless asked for. --lcancel-log writes a per-frame CSV of the
    // local fighters' action state and trigger timer, which is how the landing lag is measured.
    else if (a == "--auto-lcancel") lcancel::set_automatic(true);
    else if (a == "--lcancel-indicator") lcancel::set_indicator(true);
    else if (a == "--lcancel-log") lcancel::set_log_path(next());
    else if (a == "--load-settings") {}   // handled before this loop
    else if (a == "--music-volume") slippi::jukebox::set_user_volume(std::atoi(next()));   // the Music slider, for scripted runs
    else if (a == "--profile-render") { g_profile = true; g_profiler.render_thread = true; }
    // Recognised in the pre-scan above; listed here so it is not rejected as unknown.
#ifdef MELEE_SOURCE_PORT
    else if (a == "--list-audio-devices") {   // endpoint id and name per line, for the launcher and support
      for (const auto& d : host::audio_list_devices()) std::printf("%s\t%s\n", d.id.c_str(), d.name.c_str());
      return 0;
    }
    else if (a == "--match") { if (!source_port::set_match(next())) {
      std::fprintf(stderr, "--match <stage>:<p1>[:<p2>...], each player <kind>[/c<level>][/x<costume>]\n"); return 2; } }
    else if (a == "--card-self-test") card_self_test_dir = next();
    else if (a == "--mod-dir") source_port::set_mod_directory(next());
    else if (a == "--mod-iso") source_port::set_mod_iso(next());
    else if (a == "--mod-gci") source_port::set_mod_gci(next());
    else if (a == "--te-options") gx::RenderOptions::live_te_options() = (uint32_t)std::strtoul(next(), nullptr, 16);
    else if (a == "--te-options2") gx::RenderOptions::live_te_options2() = (uint32_t)std::strtoul(next(), nullptr, 16);
    else if (a == "--cpu-training") gx::RenderOptions::live_cpu_training() = (uint32_t)std::strtoul(next(), nullptr, 16);   // option word 3 (hex), tests
    else if (a == "--mod-profile") { if (!source_port::set_mod_profile(next())) return 2; }
    else if (a == "--mod-opponent-unverified") gx::RenderOptions::live_mods_dolphin_ok() = true;
    else if (a == "--record-native") _putenv_s("MELEE_SOURCE_RECORD", "1");
    else if (a == "--slippi-menus") { const std::string v = next();
      if (v != "on" && v != "off") { std::fprintf(stderr, "--slippi-menus on|off\n"); return 2; }
      source_port::set_slippi_menus(v == "on"); }
    else if (a == "--online-test") { if (!source_port::set_online_test(next())) {
      std::fprintf(stderr, "--online-test direct|unranked|teams[:<character>[:<color>]] (with --local-peer)\n"); return 2; } }
#endif
    else if (a == "--settings-window") {}
    else if (a == "--settings-standby") next();
    else if (a == "--settings-tab") next();
    else { usage(); return 2; }
  }
  if ((hidden || headless || scripted) && !allow_matchmaking) slippi::Matchmaking::server_allowed = false;
  if (slippi::Matchmaking::local_peer.test_stage >= 0) {
    const int stage = slippi::Matchmaking::local_peer.test_stage;
    // A mod disc (--mod-base-iso) has its own stages: any id, so a mod's stage can be tested.
    const bool legal = stage == 0x2 || stage == 0x3 || stage == 0x8 || stage == 0x1C || stage == 0x1F || stage == 0x20 ||
                       !o.mod_base_iso.empty();
    if (!slippi::Matchmaking::local_peer.enabled || !legal) {
      std::fprintf(stderr, "--test-stage requires --local-peer and a legal tournament stage id (2, 3, 8, 28, 31, or 32; any on a mod disc)\n");
      return 2;
    }
  }
  // The Static Recomp runs Slippi's code from its code table; the Source Port game reads the same
  // flag (MU_GAME_OPTION_WIDESCREEN) and runs its native version of the code.
  gecko::option_widescreen = gfx.widescreen;
  if (gfx.widescreen) std::fprintf(stderr, "widescreen: Slippi 16:9 on\n");
  gecko::option_lagless_fod = !gfx.fod_reflections; // the Gecko flag is the inverse of the UI label
  if (fps_requested && gfx.subframe == gx::SubFrameMode::Off) {
    std::fprintf(stderr, "--fps requires explicit experimental --frame-mode interpolate, extrapolate or authored\n");
    return 2;
  }
  // Cosmetic catalog/profile storage follows --settings-path just like controller and video
  // settings. Configure it for normal, standalone-settings, and automated diagnostic launches.
  host::cosmetics::configure(gfx.settings_path);
  if (!cosmetic_import.empty() || !cosmetic_scan_disc.empty() || !cosmetic_select.empty() ||
      cosmetic_enable_effects || cosmetic_restore || cosmetic_status) {
    bool ok = true;
    if (!cosmetic_import.empty()) {
      auto result = host::cosmetics::import_file(cosmetic_import);
      std::printf("%s\n", result.message.c_str()); ok &= result.ok;
    }
    if (!cosmetic_scan_disc.empty()) {
      // A costume is a skin only where it differs from the game disc's, so that disc must be open.
      if (o.iso.empty()) {
        std::fprintf(stderr, "--scan-disc-skins requires --iso (your Melee disc) to compare the mod disc against\n");
        ok = false;
      } else if (!host::disc_open(o.iso)) {
        std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str());
        ok = false;
      } else {
        const std::string name = std::filesystem::u8path(cosmetic_scan_disc).stem().u8string() + " disc";
        auto result = host::cosmetics::scan_disc_skins(cosmetic_scan_disc, name);
        std::printf("%s\n", result.message.c_str()); ok &= result.ok;
      }
    }
    if (!cosmetic_select.empty()) {
      std::string error = "no skin has that id";
      bool selected = false;
      for (const auto& asset : host::cosmetics::assets())
        if (asset.id == cosmetic_select) {
          const std::string slot = asset.target_path + (asset.scope.empty() ? "" : "#" + asset.scope);
          selected = host::cosmetics::select_variant(slot, asset.id, &error);
          break;
        }
      if (selected) std::printf("%s\n", host::cosmetics::last_message().c_str());
      else { std::fprintf(stderr, "%s\n", error.c_str()); ok = false; }
    }
    if (cosmetic_enable_effects) {
      std::string error;
      if (o.iso.empty()) {
        std::fprintf(stderr, "--enable-project-effects requires --iso so every target can be validated against the clean disc\n");
        ok = false;
      } else if (!host::disc_open(o.iso)) {
        std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str());
        ok = false;
      } else if (!host::cosmetics::enable_project_effects(&error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        ok = false;
      } else {
        std::printf("%s\n", host::cosmetics::last_message().c_str());
      }
    }
    if (cosmetic_restore) {
      std::string error;
      if (!host::cosmetics::restore_vanilla(&error)) { std::fprintf(stderr, "%s\n", error.c_str()); ok = false; }
      else std::printf("%s\n", host::cosmetics::last_message().c_str());
    }
    if (cosmetic_status) {
      std::printf("profile %s\n", host::cosmetics::profile_enabled() ? "enabled" : "disabled");
      for (const auto& asset : host::cosmetics::assets())
        std::printf("%c %s | %s | %s | %s%s%s\n", asset.selected ? '*' : '-', asset.target_path.c_str(),
                    asset.name.c_str(), asset.id.c_str(), asset.sha256.c_str(),
                    asset.voice.empty() ? "" : " | voice ", asset.voice.c_str());
    }
    return ok ? 0 : 1;
  }

  // The settings panel with no game behind it: the launcher opens this instead of booting the
  // whole game to change a setting. Before the disc check, because it needs no disc.
  if (settings_window_only) {
    gfx.pc_settings = true;
    gx::load_pc_settings(gfx, o.volume);
    const int rc = app::run_settings_window(gfx, settings_standby, [&] { gx::load_pc_settings(gfx, o.volume); gfx.pc_settings = true; });
    // The panel polls every controller so its live readouts work, which starts the adapter and
    // Switch Pro threads, and it can start an update check. Returning straight from here left those
    // threads running into static destruction, where a joinable std::thread ends the process: every
    // close of this window was a crash (0xC0000409), and with Windows Error Reporting collecting it,
    // the window took a long time to go away. Same shutdown as the game's, below.
    host::updater::shutdown();
    host::discord::shutdown();
    host::gcadapter_shutdown();
    host::switchpro_shutdown();
    slippi::shutdown();
    return rc;
  }
#ifdef MELEE_SOURCE_PORT
  if (!card_self_test_dir.empty()) return source_port::card_self_test(card_self_test_dir.c_str()) ? 0 : 1;
  // The profile picked in the settings panel, unless the command line gave its own mod layers. A
  // profile that no longer loads starts the retail game rather than refusing to start.
  if (gx::RenderOptions::kModFeaturesAvailable && !source_port::has_mod_layers() && !gfx.mod_profile.empty() &&
      !source_port::set_mod_profile(gfx.mod_profile.c_str()))
    host::log("mods: saved profile %s could not be loaded; starting the retail game", gfx.mod_profile.c_str());
  if (!replay_arg.empty() && !source_port::set_replay(replay_arg.c_str())) {
    std::fprintf(stderr, "cannot play replay %s\n", replay_arg.c_str());
    return 1;
  }
#endif
  if (o.iso.empty()) { usage(); return 2; }
  if (!host::disc_open(o.iso)) { std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str()); return 1; }
  remember_iso(o.iso);   // so the launcher can offer this disc without being told again
  // Controllers do not count as activity to Windows, so a session played only on a pad let the
  // display power off after the idle timeout (monitors going black mid-game until the mouse moved).
  // Held by this thread for as long as the game runs; Windows drops it when the process exits.
  if (!hidden) SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

  {
    const char* version = MELEE_PORT_VERSION;
    std::wstring base = L"Melee Unlocked ";
    for (const char* c = version; *c; ++c) base += (wchar_t)(unsigned char)*c;
#ifdef MELEE_SOURCE_PORT
    base += L"  |  Source Port";
#else
    base += L"  |  Static Recomp";
#endif
    host::window_title_base() = base;
  }
  std::unique_ptr<gx::Backend> backend;
  if (!headless && threaded) {
    backend = gx::create_threaded_backend(gfx, !hidden);
  } else if (!headless) {
    void* hwnd = host::window_create(gfx.window_w, gfx.window_h, host::window_title_base().c_str(), !hidden);
    if (gfx.fullscreen && !gfx.exclusive_fullscreen) host::window_set_fullscreen(true);
    backend.reset(gx::create_render_backend(hwnd, gfx.window_w, gfx.window_h, gfx));
    host::window_set_resize_callback([renderer = backend.get()](int w, int h) { gx::render_resize(renderer, w, h); });
    host::g_has_window = true;
  }
  // Texture packs: scan before the game starts so the settings list is right, and decode up front
  // when the player asked for that, with the wait shown in the title bar rather than as a silent
  // half minute. Decoding runs on its own thread, so the game keeps booting while it works.
  gx::texpack::configure(gfx.custom_textures, gfx.dump_textures);
  gx::texpack::refresh_packs();
  if (gfx.custom_textures && gfx.prefetch_textures) {
    gx::texpack::prefetch_begin();
    while (gx::texpack::prefetching()) {
      uint64_t done = 0, total = 0;
      gx::texpack::prefetch_progress(&done, &total);
      wchar_t title[192];
      swprintf_s(title, L"%ls  |  loading textures %llu / %llu", host::window_title_base().c_str(),
                 (unsigned long long)done, (unsigned long long)total);
      host::window_set_title(title);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    host::window_set_title(host::window_title_base().c_str());
  }
  gx::set_authored_capture(gfx.subframe == gx::SubFrameMode::Authored || gfx.subframe == gx::SubFrameMode::AuthoredInterpolate);
  host::disc_prefetch_wait();
  gx::init(backend.get());
  host::audio_set_buffering(gfx.audio_mode, gfx.audio_buffer_ms);
  host::audio_set_device(gfx.audio_device.c_str());
  host::audio_set_asio(gfx.audio_asio_driver.c_str(), gfx.audio_asio_buffer);
  // Hidden runs are test automation (players never start hidden): they use the player's Auto memory
  // but never save to it, so a loaded test machine cannot raise the player's buffer.
  host::audio_set_remember(!hidden);
  host::audio_open(o.volume, o.audio_dump.c_str(), !headless);
  arm_test_crash(automated);   // no-op unless MELEE_TEST_CRASH is set on a hidden run

#ifdef MELEE_SOURCE_PORT
  // The game from source: nothing below this point applies (it is the recompiled guest's boot).
  static std::unique_ptr<gx::Backend>* backend_at_exit = &backend;
  auto shutdown = [](int) {
    g_profiler.report();   // no-op without --profile
    backend_at_exit->reset();
    { uint64_t silent_ms = 0, underruns = host::audio_underruns(&silent_ms);
      double rate_low = 1.0, rate_high = 1.0; host::audio_rate_range(&rate_low, &rate_high);
      host::log("audio: %llu frames played, %llu blocks dropped, %llu gaps (%llu ms held), clock tracking %+.3f%% to %+.3f%%",
                (unsigned long long)host::audio_pushed_frames(), (unsigned long long)host::audio_dropped_blocks(),
                (unsigned long long)underruns, (unsigned long long)silent_ms, (rate_low - 1.0) * 100.0, (rate_high - 1.0) * 100.0); }
    host::audio_close();
    host::updater::shutdown();
    host::discord::shutdown();
    host::gcadapter_shutdown();
    host::switchpro_shutdown();
    slippi::shutdown();
    host::log_flush();   // the process ends with ExitProcess right after this (source_host.cpp guarded)
  };
  if (g_profile) g_profiler.start();   // the native game runs on this thread; shutdown() reports
  const int source_code = source_port::run(shutdown);
  shutdown(source_code);
  host::end_process(source_code);   // everything is saved: do not run the libraries' unload code
#endif
#ifndef MELEE_SOURCE_PORT
  // MELEE_TEST_AXLIST=1: at every sound start, walk the free sound-request list (head r13-0x3F10,
  // linked through +4) and report the first time a node lies outside the driver's own table.
  if (std::getenv("MELEE_TEST_AXLIST")) { ppc::add_entry_hook(0x8038CFF4u, check_ax_free_list); ppc::set_any_entry_hook(ax_watch_any); host::set_after_guest_call(ax_watch_after_callback); }
  if (std::getenv("MELEE_TRACE_EFFECTS")) {
    ppc::add_entry_hook(0x8005FDDCu, trace_ef_sync_entry);    // efSync_Spawn(gfx_id, gobj, ...)
    ppc::add_entry_hook(0x800676F0u, trace_ef_async_entry);   // efAsync_Spawn(gobj, queue, kind, gfx_id, ...)
  }
  if (std::getenv("MELEE_TRACE_AX_VOICES")) {
    ppc::add_entry_hook(0x8038CFF4u, trace_ax_request_entry);       // HSD_AudioSFXStartParam
    ppc::add_entry_hook(0x8038B5ACu, trace_ax_stream_start_entry);  // HSD_SynthPStreamStart
    ppc::add_entry_hook(0x8038B120u, trace_ax_stream_data_entry);   // HSD_SynthPStreamFirstHakoDataCallback
    ppc::add_entry_hook(0x803883B4u, trace_ax_bank_entry);          // HSD_SynthSFXSampleLoadCallback
  }
  if (const char* range = std::getenv("MELEE_TRACE_RNG")) {
    if (std::sscanf(range, "%u:%u", &g_rng_trace_first, &g_rng_trace_last) == 2 &&
        g_rng_trace_first <= g_rng_trace_last &&
        g_rng_trace_last - g_rng_trace_first <= 120) {
      ppc::add_entry_hook(0x801E43E0u, trace_onett_entry);
      ppc::add_entry_hook(0x8006A360u, trace_magnify_fighter_entry);
      ppc::add_entry_hook(0x802FBBDCu, trace_magnify_render_entry);
      ppc::add_entry_hook(0x80086B90u, trace_fighter_bone_position_entry);
      ppc::add_entry_hook(0x80368458u, trace_cobj_set_current_entry);
      ppc::add_entry_hook(0x80086B64u, trace_magnify_gate_entry);
      ppc::add_entry_hook(0x80086ED0u, trace_magnify_gate_entry);
       ppc::add_entry_hook(0x80086A8Cu, trace_camera_visibility_entry);
       ppc::add_entry_hook(0x8000E210u, trace_camera_project_entry);
       ppc::add_entry_hook(0x8002A4ACu, trace_camera_transform_entry);
       ppc::add_entry_hook(0x8002A0C0u, trace_camera_apply_quake_entry);
       ppc::add_entry_hook(0x80030DF8u, trace_camera_translation_reset_entry);
      ppc::add_entry_hook(0x8026C88Cu, trace_item_timer_entry);
      ppc::add_entry_hook(0x8026C75Cu, trace_item_pick_entry);
       ppc::add_entry_hook(0x8026CB3Cu, trace_item_position_entry);
       ppc::add_entry_hook(0x80268B18u, trace_item_create_entry);
       ppc::add_entry_hook(0x80268E5Cu, trace_item_sword_state_entry);
       ppc::add_entry_hook(0x80285338u, trace_item_sword_spawned_entry);
      for (uint32_t address : {0x8026D62Cu, 0x8026D6F4u, 0x8026D78Cu,
                               0x8026D938u, 0x8026E15Cu, 0x8026E248u,
                               0x8026E32Cu, 0x8026E414u, 0x8026E4D0u,
                               0x8026E5A0u, 0x8026E664u, 0x8026E71Cu,
                               0x8026E7E0u, 0x8026E8C4u, 0x8028FCBCu})
        ppc::add_entry_hook(address, trace_item_sword_proc_entry);
      ppc::add_entry_hook(0x80272860u, trace_item_sword_physics_entry);
      ppc::add_entry_hook(0x8026D018u, trace_item_init_entry);
      ppc::add_entry_hook(0x80088148u, trace_fighter_sfx_entry);
      ppc::add_entry_hook(0x8002411Cu, trace_ax_sfx_entry);
      ppc::add_entry_hook(0x8038CFF4u, trace_audio_start_entry);
      ppc::add_entry_hook(0x8038CC1Cu, trace_ax_driver_callback_entry);
      ppc::add_entry_hook(0x803896F0u, trace_synth_start_entry);
      ppc::add_entry_hook(0x80389334u, trace_synth_voice_entry);
      ppc::add_entry_hook(0x80071B50u, trace_fighter_sfx_command_entry);
      ppc::add_entry_hook(0x80085030u, trace_ground_friction_entry);
      ppc::add_entry_hook(0x8007C930u, trace_ground_accel_entry);
      ppc::add_entry_hook(0x8007CB74u, trace_ground_self_entry);
      ppc::add_entry_hook(0x8000D4F8u, trace_root_delta_entry);
      ppc::add_entry_hook(0x8007F694u, trace_model_scale_entry);
      ppc::add_entry_hook(0x8006E054u, trace_root_anim_entry);
      ppc::add_entry_hook(0x8036FDC0u, trace_root_anim_channel_entry);
      ppc::add_entry_hook(0x8036AE70u, trace_root_fobj_entry);
    }
  }
#endif
  ppc::init_dispatch();
#ifndef MELEE_SOURCE_PORT
  install_rng_seed_hook();
  host::install_audio_pacing();
  host::install_console_clock();
  host::install_language_override();
#endif
  if (gx::RenderOptions::kModFeaturesAvailable && source_port::mods::auto_detect() && !gfx.native_source) {
    source_port::mods::StartupOptions scan;
    scan.base_iso = std::filesystem::u8path(o.mod_base_iso.empty() ? o.iso : o.mod_base_iso);
    // An explicit card belongs to this session already. Protect the ordinary card and
    // list the packs, without replacing or modifying the session's selected card.
    if (!explicit_card_dir) scan.card_dir = std::filesystem::u8path(o.card_dir);
    scan.use_profile_card = !explicit_card_dir;
    const auto found = source_port::mods::startup(scan);
    for (const auto& line : found.log) host::log("mods: %s", line.c_str());
    // The costumes of the mod discs found become choices in the skin list of this, the normal game.
    // Not when this session's own disc is a mod disc: there is no standard costume to compare with.
    if (o.mod_base_iso.empty()) source_port::mods::scan_detected_disc_skins(std::filesystem::u8path(o.iso));
    if (!explicit_card_dir && !found.card_dir.empty()) o.card_dir = found.card_dir.u8string();
  }
#ifndef MELEE_SOURCE_PORT
  // "20XX CPUs": the 20XX Hack Pack's own AI block from its disc under Mods, applied at each offline
  // match start (the Source Port plays its native version instead).
  host::hackpack_ai::boot();
#endif
  host::boot_setup();
  {
    std::vector<gx::texpack::CosmeticCompanion> companions;
    for (const auto& item : host::cosmetics::active_companions())
      companions.push_back({item.kind, item.target_path, item.path});
    gx::texpack::set_cosmetic_companions(std::move(companions));
  }
  host::log("boot: entering __start at %08X", 0x8000522Cu);
  int code = 0;
  if (g_profile) g_profiler.start();
  try {
    ppc::call(*host::cpu, host::ram, 0x8000522Cu);
    host::log("guest returned from __start after %u retraces", host::retrace_count());
  } catch (const ExitRequested& stop) {
    backend.reset();
    code = stop.code;
  } catch (const LoadContextUnwind&) {
    host::log("OSLoadContext reached top level");
  }
  g_profiler.report();
  { uint64_t silent_ms = 0, underruns = host::audio_underruns(&silent_ms);
    double rate_low = 1.0, rate_high = 1.0; host::audio_rate_range(&rate_low, &rate_high);
    host::log("audio: %llu frames played, %llu blocks dropped, %llu gaps (%llu ms held), clock tracking %+.3f%% to %+.3f%%",
              (unsigned long long)host::audio_pushed_frames(), (unsigned long long)host::audio_dropped_blocks(),
              (unsigned long long)underruns, (unsigned long long)silent_ms, (rate_low - 1.0) * 100.0, (rate_high - 1.0) * 100.0); }
  host::audio_close();
  host::updater::shutdown();   // the settings panel may have started an update check; join it before exit
  host::discord::shutdown();   // clears the presence and joins its thread; a no-op when never enabled
  host::gcadapter_shutdown();
  host::switchpro_shutdown();   // joins the init thread and hands any Switch pad back to the system
  slippi::shutdown();
  { uint64_t calls = 0, insns = 0; ppc::interpreter_stats(&calls, &insns);
    if (calls) host::log("interpreter: %llu calls into RAM-resident code, %llu instructions", (unsigned long long)calls, (unsigned long long)insns); }
  if (ppc::g_computed_return_checks || ppc::g_resumed_returns)
    host::log("gecko: adjusted-return checks %llu, resumed %llu (UCF Shield Drop and the like)",
              (unsigned long long)ppc::g_computed_return_checks, (unsigned long long)ppc::g_resumed_returns);
  host::log("slippi: %llu EXI commands, %llu replays written, GCT at %08X", (unsigned long long)slippi::commands_seen(),
            (unsigned long long)slippi::replays_written(), slippi::gct_load_address());
  host::end_process(code);   // everything is saved: do not run the libraries' unload code (host.cpp)
}
