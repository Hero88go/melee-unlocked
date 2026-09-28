// fres and frsqrte: the processor's table-driven estimates (after Dolphin Common/MathUtil.cpp).
// Included inside namespace ppc by the recompiler runtime AND by the native game's copy of the
// translated maths routines, so the two can never drift apart. Needs <cmath>, <cstring>, <limits>.
// SPDX-License-Identifier: GPL-2.0-or-later
static const int frsqrte_expected_base[] = {
  0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000, 0x2fb7000, 0x2d26000, 0x2ac0000,
  0x2881000, 0x2665000, 0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000, 0x1bf4000,
  0x1a7e800, 0x17cb800, 0x1552800, 0x130c000, 0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000,
  0x09e5000, 0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800, 0x0218800, 0x0105800,
};
static const int frsqrte_expected_dec[] = {
  0x7a4, 0x700, 0x670, 0x5f2, 0x584, 0x524, 0x4cc, 0x47e, 0x43a, 0x3fa, 0x3c2, 0x38e,
  0x35e, 0x332, 0x30a, 0x2e6, 0x568, 0x4f3, 0x48d, 0x435, 0x3e7, 0x3a2, 0x365, 0x32e,
  0x2fc, 0x2d0, 0x2a8, 0x283, 0x261, 0x243, 0x226, 0x20b,
};

double frsqrte(double val) {
  int64_t vali; std::memcpy(&vali, &val, 8);
  int64_t mantissa = vali & ((1LL << 52) - 1);
  int64_t sign = vali & (1LL << 63);
  int64_t exponent = vali & (0x7FFLL << 52);
  if (mantissa == 0 && exponent == 0)
    return sign ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
  if (exponent == (0x7FFLL << 52)) {
    if (mantissa == 0) return sign ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    return 0.0 + val;
  }
  if (sign) return std::numeric_limits<double>::quiet_NaN();
  if (!exponent) {
    do { exponent -= 1LL << 52; mantissa <<= 1; } while (!(mantissa & (1LL << 52)));
    mantissa &= (1LL << 52) - 1;
    exponent += 1LL << 52;
  }
  bool odd_exponent = !(exponent & (1LL << 52));
  exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) & (0x7FFLL << 52);
  int i = (int)(mantissa >> 37);
  vali = sign | exponent;
  int index = i / 2048 + (odd_exponent ? 16 : 0);
  vali |= (int64_t)(frsqrte_expected_base[index] - frsqrte_expected_dec[index] * (i % 2048)) << 26;
  double out; std::memcpy(&out, &vali, 8);
  return out;
}

static const int fres_expected_base[] = {
  0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
  0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
  0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
  0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00,
};
static const int fres_expected_dec[] = {
  0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245, 0x22a,
  0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e, 0x15b, 0x15b,
  0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106,
};

double fres(double val) {
  int64_t vali; std::memcpy(&vali, &val, 8);
  int64_t mantissa = vali & ((1LL << 52) - 1);
  int64_t sign = vali & (1LL << 63);
  int64_t exponent = vali & (0x7FFLL << 52);
  if (mantissa == 0 && exponent == 0) return std::copysign(std::numeric_limits<double>::infinity(), val);
  if (exponent == (0x7FFLL << 52)) {
    if (mantissa == 0) return std::copysign(0.0, val);
    return 0.0 + val;
  }
  if (exponent < (895LL << 52)) return std::copysign((double)std::numeric_limits<float>::max(), val);
  if (exponent >= (1149LL << 52)) return std::copysign(0.0, val);
  exponent = (0x7FDLL << 52) - exponent;
  int i = (int)(mantissa >> 37);
  vali = sign | exponent;
  vali |= (int64_t)(fres_expected_base[i / 1024] - (fres_expected_dec[i / 1024] * (i % 1024) + 1) / 2) << 29;
  double out; std::memcpy(&out, &vali, 8);
  return out;
}
