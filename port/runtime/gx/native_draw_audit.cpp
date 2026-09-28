#include "native_draw_audit.h"

namespace gx {
void NativeDrawAudit::reset(bool enabled) {
  enabled_ = enabled;
  have_submitted_sequence_ = false;
  have_streamed_sequence_ = false;
  last_submitted_sequence_ = 0;
  last_streamed_sequence_ = 0;
  pending_.clear();
  open_scope_ids_.clear();
  stats_ = {};
}

void NativeDrawAudit::note_submitted_event(const NativeRenderScopeEvent& event) {
  if (!enabled_) return;
  if (have_submitted_sequence_ && event.sequence != last_submitted_sequence_ + 1u)
    ++stats_.sequence_errors;
  have_submitted_sequence_ = true;
  last_submitted_sequence_ = event.sequence;
  ++stats_.submitted_events;
  pending_.push_back(event);
}

void NativeDrawAudit::note_streamed_event(const NativeRenderScopeEvent& event) {
  if (!enabled_) return;
  if (have_streamed_sequence_ && event.sequence != last_streamed_sequence_ + 1u)
    ++stats_.sequence_errors;
  have_streamed_sequence_ = true;
  last_streamed_sequence_ = event.sequence;
  ++stats_.streamed_events;

  if (pending_.empty()) {
    ++stats_.mismatched_events;
  } else {
    const NativeRenderScopeEvent expected = pending_.front();
    pending_.pop_front();
    if (expected.sequence == event.sequence && expected.scope_id == event.scope_id &&
        expected.phase == event.phase && expected.reserved == event.reserved) {
      ++stats_.matched_events;
    } else {
      ++stats_.mismatched_events;
    }
  }

  if (event.phase == NATIVE_SCOPE_BEGIN) {
    ++stats_.scopes_started;
    open_scope_ids_.push_back(event.scope_id);
    if (open_scope_ids_.size() > stats_.max_scope_depth)
      stats_.max_scope_depth = open_scope_ids_.size();
  } else if (event.phase == NATIVE_SCOPE_END) {
    if (open_scope_ids_.empty() || open_scope_ids_.back() != event.scope_id) {
      ++stats_.invalid_scope_events;
    } else {
      open_scope_ids_.pop_back();
      ++stats_.scopes_ended;
    }
  } else {
    ++stats_.invalid_scope_events;
  }
}

void NativeDrawAudit::note_decoded_draw() {
  if (!enabled_) return;
  if (open_scope_ids_.empty()) ++stats_.unscoped_draws;
  else ++stats_.scoped_draws;
}

NativeDrawAuditStats NativeDrawAudit::stats() const {
  NativeDrawAuditStats out = stats_;
  out.pending_events = pending_.size();
  out.open_scopes = open_scope_ids_.size();
  return out;
}
}  // namespace gx
