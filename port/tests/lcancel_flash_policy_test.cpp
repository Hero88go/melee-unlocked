// Legacy display compatibility and exclusive choices, without a game or graphics device.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mu_lcancel_flash.h"
#include <cstdio>
int failures = 0;
void check(bool condition, const char* text) {
    std::printf("%s %s\n", condition ? "ok" : "FAIL", text);
    failures += !condition;
}
int main() {
    const unsigned int unrelated = 0x00044110u;
    check(mu_lcancel_flash_mode(0, 0) == MU_LCFLASH_OFF &&
          mu_lcancel_flash_mode(0, 1) == MU_LCFLASH_MU_MISSED, "old MU indicator keeps its off/missed behavior");
    const unsigned int legacy = MU_LCFLASH_TE_ENABLE;
    check(mu_lcancel_flash_mode(legacy, 1) == MU_LCFLASH_TE_BOTH &&
          mu_lcancel_success_color(legacy) == MU_LCFLASH_SUCCESS_WHITE &&
          mu_lcancel_te_reports(legacy, 0) && mu_lcancel_te_reports(legacy, 1),
          "old TE settings and recordings retain white success and red miss");
    for (int mode = 0; mode < 7; ++mode) {
        const unsigned int bits = mu_lcancel_with_flash_mode(unrelated | legacy, mode);
        check((bits & unrelated) == unrelated, "changing flash mode preserves unrelated TE choices");
        check(mu_lcancel_flash_mode(bits, mu_lcancel_renderer_mode(mode) ? mode : 0) == mode, "each exclusive flash choice round-trips");
        if (mode < MU_LCFLASH_TE_MISSED || mu_lcancel_renderer_mode(mode))
            check(!mu_lcancel_te_reports(bits, 0) && !mu_lcancel_te_reports(bits, 1), "Off and MU suppress the native TE effect");
        if (mode == MU_LCFLASH_TE_MISSED)
            check(mu_lcancel_te_reports(bits, 0) && !mu_lcancel_te_reports(bits, 1), "TE missed does not flash successful landings");
        if (mode == MU_LCFLASH_TE_SUCCESS)
            check(!mu_lcancel_te_reports(bits, 0) && mu_lcancel_te_reports(bits, 1), "TE success does not flash missed landings");
    }
    check(mu_lcancel_renderer_reports(MU_LCFLASH_MU_SUCCESS, 1) &&
          !mu_lcancel_renderer_reports(MU_LCFLASH_MU_SUCCESS, 0), "native success reports only successes");
    check(mu_lcancel_renderer_reports(MU_LCFLASH_MU_BOTH, 1) &&
          mu_lcancel_renderer_reports(MU_LCFLASH_MU_BOTH, 0), "native both reports successes and misses");
    for (int color = 0; color < 3; ++color) {
        const unsigned int bits = mu_lcancel_with_success_color(unrelated | legacy, color);
        check(mu_lcancel_success_color(bits) == color && (bits & unrelated) == unrelated,
              "success color persists without changing other TE choices");
        check(mu_lcancel_te_reports(bits, 0), "success color never suppresses missed feedback");
        check(mu_lcancel_te_reports(bits, 1) == (color != MU_LCFLASH_SUCCESS_OFF), "success Off suppresses only successful feedback");
    }
    check(mu_lcancel_success_color(MU_LCFLASH_TE_GREEN | MU_LCFLASH_TE_SUCCESS_OFF) == MU_LCFLASH_SUCCESS_OFF,
          "conflicting manually edited color bits settle safely on Off");
    return failures ? 1 : 0;
}
