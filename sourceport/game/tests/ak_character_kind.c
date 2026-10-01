/* Exercise the production character conversions with reordered m-ex data. No game is run.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "../akaneia/mu_ak_fighters.c"

static int test_active, test_mapping[64];
static const MuAkFighter test_fighter = { .name = "Native fixture", .file = "PlWf.dat" };

int mu_mex_active(void) { return test_active; }
int mu_mex_internal_of_external(int ext)
{
    return ext >= 0 && ext < 64 ? test_mapping[ext] : -1;
}
int mu_mex_external_of_internal(int kind)
{
    int ext;
    for (ext = 0; ext < 64; ++ext) {
        if (test_mapping[ext] == kind) return ext;
    }
    return -1;
}
s8 Player_800325C8(CharacterKind ckind, bool sub)
{
    (void) sub;
    switch (ckind) {
    case CKind_MasterH: return Ft_Kind_MasterH;
    case CKind_CrezyH: return Ft_Kind_CrezyH;
    case CKind_Boy: return Ft_Kind_Boy;
    case CKind_Girl: return Ft_Kind_Girl;
    case CKind_GKoops: return Ft_Kind_GKoops;
    case ChKind_Sandbag: return Ft_Kind_Sandbag;
    case ChKind_Popo: return Ft_Kind_Popo;
    default: return -1;
    }
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void)
{
    int i;
    for (i = 0; i < 64; ++i) test_mapping[i] = -1;
    for (i = 0; i < MU_AK_KIND_SLOTS; ++i) {
        ak_fighter[i] = NULL; ak_mex[i] = -1; ak_ext[i] = -1;
    }
    CHECK(ChKind_None == 33 && CKind_MasterH == 26 && Ft_Kind_MasterH == 27);
    CHECK(mu_ak_ckind_from_mex(26) == 26 && mu_ak_mex_external(26) == 26);
    test_active = 1; ak_shift = 8;
    /* External order differs from internal order. Native ids follow the internal slot. */
    test_mapping[26] = 29;
    ak_mex[2] = 29; ak_ext[2] = 26; ak_fighter[2] = &test_fighter;
    for (i = 0; i < CKind_Playable_Count; ++i) {
        CHECK(mu_ak_ckind_from_mex(i) == i && mu_ak_mex_external(i) == i);
    }
    CHECK(mu_ak_ckind_from_mex(26) == MU_AK_CKIND_BASE + 2);
    CHECK(mu_ak_mex_external(MU_AK_CKIND_BASE + 2) == 26);
    CHECK(mu_ak_ckind_from_kind(MU_AK_KIND_BASE + 2) == MU_AK_CKIND_BASE + 2);
    CHECK(mu_ak_mex_internal(MU_AK_KIND_BASE + 2) == 29);
    CHECK(mu_ak_ckind_from_mex(27) == -1);
    CHECK(mu_ak_mex_external(MU_AK_CKIND_BASE) == -1);
    CHECK(mu_ak_ckind_from_mex(-1) == -1 && mu_ak_ckind_from_mex(64) == -1);
    CHECK(mu_ak_mex_external(MU_CK_KIND_CAP) == -1);
    /* A shifted special fighter keeps its retail native id, and exports its disc id again. */
    test_mapping[34] = 35;
    CHECK(mu_ak_ckind_from_mex(34) == CKind_MasterH);
    CHECK(mu_ak_mex_external(CKind_MasterH) == 34);
    CHECK(mu_ak_mex_external(ChKind_None) == ChKind_None);
    ak_fighter[2] = NULL; ak_mex[2] = -1; ak_ext[2] = -1;
    CHECK(mu_ak_ckind_from_kind(MU_AK_KIND_BASE + 2) == -1);
    CHECK(mu_ak_mex_external(MU_AK_CKIND_BASE + 2) == -1);
    test_active = 0; ak_shift = 0;
    CHECK(mu_ak_ckind_from_mex(26) == 26 && mu_ak_mex_external(26) == 26);
    return 0;
}
