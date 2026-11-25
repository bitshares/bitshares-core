/*
 * Bulletproofs+ range proofs on secp256k1.
 *
 * Algorithm: Monero src/ringct/bulletproofs_plus.cc (BSD-3-Clause), which
 * implements https://eprint.iacr.org/2020/735 . The cofactor-8 clearing
 * (multiply/divide by 8) used on ed25519 is omitted: secp256k1 has cofactor 1.
 * Transcript hash is SHA-256, not Keccak, so proofs are not byte-compatible
 * with Monero. Commitments are BitShares Pedersen commitments
 * (blind*G + value*H). See libraries/ctcrypto/NOTICE.md.
 *
 * Proofs cover up to 16 64-bit values, aggregated, padded to the next power of two.
 */
#pragma once

#include <bts_ct/curve.hpp>

#include <stdint.h>
#include <vector>

namespace btsct {

static constexpr size_t BP_PLUS_MAX_OUTPUTS = 16;
static constexpr size_t BP_PLUS_BITS = 64;

struct BulletproofPlus {
   Point A, A1, B;
   Scalar r1, s1, d1;
   std::vector<Point> L, R;
};

BulletproofPlus bulletproof_plus_prove(const std::vector<uint64_t>& values,
                                       const std::vector<Scalar>& blinds);

/** Weight-1 (deterministic) verification of one proof against the commitments
 *  in the order they were proved. */
bool bulletproof_plus_verify(const BulletproofPlus& proof,
                             const std::vector<Point>& commitments);

/** Batch verification. weights must be non-zero and the same length as proofs.
 *  An empty weight vector uses weight 1 for every proof. */
bool bulletproof_plus_verify_batch(const std::vector<const BulletproofPlus*>& proofs,
                                   const std::vector<const std::vector<Point>*>& commitments,
                                   const std::vector<Scalar>& weights);

std::vector<uint8_t> bulletproof_plus_serialize(const BulletproofPlus& proof);
bool bulletproof_plus_parse(const uint8_t* p, size_t n, BulletproofPlus& out);

} // namespace btsct
