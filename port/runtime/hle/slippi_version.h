// Version gate for Slippi online play. This port speaks one Slippi netplay version (SLIPPI_SEMVER).
// When Slippi's servers say a newer one is out, a match between this build and an updated player
// could run two different rule sets, so this build stays out of matchmaking until it is updated.
#pragma once
#include <cstdlib>
#include <string>

namespace slippi {

// An absent, truncated or malformed server reply is not evidence of compatibility.
inline bool release_version_valid(const std::string& version) {
  unsigned parts = 0, digits = 0;
  for (char ch : version) {
    if (ch >= '0' && ch <= '9') {
      if (++digits > 8) return false;
    } else if (ch == '.' && digits && ++parts < 4) {
      digits = 0;
    } else return false;
  }
  return digits && parts >= 2 && parts <= 3;
}

// True when dotted version `a` is newer than `b` ("3.6.10" is newer than "3.6.4"). Anything after
// the numbers (a "-beta" tag) is ignored; a missing part counts as 0; an empty `a` is never newer.
inline bool version_newer(const std::string& a, const std::string& b) {
  const char* pa = a.c_str();
  const char* pb = b.c_str();
  for (int part = 0; part < 4; ++part) {
    char* ea; char* eb;
    const long va = std::strtol(pa, &ea, 10), vb = std::strtol(pb, &eb, 10);
    if (va != vb) return va > vb;
    pa = *ea == '.' ? ea + 1 : ea;
    pb = *eb == '.' ? eb + 1 : eb;
    if (ea == pa && eb == pb) break;   // neither side has another part
  }
  return false;
}

}  // namespace slippi
