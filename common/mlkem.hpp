// mlkem.hpp - a small C++ wrapper around mlkem-native (FIPS 203 ML-KEM).
//
// mlkem-native is written in C. This header gives it a C++ face:
//   * fixed-size std::array types, so key/ciphertext sizes are checked
//     by the compiler instead of by you,
//   * a KeyPair type that wipes its secret key when it goes out of scope,
//   * thin inline functions that forward to the C API.
//
// The parameter set (512 / 768 / 1024) is chosen at compile time with
// -DMLK_CONFIG_PARAMETER_SET=... (default 768).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "mlkem_native.h"  // C API; it already contains extern "C" { }

namespace pq {

// ---- Sizes (all in bytes) -------------------------------------------------
inline constexpr int kParameterSet = MLK_CONFIG_PARAMETER_SET;
inline constexpr std::size_t kPublicKeyBytes =
    MLKEM_PUBLICKEYBYTES(MLK_CONFIG_PARAMETER_SET);
inline constexpr std::size_t kSecretKeyBytes =
    MLKEM_SECRETKEYBYTES(MLK_CONFIG_PARAMETER_SET);
inline constexpr std::size_t kCiphertextBytes =
    MLKEM_CIPHERTEXTBYTES(MLK_CONFIG_PARAMETER_SET);
inline constexpr std::size_t kSharedSecretBytes = MLKEM_BYTES;  // always 32
inline constexpr std::size_t kSeedBytes = MLKEM_SYMBYTES;       // always 32

using PublicKey = std::array<std::uint8_t, kPublicKeyBytes>;
using SecretKey = std::array<std::uint8_t, kSecretKeyBytes>;
using Ciphertext = std::array<std::uint8_t, kCiphertextBytes>;
using SharedSecret = std::array<std::uint8_t, kSharedSecretBytes>;

// ---- Small security helpers ---------------------------------------------

// Overwrite memory with zeros. The volatile pointer stops the compiler from
// deciding "this buffer is never read again, so skip the writes".
inline void secure_wipe(void* p, std::size_t n) {
  volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
  while (n--) *v++ = 0;
}

// Compare two buffers without an early exit, so the time taken does not
// reveal *where* they first differ.
inline bool ct_equal(const std::uint8_t* a, const std::uint8_t* b,
                     std::size_t n) {
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < n; ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}

template <std::size_t N>
inline bool ct_equal(const std::array<std::uint8_t, N>& a,
                     const std::array<std::uint8_t, N>& b) {
  return ct_equal(a.data(), b.data(), N);
}

// ---- Key pair --------------------------------------------------------------
// Not copyable: we don't want secret keys silently duplicated in RAM.
struct KeyPair {
  PublicKey pk{};
  SecretKey sk{};

  KeyPair() = default;
  KeyPair(const KeyPair&) = delete;
  KeyPair& operator=(const KeyPair&) = delete;
  ~KeyPair() { secure_wipe(sk.data(), sk.size()); }
};

// ---- ML-KEM operations -------------------------------------------------
// All functions return 0 on success or a negative MLK_ERR_* code.

// KeyGen: random seeds come from randombytes(), which YOU provide.
inline int keygen(KeyPair& kp) {
  return mlkem_keypair(kp.pk.data(), kp.sk.data());
}

// KeyGen with caller-supplied seeds d || z (64 bytes). Same seeds -> same
// keys. Used for known-answer tests.
inline int keygen_derand(KeyPair& kp, const std::uint8_t coins[2 * kSeedBytes]) {
  return mlkem_keypair_derand(kp.pk.data(), kp.sk.data(), coins);
}

// Encaps: from a public key, produce a ciphertext and a fresh shared secret.
inline int encaps(Ciphertext& ct, SharedSecret& ss, const PublicKey& pk) {
  return mlkem_enc(ct.data(), ss.data(), pk.data());
}

// Encaps with caller-supplied 32-byte message m (deterministic).
inline int encaps_derand(Ciphertext& ct, SharedSecret& ss, const PublicKey& pk,
                         const std::uint8_t m[kSeedBytes]) {
  return mlkem_enc_derand(ct.data(), ss.data(), pk.data(), m);
}

// Decaps: from the ciphertext and the secret key, recover the shared secret.
inline int decaps(SharedSecret& ss, const Ciphertext& ct, const SecretKey& sk) {
  return mlkem_dec(ss.data(), ct.data(), sk.data());
}

inline const char* error_name(int rc) {
  switch (rc) {
    case 0:                     return "OK";
    case MLK_ERR_FAIL:          return "MLK_ERR_FAIL";
    case MLK_ERR_OUT_OF_MEMORY: return "MLK_ERR_OUT_OF_MEMORY";
    case MLK_ERR_RNG_FAIL:      return "MLK_ERR_RNG_FAIL";
    case MLK_ERR_INVALID_PK:    return "MLK_ERR_INVALID_PK";
    case MLK_ERR_INVALID_SK:    return "MLK_ERR_INVALID_SK";
    case MLK_ERR_PCT_FAIL:      return "MLK_ERR_PCT_FAIL";
    default:                    return "unknown error";
  }
}

}  // namespace pq
