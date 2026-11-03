// The page's volume control for the player box ("embed volume <gain>", NativeEmbed.tsx sends toFixed(3)).
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef YOUGAME_EMBED_VOLUME_H
#define YOUGAME_EMBED_VOLUME_H

/* "volume <gain>" with a plain decimal gain in [0, 1] -> the game's volume 0..100, rounded; anything
 * else -> -1. Parsed by hand, not with strtod: the game may run under a locale whose decimal point
 * is a comma, and the page always sends a point. Digits past the third decimal are ignored. */
static inline int yg_embed_volume(const char* line)
{
    static const char prefix[] = "volume ";
    const char* p = line;
    int i, whole = 0, thousandths = 0, places = 0, digits = 0, dot = 0, rest = 0;
    for (i = 0; prefix[i]; ++i)
        if (p[i] != prefix[i]) return -1;
    for (p += i; *p; ++p) {
        if (*p == '.' && !dot) { dot = 1; continue; }
        if (*p < '0' || *p > '9') return -1;
        ++digits;
        if (!dot) { if (whole < 2) whole = whole * 10 + (*p - '0'); }
        else if (places < 3) { thousandths = thousandths * 10 + (*p - '0'); ++places; }
        else rest |= *p != '0';
    }
    if (!digits || whole > 1) return -1;
    while (places < 3) { thousandths *= 10; ++places; }
    if (whole == 1 && (thousandths || rest)) return -1;
    return (whole * 1000 + thousandths + 5) / 10;
}

#endif
