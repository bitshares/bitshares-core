/*
 * Bulletproofs+ prover and verifier.
 *
 * Structure follows Monero src/ringct/bulletproofs_plus.cc (BSD-3-Clause,
 * Copyright (c) 2017-2022 The Monero Project) and eprint 2020/735.
 * Differences, all documented in NOTICE.md:
 *   - secp256k1, cofactor 1: no INV_EIGHT scaling and no multiply-by-8
 *   - SHA-256 Fiat-Shamir transcript with the domain strings below
 *   - commitments are supplied by the caller (BitShares Pedersen), not embedded
 */
#include <bts_ct/bulletproofs_plus.hpp>

#include <openssl/sha.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace btsct {
namespace {

constexpr size_t NBITS = BP_PLUS_BITS;
constexpr size_t MAX_M = BP_PLUS_MAX_OUTPUTS;

void append_scalar(std::vector<uint8_t>& buf, const Scalar& s) {
   uint8_t b[32];
   s.to_bytes(b);
   buf.push_back(0x01);
   buf.insert(buf.end(), b, b + 32);
}

void append_point(std::vector<uint8_t>& buf, const Point& p) {
   uint8_t b[33];
   p.to_compressed(b);
   buf.push_back(0x02);
   buf.insert(buf.end(), b, b + 33);
}

Scalar hash_bytes(const std::vector<uint8_t>& buf) {
   return Scalar::hash_to_scalar(buf.data(), buf.size());
}

Scalar transcript_update(Scalar& transcript, const Scalar& u) {
   std::vector<uint8_t> buf;
   append_scalar(buf, transcript);
   append_scalar(buf, u);
   transcript = hash_bytes(buf);
   return transcript;
}

Scalar transcript_update(Scalar& transcript, const Point& u) {
   std::vector<uint8_t> buf;
   append_scalar(buf, transcript);
   append_point(buf, u);
   transcript = hash_bytes(buf);
   return transcript;
}

Scalar transcript_update(Scalar& transcript, const Point& a, const Point& b) {
   std::vector<uint8_t> buf;
   append_scalar(buf, transcript);
   append_point(buf, a);
   append_point(buf, b);
   transcript = hash_bytes(buf);
   return transcript;
}

Scalar hash_commitments(const std::vector<Point>& V) {
   std::vector<uint8_t> buf;
   buf.insert(buf.end(), {'B', 'T', 'S', '-', 'B', 'P', '+', '-', 'V', '-', 'v', '1'});
   for (const Point& p : V) append_point(buf, p);
   return hash_bytes(buf);
}

Scalar initial_transcript() {
   const char* d = "BTS-BP+-TRANSCRIPT-v1";
   return Scalar::hash_to_scalar(reinterpret_cast<const uint8_t*>(d), std::strlen(d));
}

const std::vector<Point>& Gi() {
   static std::vector<Point> g = [] {
      std::vector<Point> v(NBITS * MAX_M);
      for (size_t i = 0; i < v.size(); ++i) {
         std::vector<uint8_t> buf = {'B', 'T', 'S', '-', 'B', 'P', '+', '-', 'G', 'i', '-', 'v', '1'};
         buf.push_back(static_cast<uint8_t>(i));
         buf.push_back(static_cast<uint8_t>(i >> 8));
         buf.push_back(static_cast<uint8_t>(i >> 16));
         buf.push_back(static_cast<uint8_t>(i >> 24));
         v[i] = Point::hash_to_point(buf.data(), buf.size());
      }
      return v;
   }();
   return g;
}

const std::vector<Point>& Hi() {
   static std::vector<Point> h = [] {
      std::vector<Point> v(NBITS * MAX_M);
      for (size_t i = 0; i < v.size(); ++i) {
         std::vector<uint8_t> buf = {'B', 'T', 'S', '-', 'B', 'P', '+', '-', 'H', 'i', '-', 'v', '1'};
         buf.push_back(static_cast<uint8_t>(i));
         buf.push_back(static_cast<uint8_t>(i >> 8));
         buf.push_back(static_cast<uint8_t>(i >> 16));
         buf.push_back(static_cast<uint8_t>(i >> 24));
         v[i] = Point::hash_to_point(buf.data(), buf.size());
      }
      return v;
   }();
   return h;
}

size_t aggregation_log(size_t count, size_t& M) {
   size_t logM = 0;
   for (; (M = size_t{1} << logM) <= MAX_M && M < count; ++logM) {
   }
   if (M > MAX_M || M < count) throw std::runtime_error("too many outputs for Bulletproofs+");
   return logM;
}

std::vector<Scalar> powers(const Scalar& x, size_t n) {
   std::vector<Scalar> r(n);
   r[0] = Scalar::one();
   for (size_t i = 1; i < n; ++i) r[i] = r[i - 1] * x;
   return r;
}

Scalar sum_powers(const Scalar& x, size_t n) {
   // x^1 + ... + x^n
   Scalar sum = Scalar::zero();
   Scalar p = x;
   for (size_t i = 1; i <= n; ++i) {
      sum = sum + p;
      p = p * x;
   }
   return sum;
}

Scalar sum_even_powers(const Scalar& x, size_t n) {
   // x^2 + x^4 + ... + x^n, n power of two
   Scalar sum = Scalar::zero();
   Scalar p = x * x;
   for (size_t e = 2; e <= n; e += 2) {
      sum = sum + p;
      p = p * (x * x);
   }
   return sum;
}

Scalar weighted_inner(const std::vector<Scalar>& a, size_t a0,
                      const std::vector<Scalar>& b, size_t b0,
                      size_t n, const Scalar& y) {
   Scalar res = Scalar::zero();
   Scalar y_power = Scalar::one();
   for (size_t i = 0; i < n; ++i) {
      y_power = y_power * y;
      res = res + (a[a0 + i] * b[b0 + i] * y_power);
   }
   return res;
}

Point vector_exponent(const std::vector<Scalar>& a, const std::vector<Scalar>& b) {
   std::vector<std::pair<Scalar, Point>> terms;
   terms.reserve(a.size() * 2);
   const auto& G = Gi();
   const auto& H = Hi();
   for (size_t i = 0; i < a.size(); ++i) {
      terms.emplace_back(a[i], G[i]);
      terms.emplace_back(b[i], H[i]);
   }
   return Point::multiexp(terms);
}

Point compute_LR(size_t size, const Scalar& yweight,
                 const std::vector<Point>& G, size_t G0,
                 const std::vector<Point>& H, size_t H0,
                 const std::vector<Scalar>& a, size_t a0,
                 const std::vector<Scalar>& b, size_t b0,
                 const Scalar& c, const Scalar& d) {
   std::vector<std::pair<Scalar, Point>> terms;
   terms.reserve(size * 2 + 2);
   for (size_t i = 0; i < size; ++i) {
      terms.emplace_back(a[a0 + i] * yweight, G[G0 + i]);
      terms.emplace_back(b[b0 + i], H[H0 + i]);
   }
   terms.emplace_back(c, Point::value_generator());
   terms.emplace_back(d, Point::generator());
   return Point::multiexp(terms);
}

void hadamard_fold(std::vector<Point>& v, const Scalar& a, const Scalar& b) {
   const size_t sz = v.size() / 2;
   for (size_t n = 0; n < sz; ++n)
      v[n] = v[n] * a + v[sz + n] * b;
   v.resize(sz);
}

std::vector<Scalar> scaled(const std::vector<Scalar>& a, size_t begin, size_t n, const Scalar& x) {
   std::vector<Scalar> r(n);
   for (size_t i = 0; i < n; ++i) r[i] = a[begin + i] * x;
   return r;
}

bool nonzero_point(const Point& p) {
   return !p.is_infinity();
}

struct ProofData {
   Scalar y, z, e;
   std::vector<Scalar> challenges;
   size_t logM = 0;
};

bool build_challenges(const BulletproofPlus& proof, const std::vector<Point>& V, ProofData& pd) {
   if (V.empty() || V.size() > MAX_M) return false;
   if (proof.L.size() != proof.R.size() || proof.L.empty()) return false;
   if (proof.r1.is_zero() && proof.s1.is_zero() && proof.d1.is_zero()) {
      // all-zero response is not a valid proof; still allow individual zeros
   }
   for (const Point& p : proof.L)
      if (!nonzero_point(p)) return false;
   for (const Point& p : proof.R)
      if (!nonzero_point(p)) return false;
   if (!nonzero_point(proof.A) || !nonzero_point(proof.A1) || !nonzero_point(proof.B)) return false;

   size_t M = 0;
   try {
      pd.logM = aggregation_log(V.size(), M);
   } catch (...) {
      return false;
   }
   const size_t rounds = pd.logM + 6;
   if (proof.L.size() != rounds) return false;

   Scalar transcript = initial_transcript();
   transcript_update(transcript, hash_commitments(V));
   pd.y = transcript_update(transcript, proof.A);
   if (pd.y.is_zero()) return false;
   pd.z = Scalar::hash_to_scalar([&] {
      uint8_t b[32];
      pd.y.to_bytes(b);
      return std::vector<uint8_t>(b, b + 32);
   }().data(), 32);
   // hash_to_scalar of the 32-byte scalar, matching hash(y) alone
   if (pd.z.is_zero()) return false;
   transcript = pd.z;

   pd.challenges.resize(rounds);
   for (size_t j = 0; j < rounds; ++j) {
      pd.challenges[j] = transcript_update(transcript, proof.L[j], proof.R[j]);
      if (pd.challenges[j].is_zero()) return false;
   }
   pd.e = transcript_update(transcript, proof.A1, proof.B);
   if (pd.e.is_zero()) return false;
   return true;
}

} // namespace

BulletproofPlus bulletproof_plus_prove(const std::vector<uint64_t>& values,
                                       const std::vector<Scalar>& blinds) {
   if (values.empty() || values.size() != blinds.size())
      throw std::runtime_error("Bulletproofs+ input size");
   if (values.size() > MAX_M) throw std::runtime_error("Bulletproofs+ too many outputs");

   size_t M = 0;
   const size_t logM = aggregation_log(values.size(), M);
   const size_t logMN = logM + 6;
   const size_t MN = M * NBITS;

   std::vector<Point> V(values.size());
   for (size_t i = 0; i < values.size(); ++i) {
      V[i] = Point::commit(blinds[i], values[i]);
      if (V[i].is_infinity()) throw std::runtime_error("commitment at infinity");
   }

   std::vector<Scalar> aL(MN, Scalar::zero()), aR(MN, Scalar::zero());
   for (size_t j = 0; j < M; ++j) {
      for (size_t i = 0; i < NBITS; ++i) {
         // aL = bit, aR = bit - 1. Arithmetic, so the secret bit is not a branch.
         // Padding past values.size() is public and contributes bit 0.
         const uint64_t bit = (j < values.size()) ? ((values[j] >> i) & 1ull) : 0ull;
         aL[j * NBITS + i] = Scalar::from_u64(bit);
         aR[j * NBITS + i] = aL[j * NBITS + i] - Scalar::one();
      }
   }

   for (int attempt = 0; attempt < 8; ++attempt) {
      Scalar transcript = initial_transcript();
      transcript_update(transcript, hash_commitments(V));

      const Scalar alpha = Scalar::random_nonzero();
      Point A = vector_exponent(aL, aR) + Point::generator() * alpha;
      if (A.is_infinity()) continue;

      Scalar y = transcript_update(transcript, A);
      if (y.is_zero()) continue;
      Scalar z = Scalar::hash_to_scalar([&] {
         uint8_t b[32];
         y.to_bytes(b);
         return std::vector<uint8_t>(b, b + 32);
      }().data(), 32);
      if (z.is_zero()) continue;
      transcript = z;
      const Scalar z2 = z * z;

      std::vector<Scalar> d(MN, Scalar::zero());
      d[0] = z2;
      for (size_t i = 1; i < NBITS; ++i) d[i] = d[i - 1] * Scalar::from_u64(2);
      for (size_t j = 1; j < M; ++j)
         for (size_t i = 0; i < NBITS; ++i)
            d[j * NBITS + i] = d[(j - 1) * NBITS + i] * z2;

      const std::vector<Scalar> y_powers = powers(y, MN + 2);
      std::vector<Scalar> aL1(MN), aR1(MN);
      for (size_t i = 0; i < MN; ++i) {
         aL1[i] = aL[i] - z;
         aR1[i] = aR[i] + z + d[i] * y_powers[MN - i];
      }

      Scalar alpha1 = alpha;
      Scalar temp = Scalar::one();
      const Scalar y_MN_1 = y_powers[MN + 1];
      for (size_t j = 0; j < values.size(); ++j) {
         temp = temp * z2;
         alpha1 = alpha1 + (y_MN_1 * temp) * blinds[j];
      }

      const Scalar yinv = y.inverse();
      std::vector<Point> Gprime(Gi().begin(), Gi().begin() + static_cast<std::ptrdiff_t>(MN));
      std::vector<Point> Hprime(Hi().begin(), Hi().begin() + static_cast<std::ptrdiff_t>(MN));
      std::vector<Scalar> aprime = aL1;
      std::vector<Scalar> bprime = aR1;
      std::vector<Scalar> yinvpow(MN);
      yinvpow[0] = Scalar::one();
      for (size_t i = 1; i < MN; ++i) yinvpow[i] = yinvpow[i - 1] * yinv;

      std::vector<Point> L(logMN), R(logMN);
      size_t nprime = MN;
      bool restart = false;
      for (size_t round = 0; round < logMN; ++round) {
         nprime /= 2;
         const Scalar cL = weighted_inner(aprime, 0, bprime, nprime, nprime, y);
         std::vector<Scalar> a_right = scaled(aprime, nprime, nprime, y_powers[nprime]);
         const Scalar cR = weighted_inner(a_right, 0, bprime, 0, nprime, y);
         const Scalar dL = Scalar::random_nonzero();
         const Scalar dR = Scalar::random_nonzero();
         L[round] = compute_LR(nprime, yinvpow[nprime], Gprime, nprime, Hprime, 0,
                               aprime, 0, bprime, nprime, cL, dL);
         R[round] = compute_LR(nprime, y_powers[nprime], Gprime, 0, Hprime, nprime,
                               aprime, nprime, bprime, 0, cR, dR);
         if (L[round].is_infinity() || R[round].is_infinity()) {
            restart = true;
            break;
         }
         const Scalar challenge = transcript_update(transcript, L[round], R[round]);
         if (challenge.is_zero()) {
            restart = true;
            break;
         }
         const Scalar challenge_inv = challenge.inverse();
         const Scalar g_right = yinvpow[nprime] * challenge;
         hadamard_fold(Gprime, challenge_inv, g_right);
         hadamard_fold(Hprime, challenge, challenge_inv);

         const Scalar a_right_w = challenge_inv * y_powers[nprime];
         std::vector<Scalar> a_new(nprime), b_new(nprime);
         for (size_t i = 0; i < nprime; ++i) {
            a_new[i] = aprime[i] * challenge + aprime[nprime + i] * a_right_w;
            b_new[i] = bprime[i] * challenge_inv + bprime[nprime + i] * challenge;
         }
         aprime.swap(a_new);
         bprime.swap(b_new);
         alpha1 = alpha1 + dL * (challenge * challenge) + dR * (challenge_inv * challenge_inv);
      }
      if (restart || aprime.size() != 1) continue;

      const Scalar r = Scalar::random_nonzero();
      const Scalar s = Scalar::random_nonzero();
      const Scalar d_ = Scalar::random_nonzero();
      const Scalar eta = Scalar::random_nonzero();

      const Scalar a1_h = (r * y * bprime[0]) + (s * y * aprime[0]);
      Point A1 = Point::multiexp({
         {r, Gprime[0]},
         {s, Hprime[0]},
         {d_, Point::generator()},
         {a1_h, Point::value_generator()},
      });
      Point B = Point::generator() * eta + Point::value_generator() * (r * y * s);
      if (A1.is_infinity() || B.is_infinity()) continue;

      const Scalar e = transcript_update(transcript, A1, B);
      if (e.is_zero()) continue;
      const Scalar e2 = e * e;

      BulletproofPlus proof;
      proof.A = A;
      proof.A1 = A1;
      proof.B = B;
      proof.r1 = aprime[0] * e + r;
      proof.s1 = bprime[0] * e + s;
      proof.d1 = d_ * e + eta + alpha1 * e2;
      proof.L = std::move(L);
      proof.R = std::move(R);
      return proof;
   }
   throw std::runtime_error("Bulletproofs+ prove failed");
}

bool bulletproof_plus_verify_batch(const std::vector<const BulletproofPlus*>& proofs,
                                   const std::vector<const std::vector<Point>*>& commitments,
                                   const std::vector<Scalar>& weights) {
   if (proofs.empty() || proofs.size() != commitments.size()) return false;
   if (!weights.empty() && weights.size() != proofs.size()) return false;

   std::vector<ProofData> data(proofs.size());
   size_t max_rounds = 0;
   for (size_t i = 0; i < proofs.size(); ++i) {
      if (!proofs[i] || !commitments[i]) return false;
      if (!build_challenges(*proofs[i], *commitments[i], data[i])) return false;
      max_rounds = std::max(max_rounds, proofs[i]->L.size());
   }
   const size_t maxMN = size_t{1} << max_rounds;

   Scalar G_scalar = Scalar::zero();
   Scalar H_scalar = Scalar::zero();
   std::vector<Scalar> Gi_scalars(maxMN, Scalar::zero());
   std::vector<Scalar> Hi_scalars(maxMN, Scalar::zero());
   std::vector<std::pair<Scalar, Point>> terms;

   for (size_t pi = 0; pi < proofs.size(); ++pi) {
      const BulletproofPlus& proof = *proofs[pi];
      const std::vector<Point>& V = *commitments[pi];
      const ProofData& pd = data[pi];
      const size_t M = size_t{1} << pd.logM;
      const size_t MN = M * NBITS;
      const size_t rounds = proof.L.size();

      Scalar weight = weights.empty() ? Scalar::one() : weights[pi];
      if (weight.is_zero()) return false;

      const Scalar y_MN = [&] {
         Scalar y = pd.y;
         size_t t = MN;
         while (t > 1) {
            y = y * y;
            t /= 2;
         }
         return y;
      }();
      const Scalar y_MN_1 = y_MN * pd.y;
      const Scalar e2 = pd.e * pd.e;
      const Scalar z2 = pd.z * pd.z;

      Scalar vcoeff = (Scalar::zero() - e2) * y_MN_1 * weight;
      for (size_t j = 0; j < V.size(); ++j) {
         vcoeff = vcoeff * z2;
         terms.emplace_back(vcoeff, V[j]);
      }

      const Scalar minus_weight = weight.negate();
      terms.emplace_back(minus_weight, proof.B);
      const Scalar minus_weight_e = minus_weight * pd.e;
      terms.emplace_back(minus_weight_e, proof.A1);
      const Scalar minus_weight_e2 = minus_weight_e * pd.e;
      terms.emplace_back(minus_weight_e2, proof.A);

      G_scalar = G_scalar + weight * proof.d1;

      std::vector<Scalar> d(MN, Scalar::zero());
      d[0] = z2;
      for (size_t i = 1; i < NBITS; ++i) d[i] = d[i - 1] + d[i - 1];
      for (size_t j = 1; j < M; ++j)
         for (size_t i = 0; i < NBITS; ++i)
            d[j * NBITS + i] = d[(j - 1) * NBITS + i] * z2;

      const Scalar sum_d = (Scalar::from_u64(UINT64_MAX)) * sum_even_powers(pd.z, 2 * M);
      const Scalar sum_y = sum_powers(pd.y, MN);
      Scalar h_inner = (z2 - pd.z) * sum_y;
      h_inner = h_inner + y_MN_1 * pd.z * sum_d;
      h_inner = h_inner * e2;
      h_inner = h_inner + proof.r1 * pd.y * proof.s1;
      H_scalar = H_scalar + h_inner * weight;

      std::vector<Scalar> ch_inv(rounds);
      for (size_t j = 0; j < rounds; ++j) ch_inv[j] = pd.challenges[j].inverse();
      const Scalar yinv = pd.y.inverse();

      std::vector<Scalar> cache(size_t{1} << rounds);
      cache[0] = ch_inv[0];
      cache[1] = pd.challenges[0];
      for (size_t j = 1; j < rounds; ++j) {
         const size_t slots = size_t{1} << (j + 1);
         size_t s = slots;
         while (s-- > 0) {
            cache[s] = cache[s / 2] * pd.challenges[j];
            cache[s - 1] = cache[s / 2] * ch_inv[j];
            --s;
         }
      }

      Scalar e_r1_w_y = pd.e * proof.r1 * weight;
      const Scalar e_s1_w = pd.e * proof.s1 * weight;
      const Scalar e2_z_w = e2 * pd.z * weight;
      const Scalar minus_e2_z_w = e2_z_w.negate();
      Scalar minus_e2_w_y = (e2 * weight * y_MN).negate();
      for (size_t i = 0; i < MN; ++i) {
         const Scalar g_scalar = e_r1_w_y * cache[i] + e2_z_w;
         Scalar h_scalar = e_s1_w * cache[(~i) & (MN - 1)] + minus_e2_z_w;
         h_scalar = h_scalar + minus_e2_w_y * d[i];
         Gi_scalars[i] = Gi_scalars[i] + g_scalar;
         Hi_scalars[i] = Hi_scalars[i] + h_scalar;
         e_r1_w_y = e_r1_w_y * yinv;
         minus_e2_w_y = minus_e2_w_y * yinv;
      }

      for (size_t j = 0; j < rounds; ++j) {
         terms.emplace_back((pd.challenges[j] * pd.challenges[j]) * minus_weight_e2, proof.L[j]);
         terms.emplace_back((ch_inv[j] * ch_inv[j]) * minus_weight_e2, proof.R[j]);
      }
   }

   terms.emplace_back(G_scalar, Point::generator());
   terms.emplace_back(H_scalar, Point::value_generator());
   for (size_t i = 0; i < maxMN; ++i) {
      if (!Gi_scalars[i].is_zero()) terms.emplace_back(Gi_scalars[i], Gi()[i]);
      if (!Hi_scalars[i].is_zero()) terms.emplace_back(Hi_scalars[i], Hi()[i]);
   }
   return Point::multiexp(terms).is_infinity();
}

bool bulletproof_plus_verify(const BulletproofPlus& proof, const std::vector<Point>& commitments) {
   const BulletproofPlus* p = &proof;
   const std::vector<Point>* c = &commitments;
   return bulletproof_plus_verify_batch({p}, {c}, {});
}

std::vector<uint8_t> bulletproof_plus_serialize(const BulletproofPlus& proof) {
   if (proof.L.size() != proof.R.size() || proof.L.size() > 10 || proof.L.empty())
      throw std::runtime_error("serialize proof");
   std::vector<uint8_t> o;
   o.push_back('B');
   o.push_back('P');
   o.push_back('+');
   o.push_back(0x01);
   auto put_p = [&](const Point& p) {
      uint8_t b[33];
      p.to_compressed(b);
      o.insert(o.end(), b, b + 33);
   };
   auto put_s = [&](const Scalar& s) {
      uint8_t b[32];
      s.to_bytes(b);
      o.insert(o.end(), b, b + 32);
   };
   put_p(proof.A);
   put_p(proof.A1);
   put_p(proof.B);
   put_s(proof.r1);
   put_s(proof.s1);
   put_s(proof.d1);
   o.push_back(static_cast<uint8_t>(proof.L.size()));
   for (const Point& p : proof.L) put_p(p);
   for (const Point& p : proof.R) put_p(p);
   return o;
}

bool bulletproof_plus_parse(const uint8_t* p, size_t n, BulletproofPlus& out) {
   if (!p || n < 5) return false;
   if (p[0] != 'B' || p[1] != 'P' || p[2] != '+' || p[3] != 0x01) return false;
   const size_t rounds_at = 4 + 33 * 3 + 32 * 3;
   if (n < rounds_at + 1) return false;
   const size_t rounds = p[rounds_at];
   if (rounds == 0 || rounds > 10) return false;
   const size_t need = rounds_at + 1 + rounds * 33 * 2;
   if (n != need) return false;
   size_t off = 4;
   auto take_p = [&](Point& dst) -> bool {
      if (!Point::from_compressed(p + off, 33, dst)) return false;
      off += 33;
      return true;
   };
   auto take_s = [&](Scalar& dst) -> bool {
      if (!Scalar::from_bytes(p + off, 32, dst)) return false;
      off += 32;
      return true;
   };
   if (!take_p(out.A) || !take_p(out.A1) || !take_p(out.B)) return false;
   if (!take_s(out.r1) || !take_s(out.s1) || !take_s(out.d1)) return false;
   off += 1;
   out.L.resize(rounds);
   out.R.resize(rounds);
   for (size_t i = 0; i < rounds; ++i)
      if (!take_p(out.L[i])) return false;
   for (size_t i = 0; i < rounds; ++i)
      if (!take_p(out.R[i])) return false;
   return off == n;
}

} // namespace btsct
