/* Exercise the production character conversions with reordered m-ex data. No game is run.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "../akaneia/mu_ak_fighters.c"

#define TEST_EXTERNAL 128
static int test_active, test_mapping[TEST_EXTERNAL];
static const MuAkFighter test_fighter = { .name = "Native fixture", .file = "PlWf.dat" };

int mu_mex_active(void) { return test_active; }
int mu_mex_internal_of_external(int ext)
{
    return ext >= 0 && ext < TEST_EXTERNAL ? test_mapping[ext] : -1;
}
int mu_mex_external_of_internal(int kind)
{
    int ext;
    for (ext = 0; ext < TEST_EXTERNAL; ++ext) {
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
    for (i = 0; i < TEST_EXTERNAL; ++i) test_mapping[i] = -1;
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
    /* An ACE shaped disc: 32 added slots (internal 27 to 58, native kinds up to 0x41), slot 7
     * (internal 34) empty, one fighter with no native code, the special fighters behind them
     * (internal 59 to 64), and external ids in the reverse of the internal order. */
    CHECK(MU_AK_KIND_SLOTS >= 32 && MU_AK_KIND_BASE + 31 == 0x41);
    CHECK(MU_AK_KIND(0x41) && MU_AK_CKIND(0x41) && !MU_AK_KIND(MU_FT_KIND_CAP));
    for (i = 0; i < TEST_EXTERNAL; ++i) test_mapping[i] = -1;
    test_active = 1; ak_shift = 32;
    for (i = 0; i < 32; ++i) {
        test_mapping[57 - i] = Ft_Kind_MasterH + i;
        if (i == 7) continue;                      /* the empty slot: no file, no fighter */
        ak_mex[i] = (signed char) (Ft_Kind_MasterH + i);
        if (i == 12) continue;                     /* in MxDt, no native code: locked */
        ak_ext[i] = (signed char) (57 - i);
        ak_fighter[i] = &test_fighter;
    }
    for (i = 0; i < 6; ++i) test_mapping[58 + i] = Ft_Kind_MasterH + 32 + i;
    for (i = 0; i < CKind_Playable_Count; ++i) {
        CHECK(mu_ak_ckind_from_mex(i) == i && mu_ak_mex_external(i) == i);
    }
    for (i = 0; i < 32; ++i) {
        const int ext = 57 - i;
        CHECK(mu_ak_kind_from_mex(Ft_Kind_MasterH + i) == MU_AK_KIND_BASE + i);
        if (i == 7 || i == 12) {
            CHECK(mu_ak_ckind_from_mex(ext) == -1);
            CHECK(mu_ak_ckind_from_kind(MU_AK_KIND_BASE + i) == -1);
            CHECK(mu_ak_mex_external(MU_AK_CKIND_BASE + i) == -1);
            CHECK(mu_ak_fighter(MU_AK_KIND_BASE + i) == NULL);
            continue;
        }
        CHECK(mu_ak_ckind_from_mex(ext) == MU_AK_CKIND_BASE + i);
        CHECK(mu_ak_mex_external(MU_AK_CKIND_BASE + i) == ext);
        CHECK(mu_ak_ckind_from_kind(MU_AK_KIND_BASE + i) == MU_AK_CKIND_BASE + i);
        CHECK(mu_ak_mex_internal(MU_AK_KIND_BASE + i) == Ft_Kind_MasterH + i);
    }
    CHECK(mu_ak_mex_internal(MU_AK_KIND_BASE + 7) == -1);
    CHECK(mu_ak_mex_internal(MU_AK_KIND_BASE + 12) == Ft_Kind_MasterH + 12);
    /* The special fighters come back to their retail kinds and go out under the disc's ids. */
    CHECK(mu_ak_kind_from_mex(Ft_Kind_MasterH + 32) == Ft_Kind_MasterH);
    CHECK(mu_ak_kind_from_mex(Ft_Kind_Sandbag + 32) == Ft_Kind_Sandbag);
    CHECK(mu_ak_kind_from_mex(Ft_Kind_Max + 32) == -1);
    CHECK(mu_ak_ckind_from_mex(58) == CKind_MasterH);
    CHECK(mu_ak_mex_external(CKind_MasterH) == 58);
    CHECK(mu_ak_mex_internal(Ft_Kind_Sandbag) == Ft_Kind_Sandbag + 32);
    CHECK(mu_ak_mex_external(ChKind_None) == ChKind_None);
    CHECK(mu_ak_ckind_from_mex(64) == -1 && mu_ak_ckind_from_mex(TEST_EXTERNAL) == -1);
    for (i = 0; i < MU_AK_KIND_SLOTS; ++i) {
        ak_fighter[i] = NULL; ak_mex[i] = -1; ak_ext[i] = -1;
    }
    test_active = 0; ak_shift = 0;
    CHECK(mu_ak_ckind_from_mex(26) == 26 && mu_ak_mex_external(26) == 26);
    return 0;
}
