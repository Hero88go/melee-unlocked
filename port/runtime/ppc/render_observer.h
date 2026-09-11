// Read-only render identity hooks around selected original guest functions.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <memory>
namespace ppc { struct Context; }
namespace gx {
enum class Observe { AllocateJoint, ReleaseJoint, DisplayJoint, RigidMatrix, OtherMatrix };
struct AuthoredPose;
class RenderObserver {
  ppc::Context& cpu_;
  Observe kind_;
  uint64_t saved_generation_ = 0, saved_pass_ = 0;
  uint32_t saved_draw_ = 0, saved_joint_ = 0;
  uint8_t* saved_memory_ = nullptr;
  bool saved_rigid_ = false;
  std::shared_ptr<const AuthoredPose> saved_pose_;
 public:
  RenderObserver(ppc::Context& cpu, Observe kind, uint8_t* memory = nullptr);
  ~RenderObserver();
  RenderObserver(const RenderObserver&) = delete;
  RenderObserver& operator=(const RenderObserver&) = delete;
};
uint64_t observed_draw_identity(uint64_t fallback, uint64_t& generation);
void finish_observed_frame();
void set_authored_capture(bool enabled);
std::shared_ptr<const AuthoredPose> capture_authored_pose();
}
