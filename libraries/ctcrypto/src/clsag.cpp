/*
 * CLSAG, adapted from Monero src/ringct/rctSigs.cpp (BSD-3-Clause,
 * Copyright (c) 2016-2022 Monero Research Labs / The Monero Project).
 * secp256k1 cofactor is 1, so key images are not multiplied by 8 or 1/8.
 */
#include <bts_ct/clsag.hpp>

#include <openssl/sha.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace btsct {
namespace {

void append_point(std::vector<uint8_t>& buf, const Point& p) {
   uint8_t b[33];
   p.to_compressed(b);
   buf.push_back(0x02);
   buf.insert(buf.end(), b, b + 33);
}

Scalar hash_buf(const std::vector<uint8_t>& buf) {
   return Scalar::hash_to_scalar(buf.data(), buf.size());
}

Point hash_to_key_point(const Point& spend) {
   std::vector<uint8_t> buf = {'B', 'T', 'S', '-', 'C', 'L', 'S', 'A', 'G', '-', 'H', 'p', '-', 'v', '1'};
   append_point(buf, spend);
   return Point::hash_to_point(buf.data(), buf.size());
}

void append_ring(std::vector<uint8_t>& buf,
                 const std::vector<Point>& P,
                 const std::vector<Point>& C) {
   for (const Point& p : P) append_point(buf, p);
   for (const Point& p : C) append_point(buf, p);
}

Scalar mu_hash(const char* domain, size_t domain_len,
               const std::vector<Point>& P,
               const std::vector<Point>& C,
               const Point& I, const Point& D, const Point& Coff) {
   std::vector<uint8_t> buf(domain, domain + domain_len);
   append_ring(buf, P, C);
   append_point(buf, I);
   append_point(buf, D);
   append_point(buf, Coff);
   return hash_buf(buf);
}

Scalar round_hash(const std::vector<Point>& P,
                  const std::vector<Point>& C,
                  const Point& Coff,
                  const uint8_t message[32],
                  const Point& L, const Point& R) {
   const char* domain = "BTS-CLSAG-ROUND-v1";
   std::vector<uint8_t> buf(domain, domain + std::strlen(domain));
   append_ring(buf, P, C);
   append_point(buf, Coff);
   buf.push_back(0x01);
   buf.insert(buf.end(), message, message + 32);
   append_point(buf, L);
   append_point(buf, R);
   return hash_buf(buf);
}

bool canonical_sig(const ClsagSignature& sig, size_t n) {
   if (sig.s.size() != n || n == 0 || n > 64) return false;
   if (sig.I.is_infinity() || sig.D.is_infinity()) return false;
   if (sig.c1.is_zero()) return false;
   return true;
}

} // namespace

Point clsag_key_image(const Point& spend_pub, const Scalar& spend_secret) {
   return hash_to_key_point(spend_pub) * spend_secret;
}

ClsagSignature clsag_sign(const uint8_t message[32],
                          const std::vector<Point>& P,
                          const Scalar& spend_secret,
                          const std::vector<Point>& C_nonzero,
                          const Scalar& blind_diff,
                          const Point& pseudo_out,
                          size_t real_index) {
   const size_t n = P.size();
   if (!message || n == 0 || n != C_nonzero.size() || real_index >= n)
      throw std::runtime_error("CLSAG input");
   if (n > 64) throw std::runtime_error("CLSAG ring too large");

   std::vector<Point> C(n);
   for (size_t i = 0; i < n; ++i) C[i] = C_nonzero[i] - pseudo_out;

   const Point Hp = hash_to_key_point(P[real_index]);
   for (int attempt = 0; attempt < 8; ++attempt) {
      ClsagSignature sig;
      sig.I = Hp * spend_secret;
      const Point D_full = Hp * blind_diff;
      sig.D = D_full;
      if (sig.I.is_infinity() || sig.D.is_infinity()) continue;

      const Scalar a = Scalar::random_nonzero();
      const Point aG = Point::generator() * a;
      const Point aH = Hp * a;

      const char* d0 = "BTS-CLSAG-AGG-P-v1";
      const char* d1 = "BTS-CLSAG-AGG-C-v1";
      const Scalar mu_P = mu_hash(d0, std::strlen(d0), P, C_nonzero, sig.I, sig.D, pseudo_out);
      const Scalar mu_C = mu_hash(d1, std::strlen(d1), P, C_nonzero, sig.I, sig.D, pseudo_out);
      if (mu_P.is_zero() || mu_C.is_zero()) continue;

      Scalar c = round_hash(P, C_nonzero, pseudo_out, message, aG, aH);
      if (c.is_zero()) continue;

      sig.s.assign(n, Scalar::zero());
      size_t i = (real_index + 1) % n;
      if (i == 0) sig.c1 = c;
      bool bad = false;
      while (i != real_index) {
         sig.s[i] = Scalar::random_nonzero();
         const Scalar c_p = mu_P * c;
         const Scalar c_c = mu_C * c;
         const Point L = Point::generator() * sig.s[i] + P[i] * c_p + C[i] * c_c;
         const Point Hi = hash_to_key_point(P[i]);
         const Point R = Hi * sig.s[i] + sig.I * c_p + sig.D * c_c;
         if (L.is_infinity() || R.is_infinity()) {
            bad = true;
            break;
         }
         c = round_hash(P, C_nonzero, pseudo_out, message, L, R);
         if (c.is_zero()) {
            bad = true;
            break;
         }
         i = (i + 1) % n;
         if (i == 0) sig.c1 = c;
      }
      if (bad) continue;
      const Scalar t = mu_P * spend_secret + mu_C * blind_diff;
      const Scalar real_s = a - c * t;
      // Write every response slot. The real index is selected with a byte mask
      // so the store itself does not branch. The challenge walk still starts at
      // real_index + 1; see libraries/ctcrypto/doc/AUDIT.md.
      for (size_t j = 0; j < n; ++j) {
         uint64_t diff = static_cast<uint64_t>(j) ^ static_cast<uint64_t>(real_index);
         diff |= diff >> 32;
         diff |= diff >> 16;
         diff |= diff >> 8;
         diff |= diff >> 4;
         diff |= diff >> 2;
         diff |= diff >> 1;
         const uint8_t take = static_cast<uint8_t>((diff & 1u) ^ 1u);
         sig.s[j].cmov(real_s, take);
      }
      return sig;
   }
   throw std::runtime_error("CLSAG sign failed");
}

bool clsag_verify(const uint8_t message[32],
                  const ClsagSignature& sig,
                  const std::vector<Point>& P,
                  const std::vector<Point>& C_nonzero,
                  const Point& pseudo_out) {
   if (!message) return false;
   const size_t n = P.size();
   if (!canonical_sig(sig, n) || C_nonzero.size() != n) return false;
   for (const Point& p : P)
      if (p.is_infinity()) return false;
   for (const Point& p : C_nonzero)
      if (p.is_infinity()) return false;
   if (pseudo_out.is_infinity()) return false;

   std::vector<Point> C(n);
   for (size_t i = 0; i < n; ++i) C[i] = C_nonzero[i] - pseudo_out;

   const char* d0 = "BTS-CLSAG-AGG-P-v1";
   const char* d1 = "BTS-CLSAG-AGG-C-v1";
   const Scalar mu_P = mu_hash(d0, std::strlen(d0), P, C_nonzero, sig.I, sig.D, pseudo_out);
   const Scalar mu_C = mu_hash(d1, std::strlen(d1), P, C_nonzero, sig.I, sig.D, pseudo_out);
   if (mu_P.is_zero() || mu_C.is_zero()) return false;

   Scalar c = sig.c1;
   for (size_t i = 0; i < n; ++i) {
      const Scalar c_p = mu_P * c;
      const Scalar c_c = mu_C * c;
      const Point L = Point::generator() * sig.s[i] + P[i] * c_p + C[i] * c_c;
      const Point Hi = hash_to_key_point(P[i]);
      const Point R = Hi * sig.s[i] + sig.I * c_p + sig.D * c_c;
      if (L.is_infinity() || R.is_infinity()) return false;
      c = round_hash(P, C_nonzero, pseudo_out, message, L, R);
      if (c.is_zero()) return false;
   }
   return c == sig.c1;
}

std::vector<uint8_t> clsag_serialize(const ClsagSignature& sig) {
   if (sig.s.empty() || sig.s.size() > 64) throw std::runtime_error("clsag serialize");
   std::vector<uint8_t> o;
   o.push_back('C');
   o.push_back('L');
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
   put_p(sig.I);
   put_p(sig.D);
   put_s(sig.c1);
   o.push_back(static_cast<uint8_t>(sig.s.size()));
   for (const Scalar& s : sig.s) put_s(s);
   return o;
}

bool clsag_parse(const uint8_t* p, size_t n, ClsagSignature& out) {
   if (!p || n < 3 + 33 + 33 + 32 + 1) return false;
   if (p[0] != 'C' || p[1] != 'L' || p[2] != 0x01) return false;
   size_t off = 3;
   if (!Point::from_compressed(p + off, 33, out.I)) return false;
   off += 33;
   if (!Point::from_compressed(p + off, 33, out.D)) return false;
   off += 33;
   if (!Scalar::from_bytes(p + off, 32, out.c1)) return false;
   off += 32;
   if (off >= n) return false;
   const size_t count = p[off++];
   if (count == 0 || count > 64) return false;
   if (n != off + count * 32) return false;
   out.s.resize(count);
   for (size_t i = 0; i < count; ++i) {
      if (!Scalar::from_bytes(p + off, 32, out.s[i])) return false;
      off += 32;
   }
   return true;
}

} // namespace btsct
