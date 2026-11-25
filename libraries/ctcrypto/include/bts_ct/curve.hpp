/*
 * secp256k1 group operations for BitShares confidential transactions.
 *
 * Pedersen commitment is blind*G + value*H with the same generators as
 * secp256k1-zkp (libraries/fc/vendor/secp256k1-zkp, commit bd067945):
 *   G  = SEC2 secp256k1 generator
 *   H  = G2, SHA256 of the uncompressed generator, lifted to the curve
 *        (see group_impl.h, "Alternative generator for secp256k1").
 *
 * Group arithmetic is OpenSSL's libcrypto (Apache-2.0). This file does not
 * implement a new curve or a new commitment scheme.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

namespace btsct {

class Scalar {
public:
   Scalar();
   static Scalar zero();
   static Scalar one();
   static Scalar minus_one();
   static Scalar from_u64(uint64_t v);
   /** Exactly 32 bytes, big-endian, strictly less than the group order. */
   static bool from_bytes(const uint8_t* p, size_t n, Scalar& out);
   static Scalar random_nonzero();
   /** SHA-256(data) reduced once into the scalar field. May be zero. */
   static Scalar hash_to_scalar(const uint8_t* p, size_t n);

   void to_bytes(uint8_t out[32]) const;
   bool is_zero() const;

   Scalar operator+(const Scalar& o) const;
   Scalar operator-(const Scalar& o) const;
   Scalar operator*(const Scalar& o) const;
   Scalar negate() const;
   Scalar inverse() const;

   bool operator==(const Scalar& o) const;
   bool operator!=(const Scalar& o) const { return !(*this == o); }

   /** If flag is 1, replace this scalar with src. flag must be 0 or 1.
    *  The byte loop does not branch on the flag. */
   void cmov(const Scalar& src, uint8_t flag);

private:
   uint8_t b_[32];
};

class Point {
public:
   Point();
   Point(const Point&) = default;
   Point& operator=(const Point&) = default;

   static Point generator();
   static Point value_generator();
   static bool from_compressed(const uint8_t* p, size_t n, Point& out);
   /** Try-and-increment hash onto the prime-order subgroup (cofactor 1). */
   static Point hash_to_point(const uint8_t* p, size_t n);
   static Point commit(const Scalar& blind, uint64_t value);
   static Point multiexp(const std::vector<std::pair<Scalar, Point>>& terms);

   Point operator+(const Point& o) const;
   Point operator-(const Point& o) const;
   Point operator*(const Scalar& s) const;

   bool is_infinity() const;
   bool operator==(const Point& o) const;
   bool operator!=(const Point& o) const { return !(*this == o); }
   void to_compressed(uint8_t out[33]) const;

private:
   struct Rep;
   std::shared_ptr<Rep> rep_;
   explicit Point(Rep* owned);
};

} // namespace btsct
