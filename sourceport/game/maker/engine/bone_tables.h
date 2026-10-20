/* The bone tables' layout (tools/bam/bonetables.py writes them from the player's ISO at build time).
 * Natively they are read at boot from the player's patched disc, where that build compiled them in
 * (mu_maker.c), so they are writable here and hold no data until then. */
#ifndef BAM_BONE_TABLES_H
#define BAM_BONE_TABLES_H
#define BAM_REST_KINDS 27
#define BAM_REST_PARTS 33
extern unsigned char bam_rest_part[BAM_REST_PARTS];
/* World rest rotation quaternion (x, y, z, w) x 32767; all zero = no such bone. */
extern short bam_rest_world[BAM_REST_KINDS][BAM_REST_PARTS][4];
/* The same for the finger parts props and hitboxes hang from (not retargeted). */
#define BAM_REST_EXTRA 6
extern unsigned char bam_rest_extra_part[BAM_REST_EXTRA];
extern short bam_rest_extra[BAM_REST_KINDS][BAM_REST_EXTRA][4];
/* Their own (local) rest rotation, as the files store it (rad x 4096). */
extern short bam_rest_extra_rot[BAM_REST_KINDS][BAM_REST_EXTRA][3];
/* Their rest position in their parent bone (x 256). */
extern short bam_rest_extra_pos[BAM_REST_KINDS][BAM_REST_EXTRA][3];
/* Body part of each part's nearest ancestor with one (0xFF: none); all 0xFF = unknown. */
#define BAM_PART_COUNT 53
extern unsigned char bam_part_parent[BAM_REST_KINDS][BAM_PART_COUNT];
/* Rest rotation of body-less joints between a part and its parent part (none if absent). */
typedef struct BamPartMid { unsigned char kind, part; short q[4]; } BamPartMid;
#define BAM_PART_MID_COUNT 3
extern BamPartMid bam_part_mid[BAM_PART_MID_COUNT + 1];
/* Body-less bones (sword, tail...) that move hitboxes ride on: kind, joint, parent entry (0xFF: hangs
 * from the body part), body part, rest rotation (rad x 4096), position (x 256), scale (x 4096). */
typedef struct BamProp { unsigned char kind, joint, parent, part; short rot[3], pos[3], scale[3]; } BamProp;
#define BAM_PROP_COUNT 106
extern BamProp bam_prop[BAM_PROP_COUNT + 1];
/* Each kind's rows: bam_prop[bam_prop_first[k]] up to bam_prop_first[k + 1] (rows are by kind). */
extern unsigned char bam_prop_first[BAM_REST_KINDS + 1];
/* Borrowed weapons: donor kind, prop entry the weapon lies along, item (0 Beam Sword, 1 Hammer, 2 parasol),
 * donor motions it shows in (first..last), grip point on the prop (x 256), turn (x 32767) from the
 * item model's long axis (+Y) onto the weapon, and grip-to-farthest-hitbox reach (x 256). */
typedef struct BamWeapon { unsigned char kind, prop, item, pad; unsigned short first, last;
    short grip[3], align[4], reach; } BamWeapon;
#define BAM_WEAPON_COUNT 7
extern BamWeapon bam_weapon[BAM_WEAPON_COUNT + 1];
/* The donor's own meshes for rebuilt bones (Marth's sword, Mewtwo's tail): kind, group (one chain
 * of rebuilt bones), DObj index in the donor's model, PObjs of it drawn (bit each, 0 all); and when each group shows: kind, group,
 * weapon item it replaces (255: none), first..last donor motion. */
typedef struct BamMesh { unsigned char kind, group; unsigned short dobj, pobjs; } BamMesh;
typedef struct BamMeshShow { unsigned char kind, group, item, pad; unsigned short first, last; } BamMeshShow;
#define BAM_MESH_COUNT 24
extern BamMesh bam_mesh[BAM_MESH_COUNT + 1];
#define BAM_MESH_SHOW_COUNT 30
extern BamMeshShow bam_mesh_show[BAM_MESH_SHOW_COUNT + 1];
/* Donor joints collapsed (drawn at no size) while a mesh group shows: kind, group, joint. */
typedef struct BamMeshHide { unsigned char kind, group, joint; } BamMeshHide;
#define BAM_MESH_HIDE_COUNT 2
extern BamMeshHide bam_mesh_hide[BAM_MESH_HIDE_COUNT + 1];
/* Donor joints posed through the donor's own skeleton from its root (ROOT_CHAINS): kind, joint,
 * parent entry (255: the root), rest rotation (rad x 4096), position (x 256), scale (x 4096). */
typedef struct BamChain { unsigned char kind, joint, parent, pad; short rot[3], pos[3], scale[3]; } BamChain;
#define BAM_CHAIN_COUNT 23
extern BamChain bam_chain[BAM_CHAIN_COUNT + 1];
extern unsigned char bam_chain_first[BAM_REST_KINDS + 1];

#endif
