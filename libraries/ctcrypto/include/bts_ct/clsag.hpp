/*
 * CLSAG linkable ring signatures on secp256k1.
 *
 * Algorithm: Monero src/ringct/rctSigs.cpp CLSAG_Gen / verRctCLSAGSimple
 * (BSD-3-Clause), paper https://eprint.iacr.org/2019/654 .
 * Cofactor clearing is omitted (secp256k1 cofactor is 1). The transcript is
 * SHA-256 with the domain separators in NOTICE.md, so signatures are not
 * byte-compatible with Monero.
 *
 * For the real index l:
 *   P[l] = p * G
 *   C_nonzero[l] - C_offset = z * G
 *   I = p * Hp(P[l])
 *   D = z * Hp(P[l])
 */
#pragma once

#include <bts_ct/curve.hpp>

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace btsct {

struct ClsagSignature {
   Point I;
   Point D;
   Scalar c1;
   std::vector<Scalar> s;
};

/** I = spend_secret * Hp(spend_pub). Hp is the BSIP domain-separated hash-to-point. */
Point clsag_key_image(const Point& spend_pub, const Scalar& spend_secret);

ClsagSignature clsag_sign(const uint8_t message[32],
                          const std::vector<Point>& spend_pubs,
                          const Scalar& spend_secret,
                          const std::vector<Point>& commitments,
                          const Scalar& blind_diff,
                          const Point& pseudo_out,
                          size_t real_index);

bool clsag_verify(const uint8_t message[32],
                  const ClsagSignature& sig,
                  const std::vector<Point>& spend_pubs,
                  const std::vector<Point>& commitments,
                  const Point& pseudo_out);

std::vector<uint8_t> clsag_serialize(const ClsagSignature& sig);
bool clsag_parse(const uint8_t* p, size_t n, ClsagSignature& out);

} // namespace btsct
