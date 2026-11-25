/*
 * Copyright (c) 2026 the confidential-transfer upgrade authors.
 * Group law via OpenSSL. Generator H copied from secp256k1-zkp (MIT),
 * libraries/fc/vendor/secp256k1-zkp/src/group_impl.h.
 */
#include <bts_ct/curve.hpp>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace btsct {
namespace {

struct Bn {
   BIGNUM* n;
   Bn() : n(BN_new()) {
      if (!n) throw std::runtime_error("BN_new");
   }
   explicit Bn(const uint8_t* p, size_t len) : n(BN_bin2bn(p, static_cast<int>(len), nullptr)) {
      if (!n) throw std::runtime_error("BN_bin2bn");
   }
   ~Bn() { BN_free(n); }
   Bn(const Bn&) = delete;
   Bn& operator=(const Bn&) = delete;
   Bn(Bn&& o) noexcept : n(o.n) { o.n = nullptr; }
   Bn& operator=(Bn&& o) noexcept {
      if (this != &o) {
         BN_free(n);
         n = o.n;
         o.n = nullptr;
      }
      return *this;
   }
};

struct CtxHolder {
   BN_CTX* c;
   CtxHolder() : c(BN_CTX_new()) {
      if (!c) throw std::runtime_error("BN_CTX_new");
   }
   ~CtxHolder() { BN_CTX_free(c); }
};

BN_CTX* ctx() {
   thread_local CtxHolder holder;
   return holder.c;
}

const EC_GROUP* group() {
   static EC_GROUP* g = EC_GROUP_new_by_curve_name(NID_secp256k1);
   if (!g) throw std::runtime_error("secp256k1 group");
   return g;
}

const BIGNUM* order() {
   static const BIGNUM* n = EC_GROUP_get0_order(group());
   return n;
}

Bn scalar_bn(const Scalar& s) {
   uint8_t b[32];
   s.to_bytes(b);
   return Bn(b, 32);
}

Scalar scalar_from_bn(const BIGNUM* n) {
   uint8_t b[32] = {0};
   if (BN_is_negative(n)) throw std::runtime_error("negative scalar");
   if (BN_num_bytes(n) > 32) throw std::runtime_error("scalar overflow");
   if (BN_bn2binpad(n, b, 32) != 32) throw std::runtime_error("bn2binpad");
   Scalar out;
   if (!Scalar::from_bytes(b, 32, out)) throw std::runtime_error("scalar not reduced");
   return out;
}

Scalar reduce_hash(const uint8_t dig[32]) {
   Bn h(dig, 32);
   if (BN_cmp(h.n, order()) >= 0) {
      if (!BN_sub(h.n, h.n, order())) throw std::runtime_error("BN_sub");
   }
   return scalar_from_bn(h.n);
}

} // namespace

struct Point::Rep {
   EC_POINT* p;
   explicit Rep(EC_POINT* point) : p(point) {
      if (!p) throw std::runtime_error("EC_POINT");
   }
   ~Rep() { EC_POINT_free(p); }
};

Point::Point(Rep* owned) : rep_(owned) {}

Point::Point() : Point(new Rep(EC_POINT_new(group()))) {
   if (!EC_POINT_set_to_infinity(group(), rep_->p)) throw std::runtime_error("infinity");
}

Scalar::Scalar() { memset(b_, 0, 32); }

Scalar Scalar::zero() { return Scalar(); }

Scalar Scalar::one() { return from_u64(1); }

Scalar Scalar::minus_one() { return zero() - one(); }

Scalar Scalar::from_u64(uint64_t v) {
   uint8_t b[32] = {0};
   for (int i = 0; i < 8; ++i)
      b[31 - i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
   Scalar s;
   if (!from_bytes(b, 32, s)) throw std::runtime_error("from_u64");
   return s;
}

bool Scalar::from_bytes(const uint8_t* p, size_t n, Scalar& out) {
   if (!p || n != 32) return false;
   Bn h(p, 32);
   if (BN_cmp(h.n, order()) >= 0) return false;
   memcpy(out.b_, p, 32);
   return true;
}

void Scalar::to_bytes(uint8_t out[32]) const { memcpy(out, b_, 32); }

bool Scalar::is_zero() const {
   for (int i = 0; i < 32; ++i)
      if (b_[i]) return false;
   return true;
}

Scalar Scalar::random_nonzero() {
   for (int attempt = 0; attempt < 64; ++attempt) {
      uint8_t raw[64];
      if (RAND_bytes(raw, sizeof(raw)) != 1) throw std::runtime_error("RAND_bytes");
      Bn h(raw, sizeof(raw));
      Bn r;
      if (!BN_mod(r.n, h.n, order(), ctx())) throw std::runtime_error("BN_mod");
      if (BN_is_zero(r.n)) continue;
      return scalar_from_bn(r.n);
   }
   throw std::runtime_error("random scalar");
}

Scalar Scalar::hash_to_scalar(const uint8_t* p, size_t n) {
   uint8_t dig[32];
   SHA256(p, n, dig);
   return reduce_hash(dig);
}

Scalar Scalar::operator+(const Scalar& o) const {
   Bn a = scalar_bn(*this);
   Bn b = scalar_bn(o);
   Bn r;
   if (!BN_mod_add(r.n, a.n, b.n, order(), ctx())) throw std::runtime_error("add");
   return scalar_from_bn(r.n);
}

Scalar Scalar::operator-(const Scalar& o) const {
   Bn a = scalar_bn(*this);
   Bn b = scalar_bn(o);
   Bn r;
   if (!BN_mod_sub(r.n, a.n, b.n, order(), ctx())) throw std::runtime_error("sub");
   return scalar_from_bn(r.n);
}

Scalar Scalar::operator*(const Scalar& o) const {
   Bn a = scalar_bn(*this);
   Bn b = scalar_bn(o);
   Bn r;
   if (!BN_mod_mul(r.n, a.n, b.n, order(), ctx())) throw std::runtime_error("mul");
   return scalar_from_bn(r.n);
}

Scalar Scalar::negate() const { return zero() - *this; }

Scalar Scalar::inverse() const {
   if (is_zero()) throw std::runtime_error("invert zero");
   Bn a = scalar_bn(*this);
   Bn r;
   if (!BN_mod_inverse(r.n, a.n, order(), ctx())) throw std::runtime_error("inverse");
   return scalar_from_bn(r.n);
}

bool Scalar::operator==(const Scalar& o) const { return memcmp(b_, o.b_, 32) == 0; }

void Scalar::cmov(const Scalar& src, uint8_t flag) {
   const uint8_t mask = static_cast<uint8_t>(0u - static_cast<unsigned>(flag & 1u));
   for (int i = 0; i < 32; ++i)
      b_[i] = static_cast<uint8_t>((b_[i] & static_cast<uint8_t>(~mask)) | (src.b_[i] & mask));
}

Point Point::generator() {
   static Point g = [] {
      const EC_POINT* gen = EC_GROUP_get0_generator(group());
      EC_POINT* p = EC_POINT_dup(gen, group());
      return Point(new Rep(p));
   }();
   return g;
}

Point Point::value_generator() {
   // G2 from secp256k1-zkp group_impl.h (MIT). X then Y, big-endian.
   static const char* hx =
      "50929b74c1a04954b78b4b6035e97a5e078a5a0f28ec96d547bfee9ace803ac0";
   static const char* hy =
      "31d3c6863973926e049e637cb1b5f40a36dac28af1766968c30c2313f3a38904";
   static Point h = [] {
      Bn x;
      Bn y;
      if (!BN_hex2bn(&x.n, hx) || !BN_hex2bn(&y.n, hy))
         throw std::runtime_error("H hex");
      EC_POINT* p = EC_POINT_new(group());
      if (!EC_POINT_set_affine_coordinates(group(), p, x.n, y.n, ctx()))
         throw std::runtime_error("H not on curve");
      return Point(new Rep(p));
   }();
   return h;
}

bool Point::from_compressed(const uint8_t* p, size_t n, Point& out) {
   if (!p || n != 33) return false;
   if (p[0] != 0x02 && p[0] != 0x03) return false;
   EC_POINT* pt = EC_POINT_new(group());
   if (!pt) return false;
   if (!EC_POINT_oct2point(group(), pt, p, 33, ctx())) {
      EC_POINT_free(pt);
      return false;
   }
   if (EC_POINT_is_at_infinity(group(), pt)) {
      EC_POINT_free(pt);
      return false;
   }
   uint8_t again[33];
   if (EC_POINT_point2oct(group(), pt, POINT_CONVERSION_COMPRESSED, again, 33, ctx()) != 33 ||
       memcmp(again, p, 33) != 0) {
      EC_POINT_free(pt);
      return false;
   }
   out = Point(new Rep(pt));
   return true;
}

void Point::to_compressed(uint8_t out[33]) const {
   if (is_infinity()) throw std::runtime_error("compress infinity");
   if (EC_POINT_point2oct(group(), rep_->p, POINT_CONVERSION_COMPRESSED, out, 33, ctx()) != 33)
      throw std::runtime_error("point2oct");
}

Point Point::hash_to_point(const uint8_t* data, size_t n) {
   for (uint32_t counter = 0; counter < 256; ++counter) {
      std::vector<uint8_t> buf;
      buf.reserve(n + 4);
      buf.insert(buf.end(), data, data + n);
      buf.push_back(static_cast<uint8_t>(counter));
      buf.push_back(static_cast<uint8_t>(counter >> 8));
      buf.push_back(static_cast<uint8_t>(counter >> 16));
      buf.push_back(static_cast<uint8_t>(counter >> 24));
      uint8_t dig[32];
      SHA256(buf.data(), buf.size(), dig);
      for (uint8_t prefix : {uint8_t{0x02}, uint8_t{0x03}}) {
         uint8_t comp[33];
         comp[0] = prefix;
         memcpy(comp + 1, dig, 32);
         Point p;
         if (from_compressed(comp, 33, p)) return p;
      }
   }
   throw std::runtime_error("hash_to_point failed");
}

Point Point::commit(const Scalar& blind, uint64_t value) {
   Bn b = scalar_bn(blind);
   Bn v = scalar_bn(Scalar::from_u64(value));
   const EC_POINT* pts[1] = {value_generator().rep_->p};
   const BIGNUM* ms[1] = {v.n};
   EC_POINT* r = EC_POINT_new(group());
   if (!EC_POINTs_mul(group(), r, b.n, 1, pts, ms, ctx()))
      throw std::runtime_error("commit");
   return Point(new Rep(r));
}

Point Point::operator+(const Point& o) const {
   EC_POINT* r = EC_POINT_new(group());
   if (!EC_POINT_add(group(), r, rep_->p, o.rep_->p, ctx()))
      throw std::runtime_error("point add");
   return Point(new Rep(r));
}

Point Point::operator-(const Point& o) const {
   EC_POINT* neg = EC_POINT_dup(o.rep_->p, group());
   if (!neg || !EC_POINT_invert(group(), neg, ctx())) {
      EC_POINT_free(neg);
      throw std::runtime_error("invert");
   }
   EC_POINT* r = EC_POINT_new(group());
   bool ok = EC_POINT_add(group(), r, rep_->p, neg, ctx());
   EC_POINT_free(neg);
   if (!ok) throw std::runtime_error("point sub");
   return Point(new Rep(r));
}

Point Point::operator*(const Scalar& s) const {
   Bn k = scalar_bn(s);
   EC_POINT* r = EC_POINT_new(group());
   if (!EC_POINT_mul(group(), r, nullptr, rep_->p, k.n, ctx()))
      throw std::runtime_error("point mul");
   return Point(new Rep(r));
}

bool Point::is_infinity() const { return EC_POINT_is_at_infinity(group(), rep_->p) == 1; }

bool Point::operator==(const Point& o) const {
   return EC_POINT_cmp(group(), rep_->p, o.rep_->p, ctx()) == 0;
}

Point Point::multiexp(const std::vector<std::pair<Scalar, Point>>& terms) {
   std::vector<const EC_POINT*> pts;
   std::vector<Bn> storage;
   std::vector<const BIGNUM*> scalars;
   pts.reserve(terms.size());
   storage.reserve(terms.size());
   scalars.reserve(terms.size());
   for (const auto& t : terms) {
      if (t.first.is_zero() || t.second.is_infinity()) continue;
      storage.push_back(scalar_bn(t.first));
      pts.push_back(t.second.rep_->p);
      scalars.push_back(storage.back().n);
   }
   EC_POINT* r = EC_POINT_new(group());
   if (pts.empty()) {
      EC_POINT_set_to_infinity(group(), r);
   } else if (!EC_POINTs_mul(group(), r, nullptr, pts.size(), pts.data(), scalars.data(), ctx())) {
      EC_POINT_free(r);
      throw std::runtime_error("multiexp");
   }
   return Point(new Rep(r));
}

} // namespace btsct
