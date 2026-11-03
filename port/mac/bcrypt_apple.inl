// CommonCrypto implementation of the host's BCrypt digest surface. Included by compat.cpp.
namespace {
struct Algorithm { enum Kind { MD5, SHA1, SHA256, RNG } kind; };
struct Hash {
  Algorithm::Kind kind;
  CC_MD5_CTX md5{}; CC_SHA1_CTX sha1{}; CC_SHA256_CTX sha256{};
};
ULONG digest_size(Algorithm::Kind kind) {
  return kind == Algorithm::MD5 ? CC_MD5_DIGEST_LENGTH : kind == Algorithm::SHA1 ? CC_SHA1_DIGEST_LENGTH :
         kind == Algorithm::SHA256 ? CC_SHA256_DIGEST_LENGTH : 0;
}
}
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* out, LPCWSTR id, LPCWSTR, ULONG) {
  if (!out || !id) return STATUS_UNSUCCESSFUL;
  auto name = narrow(id); Algorithm::Kind kind;
  if (name == "MD5") kind = Algorithm::MD5;
  else if (name == "SHA1") kind = Algorithm::SHA1;
  else if (name == "SHA256") kind = Algorithm::SHA256;
  else if (name == "RNG") kind = Algorithm::RNG;
  else return STATUS_UNSUCCESSFUL;
  *out = new Algorithm{kind}; return STATUS_SUCCESS;
}
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE a, ULONG) { delete static_cast<Algorithm*>(a); return STATUS_SUCCESS; }
NTSTATUS BCryptGetProperty(void* object, LPCWSTR property, PUCHAR output, ULONG size, ULONG* result, ULONG) {
  auto* a = static_cast<Algorithm*>(object);
  if (!a || !output || size < sizeof(DWORD)) return STATUS_UNSUCCESSFUL;
  auto p = narrow(property);
  DWORD value = p == "HashDigestLength" ? digest_size(a->kind) : p == "ObjectLength" ? sizeof(Hash) : 0;
  if (!value) return STATUS_UNSUCCESSFUL;
  std::memcpy(output, &value, sizeof value); if (result) *result = sizeof value;
  return STATUS_SUCCESS;
}
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* out, PUCHAR, ULONG, PUCHAR, ULONG, ULONG) {
  auto* a = static_cast<Algorithm*>(algorithm);
  if (!a || !out || a->kind == Algorithm::RNG) return STATUS_UNSUCCESSFUL;
  auto* h = new Hash{}; h->kind = a->kind;
  if (h->kind == Algorithm::MD5) CC_MD5_Init(&h->md5);
  else if (h->kind == Algorithm::SHA1) CC_SHA1_Init(&h->sha1);
  else CC_SHA256_Init(&h->sha256);
  *out = h; return STATUS_SUCCESS;
}
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR in, ULONG n, ULONG) {
  auto* h = static_cast<Hash*>(hash); if (!h) return STATUS_UNSUCCESSFUL;
  int ok = h->kind == Algorithm::MD5 ? CC_MD5_Update(&h->md5, in, n) :
           h->kind == Algorithm::SHA1 ? CC_SHA1_Update(&h->sha1, in, n) : CC_SHA256_Update(&h->sha256, in, n);
  return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR out, ULONG n, ULONG) {
  auto* h = static_cast<Hash*>(hash); if (!h || !out || n < digest_size(h->kind)) return STATUS_UNSUCCESSFUL;
  int ok = h->kind == Algorithm::MD5 ? CC_MD5_Final(out, &h->md5) :
           h->kind == Algorithm::SHA1 ? CC_SHA1_Final(out, &h->sha1) : CC_SHA256_Final(out, &h->sha256);
  return ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash) { delete static_cast<Hash*>(hash); return STATUS_SUCCESS; }
NTSTATUS BCryptHash(BCRYPT_ALG_HANDLE a, PUCHAR, ULONG, PUCHAR in, ULONG n, PUCHAR out, ULONG size) {
  BCRYPT_HASH_HANDLE h = nullptr; NTSTATUS s = BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0);
  if (s != STATUS_SUCCESS) return s;
  s = BCryptHashData(h, in, n, 0); if (s == STATUS_SUCCESS) s = BCryptFinishHash(h, out, size, 0);
  BCryptDestroyHash(h); return s;
}
NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE, PUCHAR out, ULONG n, ULONG) {
  if (!out && n) return STATUS_UNSUCCESSFUL;
  arc4random_buf(out, n); return STATUS_SUCCESS;
}
