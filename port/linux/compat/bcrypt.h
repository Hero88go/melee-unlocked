// Linux build: the BCrypt hashes and random numbers the host uses, over OpenSSL's libcrypto and
// getrandom(). SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"

typedef long NTSTATUS;
typedef void* BCRYPT_ALG_HANDLE;
typedef void* BCRYPT_HASH_HANDLE;
#define BCRYPT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0L)
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#define BCRYPT_MD5_ALGORITHM L"MD5"
#define BCRYPT_SHA1_ALGORITHM L"SHA1"
#define BCRYPT_SHA256_ALGORITHM L"SHA256"
#define BCRYPT_RNG_ALGORITHM L"RNG"
#define BCRYPT_OBJECT_LENGTH L"ObjectLength"
#define BCRYPT_HASH_LENGTH L"HashDigestLength"
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002u
#define BCRYPT_HASH_REUSABLE_FLAG 0x00000020u
#define MS_PRIMITIVE_PROVIDER L"Microsoft Primitive Provider"

NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* algorithm, LPCWSTR id, LPCWSTR implementation, ULONG flags);
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE algorithm, ULONG flags);
NTSTATUS BCryptGetProperty(void* object, LPCWSTR property, PUCHAR output, ULONG size, ULONG* result, ULONG flags);
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* hash, PUCHAR object, ULONG object_size,
                          PUCHAR secret, ULONG secret_size, ULONG flags);
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR input, ULONG size, ULONG flags);
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR output, ULONG size, ULONG flags);
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash);
NTSTATUS BCryptHash(BCRYPT_ALG_HANDLE algorithm, PUCHAR secret, ULONG secret_size, PUCHAR input, ULONG input_size,
                    PUCHAR output, ULONG output_size);
NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE algorithm, PUCHAR buffer, ULONG size, ULONG flags);
