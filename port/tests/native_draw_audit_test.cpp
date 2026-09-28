#include "native_draw_audit.h"
#include <cassert>

int main() {
  gx::NativeDrawAudit audit;
  audit.reset(true);

  const gx::NativeRenderScopeEvent begin_outer{1, 41, gx::NATIVE_SCOPE_BEGIN, 0};
  const gx::NativeRenderScopeEvent begin_inner{2, 42, gx::NATIVE_SCOPE_BEGIN, 0};
  const gx::NativeRenderScopeEvent end_inner{3, 42, gx::NATIVE_SCOPE_END, 0};
  const gx::NativeRenderScopeEvent end_outer{4, 41, gx::NATIVE_SCOPE_END, 0};
  for (const auto& event : {begin_outer, begin_inner, end_inner, end_outer})
    audit.note_submitted_event(event);

  audit.note_streamed_event(begin_outer);
  audit.note_streamed_event(begin_inner);
  audit.note_decoded_draw();
  audit.note_streamed_event(end_inner);
  audit.note_decoded_draw();
  audit.note_streamed_event(end_outer);
  audit.note_decoded_draw();

  // A missing event and a corrupt scope ending must both be visible in the audit.
  audit.note_submitted_event({6, 43, gx::NATIVE_SCOPE_BEGIN, 0});
  audit.note_streamed_event({7, 99, gx::NATIVE_SCOPE_END, 0});
  const auto stats = audit.stats();
  assert(stats.submitted_events == 5);
  assert(stats.streamed_events == 5);
  assert(stats.matched_events == 4);
  assert(stats.mismatched_events == 1);
  assert(stats.sequence_errors == 1);
  assert(stats.invalid_scope_events == 1);
  assert(stats.scopes_started == 2);
  assert(stats.scopes_ended == 2);
  assert(stats.scoped_draws == 2);
  assert(stats.unscoped_draws == 1);
  assert(stats.max_scope_depth == 2);
  assert(stats.pending_events == 0);
  assert(stats.open_scopes == 0);

  audit.reset(false);
  audit.note_submitted_event(begin_outer);
  audit.note_streamed_event(begin_outer);
  audit.note_decoded_draw();
  const auto disabled = audit.stats();
  assert(disabled.submitted_events == 0 && disabled.streamed_events == 0 &&
         disabled.scoped_draws == 0 && disabled.pending_events == 0);

}
