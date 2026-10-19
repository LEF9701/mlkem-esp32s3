// experiments.hpp - the ML-KEM experiments, shared by the Ubuntu (host)
// program and the ESP32-S3 firmware. Only timing and randomness differ
// between the two platforms; those are passed in or provided by main.cpp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mlkem.hpp"
#include "kat_vectors.h"  // fixed inputs + expected outputs (from mlkem-native)

namespace lab {

using NowMicros = std::uint64_t (*)();

inline void print_hex(const char* label, const std::uint8_t* p, std::size_t n,
                      std::size_t show = 16) {
  std::printf("  %-14s", label);
  for (std::size_t i = 0; i < n && i < show; ++i) std::printf("%02x", p[i]);
  if (n > show) std::printf("...  (%u bytes)", static_cast<unsigned>(n));
  std::printf("\n");
}

inline bool check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  return ok;
}

// ---------------------------------------------------------------------------
// Experiment 1 - Known-answer test (KAT)
// Feed fixed seeds into the deterministic API and compare every output
// byte with the expected values. If this passes, the library computes
// exactly the same thing on your CPU as on the machine that made the vectors.
// ---------------------------------------------------------------------------
inline bool exp1_known_answer_test() {
  std::printf("\n=== Experiment 1: known-answer test (ML-KEM-%d) ===\n",
              pq::kParameterSet);
  bool ok = true;

  std::uint8_t coins[2 * pq::kSeedBytes];
  std::memcpy(coins, test_vector_d, pq::kSeedBytes);
  std::memcpy(coins + pq::kSeedBytes, test_vector_z, pq::kSeedBytes);

  pq::KeyPair kp;
  ok &= check(pq::keygen_derand(kp, coins) == 0, "keygen_derand returns 0");
  ok &= check(std::memcmp(kp.pk.data(), test_vector_pk, pq::kPublicKeyBytes) == 0,
              "public key matches vector");
  ok &= check(std::memcmp(kp.sk.data(), test_vector_sk, pq::kSecretKeyBytes) == 0,
              "secret key matches vector");

  pq::Ciphertext ct;
  pq::SharedSecret ss_enc, ss_dec;
  ok &= check(pq::encaps_derand(ct, ss_enc, kp.pk, test_vector_m) == 0,
              "encaps_derand returns 0");
  ok &= check(std::memcmp(ct.data(), test_vector_ct, pq::kCiphertextBytes) == 0,
              "ciphertext matches vector");
  ok &= check(std::memcmp(ss_enc.data(), test_vector_ss, pq::kSharedSecretBytes) == 0,
              "shared secret matches vector");

  ok &= check(pq::decaps(ss_dec, ct, kp.sk) == 0, "decaps returns 0");
  ok &= check(pq::ct_equal(ss_enc, ss_dec), "decaps recovers the same secret");

  std::printf("  Result: %s\n", ok ? "ALL PASS" : "SOMETHING FAILED");
  return ok;
}

// ---------------------------------------------------------------------------
// Experiment 2 - A key exchange between "Alice" and "Bob"
// Alice publishes a public key. Bob uses it to make a ciphertext and a secret.
// Alice decapsulates the ciphertext and gets the same 32-byte secret.
// Only pk and ct ever travel over the (imaginary) network.
// ---------------------------------------------------------------------------
inline bool exp2_key_exchange() {
  std::printf("\n=== Experiment 2: Alice <-> Bob key exchange ===\n");

  pq::KeyPair alice;  // Alice's long-term (or per-session) key pair
  int rc = pq::keygen(alice);
  if (rc != 0) {
    std::printf("  keygen failed: %s\n", pq::error_name(rc));
    return false;
  }
  std::printf("  Alice -> Bob : public key\n");
  print_hex("pk", alice.pk.data(), alice.pk.size());

  pq::Ciphertext ct;
  pq::SharedSecret bob_secret;
  rc = pq::encaps(ct, bob_secret, alice.pk);
  if (rc != 0) {
    std::printf("  encaps failed: %s\n", pq::error_name(rc));
    return false;
  }
  std::printf("  Bob -> Alice : ciphertext\n");
  print_hex("ct", ct.data(), ct.size());

  pq::SharedSecret alice_secret;
  rc = pq::decaps(alice_secret, ct, alice.sk);
  if (rc != 0) {
    std::printf("  decaps failed: %s\n", pq::error_name(rc));
    return false;
  }

  print_hex("Bob's secret", bob_secret.data(), bob_secret.size(), 32);
  print_hex("Alice secret", alice_secret.data(), alice_secret.size(), 32);
  bool ok = check(pq::ct_equal(alice_secret, bob_secret), "both sides agree");

  // In a real protocol you would now feed the secret into a KDF/AEAD.
  // Here we just wipe it.
  pq::secure_wipe(bob_secret.data(), bob_secret.size());
  pq::secure_wipe(alice_secret.data(), alice_secret.size());
  return ok;
}

// ---------------------------------------------------------------------------
// Experiment 3 - What happens when data is tampered with?
//  a) Flip 1 bit of the ciphertext: decaps still "succeeds" (returns 0) but
//     gives a different, pseudo-random secret. This is "implicit rejection":
//     an attacker learns nothing from an error code.
//  b) Corrupt the public key so a coefficient is >= q (3329): encaps refuses.
//  c) Corrupt the hash H(pk) stored inside the secret key: decaps refuses.
// ---------------------------------------------------------------------------
inline bool exp3_tampering() {
  std::printf("\n=== Experiment 3: tampering ===\n");
  bool ok = true;

  pq::KeyPair kp;
  pq::Ciphertext ct;
  pq::SharedSecret ss_good, ss_bad;
  if (pq::keygen(kp) != 0 || pq::encaps(ct, ss_good, kp.pk) != 0) return false;

  // (a) one flipped bit in the ciphertext
  ct[0] ^= 0x01;
  int rc = pq::decaps(ss_bad, ct, kp.sk);
  std::printf("  (a) 1 bit flipped in ct -> decaps returned %d (%s)\n", rc,
              pq::error_name(rc));
  print_hex("real secret", ss_good.data(), ss_good.size(), 32);
  print_hex("got secret", ss_bad.data(), ss_bad.size(), 32);
  ok &= check(rc == 0 && !pq::ct_equal(ss_good, ss_bad),
              "returns 0 but secret is different (implicit rejection)");
  ct[0] ^= 0x01;  // undo

  // (b) public key with an out-of-range coefficient.
  // The first 12-bit coefficient is stored in byte 0 + low 4 bits of byte 1.
  pq::PublicKey bad_pk = kp.pk;
  bad_pk[0] = 0xFF;
  bad_pk[1] |= 0x0F;  // coefficient = 0xFFF = 4095 > 3328
  pq::Ciphertext ct2;
  pq::SharedSecret ss2;
  rc = pq::encaps(ct2, ss2, bad_pk);
  std::printf("  (b) invalid public key -> encaps returned %d (%s)\n", rc,
              pq::error_name(rc));
  ok &= check(rc == MLK_ERR_INVALID_PK, "encaps rejects malformed public key");

  // (c) secret key layout: dk_pke | ek | H(ek) (32) | z (32)
  // Flip a bit inside H(ek), which sits 64 bytes before the end.
  pq::SecretKey bad_sk = kp.sk;
  bad_sk[pq::kSecretKeyBytes - 64] ^= 0x80;
  rc = pq::decaps(ss2, ct, bad_sk);
  std::printf("  (c) corrupted secret key -> decaps returned %d (%s)\n", rc,
              pq::error_name(rc));
  ok &= check(rc == MLK_ERR_INVALID_SK, "decaps rejects inconsistent secret key");
  pq::secure_wipe(bad_sk.data(), bad_sk.size());

  return ok;
}

// ---------------------------------------------------------------------------
// Experiment 4 - Benchmark
// `pause` (may be null) is called between rounds; on the ESP32 it yields to
// FreeRTOS so the watchdog stays happy. It is outside the timed region.
// ---------------------------------------------------------------------------
inline void exp4_benchmark(NowMicros now, int rounds, void (*pause)() = nullptr) {
  std::printf("\n=== Experiment 4: benchmark (%d rounds each) ===\n", rounds);
  std::uint64_t t_kg = 0, t_enc = 0, t_dec = 0;

  for (int i = 0; i < rounds; ++i) {
    pq::KeyPair kp;
    pq::Ciphertext ct;
    pq::SharedSecret a, b;

    std::uint64_t t0 = now();
    int r1 = pq::keygen(kp);
    std::uint64_t t1 = now();
    int r2 = pq::encaps(ct, a, kp.pk);
    std::uint64_t t2 = now();
    int r3 = pq::decaps(b, ct, kp.sk);
    std::uint64_t t3 = now();

    if (r1 || r2 || r3 || !pq::ct_equal(a, b)) {
      std::printf("  round %d FAILED\n", i);
      return;
    }
    t_kg += t1 - t0;
    t_enc += t2 - t1;
    t_dec += t3 - t2;
    if (pause) pause();
  }

  auto avg = [rounds](std::uint64_t t) {
    return static_cast<double>(t) / rounds;
  };
  std::printf("  %-8s %12s %14s\n", "op", "avg [us]", "ops / second");
  std::printf("  %-8s %12.1f %14.1f\n", "keygen", avg(t_kg), 1e6 / avg(t_kg));
  std::printf("  %-8s %12.1f %14.1f\n", "encaps", avg(t_enc), 1e6 / avg(t_enc));
  std::printf("  %-8s %12.1f %14.1f\n", "decaps", avg(t_dec), 1e6 / avg(t_dec));
  std::printf("  sizes: pk=%u  sk=%u  ct=%u  ss=%u bytes\n",
              static_cast<unsigned>(pq::kPublicKeyBytes),
              static_cast<unsigned>(pq::kSecretKeyBytes),
              static_cast<unsigned>(pq::kCiphertextBytes),
              static_cast<unsigned>(pq::kSharedSecretBytes));
}

}  // namespace lab
