// Regression checks for the Mac host's Win32 compatibility boundary; no game or sockets.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* name) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; } };
  check(_putenv_s("MELEE_MAC_COMPAT_TEST", "set") == 0 && std::getenv("MELEE_MAC_COMPAT_TEST"), "set variable");
  check(_putenv_s("MELEE_MAC_COMPAT_TEST", "") == 0 && !std::getenv("MELEE_MAC_COMPAT_TEST"), "empty value removes variable");
  check(STATUS_UNSUCCESSFUL < 0, "NTSTATUS failures are signed 32-bit");
  for (const auto& vector : {std::pair<const wchar_t*, const char*>{L"MD5", "900150983cd24fb0d6963f7d28e17f72"},
                           {L"SHA1", "a9993e364706816aba3e25717850c26c9cd0d89d"},
                           {L"SHA256", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}}) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    check(BCryptOpenAlgorithmProvider(&algorithm, vector.first, nullptr, 0) == STATUS_SUCCESS, "open digest");
    BCRYPT_HASH_HANDLE hash = nullptr;
    check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == STATUS_SUCCESS, "create digest");
    DWORD length = 0; ULONG count = 0;
    check(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&length, sizeof length, &count, 0) == STATUS_SUCCESS &&
          count == sizeof length && length * 2 == std::strlen(vector.second), "digest length");
    check(BCryptHashData(hash, (PUCHAR)"a", 1, 0) == STATUS_SUCCESS &&
          BCryptHashData(hash, (PUCHAR)"bc", 2, 0) == STATUS_SUCCESS, "streaming digest");
    std::array<unsigned char,32> bytes{};
    check(BCryptFinishHash(hash, bytes.data(), 1, 0) < 0, "short output is rejected");
    check(BCryptFinishHash(hash, bytes.data(), bytes.size(), 0) == STATUS_SUCCESS, "finish digest");
    std::string hex;
    for (DWORD i = 0; i < length; ++i) { char value[3]; std::snprintf(value, sizeof value, "%02x", bytes[i]); hex += value; }
    check(hex == vector.second, "standard abc digest vector");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
  }
  BCRYPT_ALG_HANDLE invalid = nullptr;
  check(BCryptOpenAlgorithmProvider(&invalid, L"invalid", nullptr, 0) < 0, "unsupported algorithm fails");
  check(BCryptGenRandom(nullptr, nullptr, 1, 0) < 0, "invalid RNG output fails");
  void* memory = VirtualAlloc(nullptr, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  check(memory != nullptr, "reserve and commit memory");
  if (memory) { std::memset(memory, 0x42, 65536); check(VirtualFree(memory, 0, MEM_RELEASE), "release remembered allocation extent"); }
  std::printf("Mac compatibility regression checks: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
