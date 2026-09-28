#include "native_pose_bridge.h"

#include <array>
#include <cstdio>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<MuNativePoseJoint>);
static_assert(std::is_trivially_copyable_v<MuNativePoseTrack>);
static_assert(std::is_trivially_copyable_v<MuNativePoseSnapshot>);

int main() {
  MuNativePoseJoint joint{};
  joint.generation = 1;
  joint.scale[0] = joint.scale[1] = joint.scale[2] = 1.0f;
  joint.quaternion[3] = 1.0f;
  joint.world[0] = joint.world[5] = joint.world[10] = 1.0f;
  joint.rate = 1.0f;
  joint.end = 10.0f;
  joint.track_count = 1;

  MuNativePoseTrack track{};
  track.channel = 5;
  track.value_format = 2;
  track.slope_format = 2;
  track.byte_offset = 0;
  track.byte_length = 3;
  std::array<uint8_t, 3> bytes{{0x10, 0x20, 0x30}};

  MuNativePoseSnapshot snapshot{};
  snapshot.kind = MU_NATIVE_POSE_RIGID;
  snapshot.joint_count = 1;
  snapshot.track_count = 1;
  snapshot.track_byte_count = static_cast<uint32_t>(bytes.size());
  snapshot.joints = &joint;
  snapshot.tracks = &track;
  snapshot.track_bytes = bytes.data();

  auto pose = gx::copy_native_pose_snapshot(snapshot);
  // A rigid pose keeps its joints in the chain it shares with every draw of that joint.
  const auto* joints = pose && pose->chain ? &pose->chain->joints : nullptr;
  if (!joints || joints->size() != 1 || (*joints)[0].tracks.size() != 1 ||
      (*joints)[0].tracks[0].bytes != std::vector<uint8_t>(bytes.begin(), bytes.end())) {
    std::fputs("valid native pose payload was not copied\n", stderr);
    return 1;
  }

  bytes[0] = 0xFF;
  if ((*joints)[0].tracks[0].bytes[0] != 0x10) {
    std::fputs("native pose retained callback-scoped track memory\n", stderr);
    return 1;
  }

  MuNativePoseSnapshot invalid = snapshot;
  track.byte_offset = 2;
  if (gx::copy_native_pose_snapshot(invalid)) {
    std::fputs("out-of-range native track payload was accepted\n", stderr);
    return 1;
  }
  track.byte_offset = 0;
  invalid.kind = MU_NATIVE_POSE_ENVELOPE;
  if (gx::copy_native_pose_snapshot(invalid)) {
    std::fputs("envelope payload entered the rigid-pose converter\n", stderr);
    return 1;
  }

  // Envelope: root + two leaf joints, chains root..a and root..b sharing the root record.
  // Slot 0 is a single full-weight bone; slot 1 blends both. right_kind 1 uses chain 0.
  std::array<MuNativePoseJoint, 3> env_joints{};
  for (uint32_t i = 0; i < env_joints.size(); ++i) {
    env_joints[i].generation = i + 1;
    env_joints[i].scale[0] = env_joints[i].scale[1] = env_joints[i].scale[2] = 1.0f;
    env_joints[i].quaternion[3] = 1.0f;
    env_joints[i].world[0] = env_joints[i].world[5] = env_joints[i].world[10] = 1.0f;
    env_joints[i].world[3] = static_cast<float>(i);
  }
  const std::array<uint32_t, 4> links{{0, 1, 0, 2}};
  const std::array<MuNativePoseChain, 2> chains{{{0, 2}, {2, 2}}};
  std::array<MuNativePoseBone, 3> bones{};
  bones[0].chain = 0; bones[0].weight = 1.0f;
  bones[1].chain = 0; bones[1].weight = 0.25f;
  bones[2].chain = 1; bones[2].weight = 0.75f;
  for (auto& bone : bones) bone.envelope[0] = bone.envelope[5] = bone.envelope[10] = 1.0f;
  const std::array<MuNativePoseSlot, 2> slots{{{0, 1}, {1, 2}}};

  MuNativePoseSnapshot env{};
  env.kind = MU_NATIVE_POSE_ENVELOPE;
  env.joint_count = static_cast<uint32_t>(env_joints.size());
  env.joints = env_joints.data();
  env.has_view = 1;
  env.view[0] = env.view[5] = env.view[10] = 1.0f;
  env.chain_count = static_cast<uint32_t>(chains.size());
  env.chains = chains.data();
  env.link_count = static_cast<uint32_t>(links.size());
  env.links = links.data();
  env.bone_count = static_cast<uint32_t>(bones.size());
  env.bones = bones.data();
  env.slot_count = static_cast<uint32_t>(slots.size());
  env.slots = slots.data();
  env.right_kind = 1;
  env.right_chain_m = env.right_chain_x = 0;
  env.right_envelope[0] = env.right_envelope[5] = env.right_envelope[10] = 1.0f;

  auto skinned = gx::copy_native_envelope_snapshot(env);
  if (!skinned || !skinned->envelope || skinned->slots.size() != 2 ||
      skinned->slots[1].bones.size() != 2 || skinned->right_kind != 1 ||
      !skinned->right_chain_m || skinned->right_chain_m != skinned->slots[0].bones[0].chain ||
      skinned->slots[1].bones[1].chain->joints.size() != 2 ||
      skinned->slots[1].bones[1].chain->joints.back().world[3] != 2.0f ||
      skinned->slots[1].bones[1].weight != 0.75f) {
    std::fputs("valid native envelope payload was not converted\n", stderr);
    return 1;
  }
  if (gx::copy_native_envelope_snapshot(snapshot)) {
    std::fputs("rigid payload entered the envelope converter\n", stderr);
    return 1;
  }
  MuNativePoseSnapshot bad = env;
  const std::array<uint32_t, 4> bad_links{{0, 1, 0, 3}};
  bad.links = bad_links.data();
  if (gx::copy_native_envelope_snapshot(bad)) {
    std::fputs("envelope chain with an out-of-range joint was accepted\n", stderr);
    return 1;
  }
  bad = env;
  const std::array<MuNativePoseSlot, 2> gap_slots{{{0, 1}, {2, 1}}};
  bad.slots = gap_slots.data();
  if (gx::copy_native_envelope_snapshot(bad)) {
    std::fputs("envelope slots that skip a bone were accepted\n", stderr);
    return 1;
  }
  bad = env;
  bad.right_chain_x = 2;
  if (gx::copy_native_envelope_snapshot(bad)) {
    std::fputs("envelope right transform with an out-of-range chain was accepted\n", stderr);
    return 1;
  }
  return 0;
}
