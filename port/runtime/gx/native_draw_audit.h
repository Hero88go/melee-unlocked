// Validates optional native HSD_PObj scope markers against the GX command stream.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace gx {
enum : uint32_t { NATIVE_SCOPE_BEGIN = 1, NATIVE_SCOPE_END = 2 };

struct NativeRenderScopeEvent {
  uint32_t sequence = 0;
  uint32_t scope_id = 0;
  uint32_t phase = 0;
  uint32_t reserved = 0;
};

struct NativeDrawAuditStats {
  uint64_t submitted_events = 0;
  uint64_t streamed_events = 0;
  uint64_t matched_events = 0;
  uint64_t mismatched_events = 0;
  uint64_t sequence_errors = 0;
  uint64_t invalid_scope_events = 0;
  uint64_t scopes_started = 0;
  uint64_t scopes_ended = 0;
  uint64_t scoped_draws = 0;
  uint64_t unscoped_draws = 0;
  uint64_t max_scope_depth = 0;
  size_t pending_events = 0;
  size_t open_scopes = 0;
};

class NativeDrawAudit {
 public:
  void reset(bool enabled);
  void note_submitted_event(const NativeRenderScopeEvent& event);
  void note_streamed_event(const NativeRenderScopeEvent& event);
  void note_decoded_draw();
  bool enabled() const { return enabled_; }
  NativeDrawAuditStats stats() const;

 private:
  bool enabled_ = false;
  bool have_submitted_sequence_ = false;
  bool have_streamed_sequence_ = false;
  uint32_t last_submitted_sequence_ = 0;
  uint32_t last_streamed_sequence_ = 0;
  std::deque<NativeRenderScopeEvent> pending_;
  std::vector<uint32_t> open_scope_ids_;
  NativeDrawAuditStats stats_{};
};
}  // namespace gx
