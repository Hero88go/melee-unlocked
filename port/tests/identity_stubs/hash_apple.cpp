#include "bcrypt.h"
#include <CommonCrypto/CommonDigest.h>
#include <string>
static std::string narrow(LPCWSTR s) { return std::string(s, s + std::wcslen(s)); }
#include "../../../port/mac/bcrypt_apple.inl"
