/* Melee Maker, native build: the game-side switch and the data its disc carries.
 *
 * Melee Maker (borrowed moves on any character) ships as a patched disc whose main.dol adds its
 * engine as an overlay. Natively that engine is compiled into the game (maker/) and its edits to the
 * game's source share rogueMelee's lines in melee-native.patch (include/mu_bm.h). Builds work only
 * while its disc is the loaded mod (the host's MU_MOD_MAKER bit, given for its supported release).
 *
 * Its bone tables are generated from the player's own game files by its build and compiled into the
 * disc's main.dol. They are read from there once, through command 0xEE by console address
 * (port/runtime/host/maker_source.h), and turned from the console's byte order. */
#include <mu_native.h>
#include <string.h>
#include <dolphin/os.h>

int mu_online_abi_command(unsigned int command, const unsigned char* payload, unsigned int size,
                          unsigned char* response, unsigned int capacity, unsigned int* response_size);
#include <engine/bone_tables.h>

#define MU_MOD_MAKER 0x8u          /* port/runtime/abi/mu_host.h */
#define MU_MAKER_COMMAND 0xEEu
#define MU_MAKER_OP_DOL_READ 0x01u

/* The release the host accepts (maker_source.h kBuild); online play compares it. */
const char bam_build_id[] = "1.3.8-1865b7";

unsigned char bam_rest_part[BAM_REST_PARTS];
short bam_rest_world[BAM_REST_KINDS][BAM_REST_PARTS][4];
unsigned char bam_rest_extra_part[BAM_REST_EXTRA];
short bam_rest_extra[BAM_REST_KINDS][BAM_REST_EXTRA][4];
short bam_rest_extra_rot[BAM_REST_KINDS][BAM_REST_EXTRA][3];
short bam_rest_extra_pos[BAM_REST_KINDS][BAM_REST_EXTRA][3];
unsigned char bam_part_parent[BAM_REST_KINDS][BAM_PART_COUNT];
BamPartMid bam_part_mid[BAM_PART_MID_COUNT + 1];
BamProp bam_prop[BAM_PROP_COUNT + 1];
unsigned char bam_prop_first[BAM_REST_KINDS + 1];
BamWeapon bam_weapon[BAM_WEAPON_COUNT + 1];
BamMesh bam_mesh[BAM_MESH_COUNT + 1];
BamMeshShow bam_mesh_show[BAM_MESH_SHOW_COUNT + 1];
BamMeshHide bam_mesh_hide[BAM_MESH_HIDE_COUNT + 1];
BamChain bam_chain[BAM_CHAIN_COUNT + 1];
unsigned char bam_chain_first[BAM_REST_KINDS + 1];

/* Field widths of one row, repeated over the table: each 2-byte field is swapped. */
static const char W_BYTES[] = { 1, 0 }, W_SHORTS[] = { 2, 0 }, W_MID[] = { 1, 1, 2, 2, 2, 2, 0 },
                  W_PROP[] = { 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0 },
                  W_WEAPON[] = { 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0 },
                  W_MESH[] = { 1, 1, 2, 2, 0 }, W_SHOW[] = { 1, 1, 1, 1, 2, 2, 0 };

/* Where release 1.3.8 keeps each table (its overlay's staging section). */
static const struct { void* dst; unsigned size, address; const char* widths; } tables[] = {
    { bam_rest_part, sizeof bam_rest_part, 0x80541C88, W_BYTES },
    { bam_rest_world, sizeof bam_rest_world, 0x80541CB0, W_SHORTS },
    { bam_rest_extra_part, sizeof bam_rest_extra_part, 0x80543888, W_BYTES },
    { bam_rest_extra, sizeof bam_rest_extra, 0x80543890, W_SHORTS },
    { bam_rest_extra_rot, sizeof bam_rest_extra_rot, 0x80543DA0, W_SHORTS },
    { bam_rest_extra_pos, sizeof bam_rest_extra_pos, 0x8054416C, W_SHORTS },
    { bam_part_parent, sizeof bam_part_parent, 0x80544538, W_BYTES },
    { bam_part_mid, sizeof bam_part_mid, 0x80544AD0, W_MID },
    { bam_prop, sizeof bam_prop, 0x80544AF8, W_PROP },
    { bam_prop_first, sizeof bam_prop_first, 0x8054542C, W_BYTES },
    { bam_weapon, sizeof bam_weapon, 0x80545448, W_WEAPON },
    { bam_mesh, sizeof bam_mesh, 0x80545508, W_MESH },
    { bam_mesh_show, sizeof bam_mesh_show, 0x805455A0, W_SHOW },
    { bam_mesh_hide, sizeof bam_mesh_hide, 0x80545698, W_BYTES },
    { bam_chain, sizeof bam_chain, 0x805456A8, W_PROP },
    { bam_chain_first, sizeof bam_chain_first, 0x805458B8, W_BYTES },
};

static int dol_read(unsigned address, unsigned char* dst, unsigned size)
{
    static unsigned char reply[4096];
    while (size) {
        unsigned char p[7];
        unsigned n = size > sizeof reply ? (unsigned) sizeof reply : size, got = 0;
        p[0] = MU_MAKER_OP_DOL_READ;
        p[1] = (unsigned char) (address >> 24);
        p[2] = (unsigned char) (address >> 16);
        p[3] = (unsigned char) (address >> 8);
        p[4] = (unsigned char) address;
        p[5] = (unsigned char) (n >> 8);
        p[6] = (unsigned char) n;
        if (mu_online_abi_command(MU_MAKER_COMMAND, p, sizeof p, reply, sizeof reply, &got) != 0 || got != n) {
            return 0;
        }
        memcpy(dst, reply, n);
        dst += n, address += n, size -= n;
    }
    return 1;
}

static int load_tables(void)
{
    unsigned i;
    for (i = 0; i < sizeof tables / sizeof tables[0]; ++i) {
        unsigned char* b = tables[i].dst;
        unsigned at = 0;
        if (!dol_read(tables[i].address, b, tables[i].size)) {
            return 0;
        }
        while (at < tables[i].size) {
            const char* w;
            for (w = tables[i].widths; *w && at < tables[i].size; at += (unsigned) *w++) {
                if (*w == 2) {
                    unsigned char t = b[at];
                    b[at] = b[at + 1];
                    b[at + 1] = t;
                }
            }
        }
    }
    return 1;
}

int mu_bm_maker(void)
{
    static int tables_state;   /* 0 not tried, 1 loaded, -1 failed */
    if (!(mu_mod_flags() & MU_MOD_MAKER)) {
        return 0;
    }
    /* Online, only Direct plays the disc's game (Unranked, Teams and Party play the retail one). */
    if ((mu_online_active() || mu_online_pending() || mu_slippi_in_online_mode()) && mu_slippi_selected_mode() != 2) {
        return 0;
    }
    if (tables_state == 0) {
        tables_state = load_tables() ? 1 : -1;
        OSReport("[bam] native: Melee Maker %s, bone tables %s\n", bam_build_id,
                 tables_state > 0 ? "read from its disc" : "missing, builds off");
    }
    return tables_state > 0;
}
