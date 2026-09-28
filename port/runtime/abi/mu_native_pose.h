/* Plain C payloads for the native HSD-to-host authored-pose bridge.
 * Array pointers in MuNativePoseSnapshot are borrowed for the duration of the callback only;
 * the host must copy them before returning. They point to POD records/bytes, never HSD objects.
 * Keep this header free of system includes: native decompilation units use a separate C library.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MU_NATIVE_POSE_H
#define MU_NATIVE_POSE_H

#if defined(_MSC_VER)
typedef unsigned __int32 MuNativePoseU32;
typedef signed __int16 MuNativePoseS16;
typedef unsigned __int8 MuNativePoseU8;
#else
typedef __UINT32_TYPE__ MuNativePoseU32;
typedef __INT16_TYPE__ MuNativePoseS16;
typedef __UINT8_TYPE__ MuNativePoseU8;
#endif

#define MU_NATIVE_POSE_MAX_JOINTS 128u
#define MU_NATIVE_POSE_MAX_TRACKS 4096u
#define MU_NATIVE_POSE_MAX_TRACK_BYTES (1024u * 1024u)
/* Envelope (skinned) snapshots: unique joints of every bone chain, and the chains that use them. */
#define MU_NATIVE_POSE_MAX_TOTAL_JOINTS 1024u
#define MU_NATIVE_POSE_MAX_CHAINS 128u
#define MU_NATIVE_POSE_MAX_LINKS 8192u
#define MU_NATIVE_POSE_MAX_SLOTS 10u
#define MU_NATIVE_POSE_MAX_BONES 640u

enum {
    MU_NATIVE_POSE_UNSUPPORTED = 0,
    MU_NATIVE_POSE_RIGID = 1,
    MU_NATIVE_POSE_ENVELOPE = 2
};

enum {
    MU_NATIVE_POSE_REJECT_NONE = 0,
    MU_NATIVE_POSE_REJECT_KIND = 1,
    MU_NATIVE_POSE_REJECT_NO_JOINT = 2,
    MU_NATIVE_POSE_REJECT_CHAIN_LIMIT = 3,
    MU_NATIVE_POSE_REJECT_JOINT_FLAGS = 4,
    MU_NATIVE_POSE_REJECT_CONSTRAINTS = 5,
    MU_NATIVE_POSE_REJECT_TRACK_LIMIT = 6,
    MU_NATIVE_POSE_REJECT_TRACK_BYTES = 7,
    MU_NATIVE_POSE_REJECT_TRACK_FORMAT = 8,
    MU_NATIVE_POSE_REJECT_ENVELOPE_DATA = 9,
    MU_NATIVE_POSE_REJECT_ENVELOPE_LIMIT = 10,
    MU_NATIVE_POSE_REJECT_BOUND_ANIMATION = 11,
    MU_NATIVE_POSE_REJECT_COUNT = 12
};

typedef struct MuNativePoseJoint {
    MuNativePoseU32 generation;
    MuNativePoseU32 flags;
    MuNativePoseU32 anim_flags;
    MuNativePoseU32 first_track;
    MuNativePoseU32 track_count;
    float scale[3];
    float rotation[3];
    float translation[3];
    float quaternion[4];
    float world[12];
    float frame;
    float rate;
    float end;
    float rewind;
} MuNativePoseJoint;

typedef struct MuNativePoseTrack {
    MuNativePoseS16 start_frame;
    MuNativePoseU8 channel;
    MuNativePoseU8 value_format;
    MuNativePoseU8 slope_format;
    MuNativePoseU8 reserved[3];
    MuNativePoseU32 byte_offset;
    MuNativePoseU32 byte_length;
} MuNativePoseTrack;

/* Envelope chain: links[first_link .. first_link+link_count) are joint indices, root..leaf. */
typedef struct MuNativePoseChain {
    MuNativePoseU32 first_link;
    MuNativePoseU32 link_count;
} MuNativePoseChain;

/* One weighted bone of a matrix slot: its joint chain and inverse-bind (envelope) matrix. */
typedef struct MuNativePoseBone {
    MuNativePoseU32 chain;
    float weight;
    float envelope[12];
} MuNativePoseBone;

typedef struct MuNativePoseSlot {
    MuNativePoseU32 first_bone;
    MuNativePoseU32 bone_count;
} MuNativePoseSlot;

typedef struct MuNativePoseSnapshot {
    MuNativePoseU32 scope_id;
    MuNativePoseU32 kind;
    MuNativePoseU32 reject_reason;
    MuNativePoseU32 joint_count;
    MuNativePoseU32 track_count;
    MuNativePoseU32 track_byte_count;
    MuNativePoseU32 has_view;
    MuNativePoseU32 quake;
    float view[12];
    const MuNativePoseJoint* joints;
    const MuNativePoseTrack* tracks;
    const MuNativePoseU8* track_bytes;

    /* Host API version 7. default_setup is set for every kind; the rest is MU_NATIVE_POSE_ENVELOPE only. `joints` then holds each joint once;
     * chains reference them by index. right_kind mirrors _HSD_mkEnvelopeModelNodeMtx:
     * 0 none (skeleton root), 1 inverse(x.env), 2 inverse(x.world)*m.world,
     * 3 inverse(x.world*x.env)*m.world, with m = right_chain_m's leaf and x = right_chain_x's. */
    MuNativePoseU32 default_setup;   /* 1: the stock HSD PObjSetupMtx path (what Legacy observes) */
    MuNativePoseU32 chain_count;
    MuNativePoseU32 link_count;
    MuNativePoseU32 bone_count;
    MuNativePoseU32 slot_count;
    MuNativePoseU32 right_kind;
    MuNativePoseU32 right_chain_m;
    MuNativePoseU32 right_chain_x;
    float right_envelope[12];
    const MuNativePoseChain* chains;
    const MuNativePoseU32* links;
    const MuNativePoseBone* bones;
    const MuNativePoseSlot* slots;
} MuNativePoseSnapshot;

#endif
