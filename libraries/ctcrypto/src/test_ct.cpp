#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const char* msg) {
   if (!cond) {
      std::cerr << "FAIL " << msg << "\n";
      ++failures;
   } else {
      std::cout << "ok   " << msg << "\n";
   }
}

std::vector<btsct::Point> commits(const std::vector<uint64_t>& v, const std::vector<btsct::Scalar>& b) {
   std::vector<btsct::Point> c;
   for (size_t i = 0; i < v.size(); ++i) c.push_back(btsct::Point::commit(b[i], v[i]));
   return c;
}

void test_pedersen() {
   auto blind = btsct::Scalar::from_u64(7);
   auto c0 = btsct::Point::commit(blind, 0);
   auto c1 = btsct::Point::commit(btsct::Scalar::zero(), 1);
   expect(c0 == btsct::Point::generator() * blind, "commit(blind,0) == blind*G");
   expect(c1 == btsct::Point::value_generator(), "commit(0,1) == H");
   auto c = btsct::Point::commit(blind, 5);
   expect(c == btsct::Point::generator() * blind + btsct::Point::value_generator() * btsct::Scalar::from_u64(5),
          "commit splits across G and H");
   uint8_t comp[33];
   c1.to_compressed(comp);
   btsct::Point back;
   expect(btsct::Point::from_compressed(comp, 33, back) && back == c1, "H roundtrip");
   comp[0] = 0x00;
   expect(!btsct::Point::from_compressed(comp, 33, back), "reject illegal point prefix");
   expect(!btsct::Point::from_compressed(comp + 1, 32, back), "reject truncated point");
}

void test_range(const std::vector<uint64_t>& values, const char* name) {
   std::vector<btsct::Scalar> blinds;
   for (size_t i = 0; i < values.size(); ++i) blinds.push_back(btsct::Scalar::random_nonzero());
   auto proof = btsct::bulletproof_plus_prove(values, blinds);
   auto V = commits(values, blinds);
   expect(btsct::bulletproof_plus_verify(proof, V), name);
   auto bytes = btsct::bulletproof_plus_serialize(proof);
   btsct::BulletproofPlus parsed;
   expect(btsct::bulletproof_plus_parse(bytes.data(), bytes.size(), parsed), std::string(name).append(" parse").c_str());
   expect(btsct::bulletproof_plus_verify(parsed, V), std::string(name).append(" parsed verify").c_str());
   if (!bytes.empty()) {
      auto bad = bytes;
      bad[20] ^= 0x01;
      btsct::BulletproofPlus junk;
      bool parsed_bad = btsct::bulletproof_plus_parse(bad.data(), bad.size(), junk);
      expect(!parsed_bad || !btsct::bulletproof_plus_verify(junk, V), std::string(name).append(" tamper").c_str());
   }
   expect(!btsct::bulletproof_plus_parse(bytes.data(), bytes.size() / 2, parsed),
          std::string(name).append(" truncated").c_str());
   expect(!btsct::bulletproof_plus_parse(nullptr, 0, parsed), std::string(name).append(" empty").c_str());
   if (V.size() == 1) {
      auto other = V;
      other[0] = btsct::Point::commit(blinds[0], values[0] == 0 ? 1 : values[0] - 1);
      expect(!btsct::bulletproof_plus_verify(proof, other), std::string(name).append(" wrong commitment").c_str());
   }
}

void test_batch_and_inflation() {
   std::vector<uint64_t> a{1, 2};
   std::vector<uint64_t> b{9};
   std::vector<btsct::Scalar> ba{btsct::Scalar::random_nonzero(), btsct::Scalar::random_nonzero()};
   std::vector<btsct::Scalar> bb{btsct::Scalar::random_nonzero()};
   auto pa = btsct::bulletproof_plus_prove(a, ba);
   auto pb = btsct::bulletproof_plus_prove(b, bb);
   auto Va = commits(a, ba);
   auto Vb = commits(b, bb);
   const btsct::BulletproofPlus* ps[2] = {&pa, &pb};
   const std::vector<btsct::Point>* cs[2] = {&Va, &Vb};
   expect(btsct::bulletproof_plus_verify_batch({ps, ps + 2}, {cs, cs + 2}, {}), "batch two valid");
   std::vector<btsct::Scalar> w{btsct::Scalar::from_u64(3), btsct::Scalar::from_u64(9)};
   expect(btsct::bulletproof_plus_verify_batch({ps, ps + 2}, {cs, cs + 2}, w), "batch weighted");
   auto Vbad = Vb;
   Vbad[0] = btsct::Point::commit(bb[0], 8);
   const std::vector<btsct::Point>* cbad[2] = {&Va, &Vbad};
   expect(!btsct::bulletproof_plus_verify_batch({ps, ps + 2}, {cbad, cbad + 2}, {}), "batch rejects swapped value");

   // A commitment to -1 cannot be explained by a proof of a small non-negative value.
   auto neg = btsct::Point::value_generator() * btsct::Scalar::minus_one() +
              btsct::Point::generator() * ba[0];
   std::vector<btsct::Point> forged{neg};
   expect(!btsct::bulletproof_plus_verify(pa, {Va[0]}), "single proof does not verify a different output set");
   expect(!btsct::bulletproof_plus_verify(pb, forged), "proof rejects commitment to -1");
}

void test_clsag() {
   const size_t n = 4;
   std::vector<btsct::Scalar> secrets;
   std::vector<btsct::Point> pubs;
   std::vector<btsct::Scalar> blinds;
   std::vector<btsct::Point> commits_v;
   const uint64_t amount = 50;
   for (size_t i = 0; i < n; ++i) {
      secrets.push_back(btsct::Scalar::random_nonzero());
      pubs.push_back(btsct::Point::generator() * secrets.back());
      blinds.push_back(btsct::Scalar::random_nonzero());
      commits_v.push_back(btsct::Point::commit(blinds.back(), amount + i));
   }
   const size_t real = 2;
   auto pseudo_blind = btsct::Scalar::random_nonzero();
   auto pseudo = btsct::Point::commit(pseudo_blind, amount + real);
   auto z = blinds[real] - pseudo_blind;
   uint8_t msg[32];
   for (int i = 0; i < 32; ++i) msg[i] = static_cast<uint8_t>(i + 3);
   auto sig = btsct::clsag_sign(msg, pubs, secrets[real], commits_v, z, pseudo, real);
   expect(btsct::clsag_verify(msg, sig, pubs, commits_v, pseudo), "clsag valid ring 4");
   auto again = btsct::clsag_sign(msg, pubs, secrets[real], commits_v, z, pseudo, real);
   expect(sig.I == again.I, "key image depends only on the spend key");
   msg[0] ^= 1;
   expect(!btsct::clsag_verify(msg, sig, pubs, commits_v, pseudo), "clsag rejects other message");
   msg[0] ^= 1;
   auto bad = sig;
   uint8_t sb[32];
   bad.s[0].to_bytes(sb);
   sb[31] ^= 0x02;
   expect(!btsct::Scalar::from_bytes(sb, 32, bad.s[0]) || !btsct::clsag_verify(msg, bad, pubs, commits_v, pseudo),
          "clsag rejects forged response");
   auto wrong_pseudo = btsct::Point::commit(pseudo_blind, amount + real + 1);
   expect(!btsct::clsag_verify(msg, sig, pubs, commits_v, wrong_pseudo), "clsag rejects wrong pseudo amount");
   std::vector<btsct::Point> other_asset = commits_v;
   other_asset[real] = btsct::Point::commit(blinds[real], amount + real + 7);
   expect(!btsct::clsag_verify(msg, sig, pubs, other_asset, pseudo), "clsag rejects substituted commitment");

   auto bytes = btsct::clsag_serialize(sig);
   btsct::ClsagSignature parsed;
   expect(btsct::clsag_parse(bytes.data(), bytes.size(), parsed) &&
             btsct::clsag_verify(msg, parsed, pubs, commits_v, pseudo),
          "clsag parse");
   expect(!btsct::clsag_parse(bytes.data(), 8, parsed), "clsag truncated");
   expect(!btsct::clsag_parse(bytes.data(), 0, parsed), "clsag empty");

   // Ring of 16, the consensus size, one honest member.
   const size_t N = 16;
   std::vector<btsct::Scalar> s16, b16;
   std::vector<btsct::Point> p16, c16;
   for (size_t i = 0; i < N; ++i) {
      s16.push_back(btsct::Scalar::random_nonzero());
      p16.push_back(btsct::Point::generator() * s16.back());
      b16.push_back(btsct::Scalar::random_nonzero());
      c16.push_back(btsct::Point::commit(b16.back(), 1000 + (i == 7 ? 0 : i)));
   }
   auto pb = btsct::Scalar::random_nonzero();
   auto pseudo16 = btsct::Point::commit(pb, 1000);
   auto z16 = b16[7] - pb;
   auto sig16 = btsct::clsag_sign(msg, p16, s16[7], c16, z16, pseudo16, 7);
   expect(btsct::clsag_verify(msg, sig16, p16, c16, pseudo16), "clsag ring 16");
   // Spending a different real index with the same signature must fail.
   expect(!btsct::clsag_verify(msg, sig16, p16, c16, btsct::Point::commit(pb, 1001)),
          "clsag ring 16 rejects other pseudo");
}

btsct::Point point_from_hex(const char* hex) {
   uint8_t raw[33];
   for (int i = 0; i < 33; ++i) {
      unsigned int byte = 0;
      std::sscanf(hex + 2 * i, "%02x", &byte);
      raw[i] = static_cast<uint8_t>(byte);
   }
   btsct::Point p;
   if (!btsct::Point::from_compressed(raw, 33, p)) throw std::runtime_error("vector point");
   return p;
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
   std::vector<uint8_t> out(hex.size() / 2);
   for (size_t i = 0; i < out.size(); ++i) {
      unsigned int byte = 0;
      std::sscanf(hex.c_str() + 2 * i, "%02x", &byte);
      out[i] = static_cast<uint8_t>(byte);
   }
   return out;
}

void test_locked_vector() {
   // Openings (blind, value) = (3, 1) and (9, 2). Commitments are deterministic.
   // The proof was produced by this prover and is a regression vector for the
   // transcript. Randomized provers may emit other valid proofs of the same openings.
   auto c0 = point_from_hex("0263ed256bcac0f48fabdd28184aaf0dc05c5d0019bf88ab78f842c437b5159830");
   auto c1 = point_from_hex("03c80827b14ea6b15ba26b5ff36c46013201582440417539a1914abe20fb574de7");
   expect(btsct::Point::commit(btsct::Scalar::from_u64(3), 1) == c0, "vector commitment 3,1");
   expect(btsct::Point::commit(btsct::Scalar::from_u64(9), 2) == c1, "vector commitment 9,2");
   const char* proof_hex =
      "42502b010283f1d32d6a7f437a9ce14dbce8581d5f406f38288843254bac61d961f9596d8f03496c0b47779691820ebd5e3d66894edc2870642fe3321d0899fe69cea141d26902523df5578a87db1e154fc959ff68f493e1915ddc6c394cb6039d1810d2623f0c0f393f64d67a057c711657e53fcd6ea9a1895efb764afdb4e632535bd8097eb60243ead7a4db8d56bf80cd0644772119452d8c8c095883cc818f1bb1e05cd5ce2e68313de1d60d11043ffc656f73e4281d900ad77dd2d86a15086904db7a35190703edd0e834e1116ff0a71749d4473ac8cd4928ed64e6851a58798676d739f2fc6a03c9036ac68f3a0d149ce4ddfd338ef9dfd2daef179162c98ec8eddb5ae898d8d7032553c64facdb9a022b3e3a14ad2e85c75c7c385380945a61ada0a4fcc8c7ac48030d4db44a67bc83798c89b9ef41705b963d0a618d7361c29dbe2257e8dccf274c03e31ad43a51301f6a27a8b989afa9eb043a1cce554896bbc3478b972364498da803be605ca541e388f46ed38ade78bbf407b4ff5b8f3f16bfd4b8e1826dfbbb789702e31a8c0c6f1a27b4753afa7ac43407ea33cbc27b2e9b04cbc932905c80fc2c450218b4a11433112ea94e58937d1286f80522a4f02bbf1f02d52f387f00e7300621028c5dd04f4e4e2022f5e74187313fb6f9e6216b7233a3524d9409da2f28dadb3802a1af41dc3b60d6670171658d3007dc855495035bb036f865e78bcc396fb7dcbf03941fd7719f7509a2fbfd1c5eec20ea3102fa44365c04e1156212e5a820ee8783038f3ecdaff3e3d33459cb31d653a8bb698e90d17c1efd364bec3b68c569f3f496021a3fea21247d2b56c56b66004901573b9f4fd21385a1916101dcae1ba5ef211403e84e220dcffe96e3d52d4dd93561148851712307e63c64e9a896f46763aff928";
   auto bytes = hex_to_bytes(proof_hex);
   btsct::BulletproofPlus proof;
   expect(btsct::bulletproof_plus_parse(bytes.data(), bytes.size(), proof), "vector proof parses");
   expect(btsct::bulletproof_plus_verify(proof, {c0, c1}), "vector proof verifies");
   bytes[40] ^= 0x01;
   btsct::BulletproofPlus bad;
   bool parsed = btsct::bulletproof_plus_parse(bytes.data(), bytes.size(), bad);
   expect(!parsed || !btsct::bulletproof_plus_verify(bad, {c0, c1}), "vector proof bitflip rejected");
}

void dump_vector() {
   std::vector<uint64_t> values{1, 2};
   std::vector<btsct::Scalar> blinds{btsct::Scalar::from_u64(3), btsct::Scalar::from_u64(9)};
   auto proof = btsct::bulletproof_plus_prove(values, blinds);
   auto bytes = btsct::bulletproof_plus_serialize(proof);
   auto V = commits(values, blinds);
   if (!btsct::bulletproof_plus_verify(proof, V)) expect(false, "vector proof");
   std::cout << "VECTOR_BP";
   for (uint8_t b : bytes) std::printf("%02x", b);
   std::cout << "\n";
   uint8_t c0[33], c1[33];
   V[0].to_compressed(c0);
   V[1].to_compressed(c1);
   std::cout << "VECTOR_C0";
   for (uint8_t b : c0) std::printf("%02x", b);
   std::cout << "\nVECTOR_C1";
   for (uint8_t b : c1) std::printf("%02x", b);
   std::cout << "\n";
}

void test_dos_timing() {
   std::vector<uint64_t> values{123456789ull};
   std::vector<btsct::Scalar> blinds{btsct::Scalar::random_nonzero()};
   auto proof = btsct::bulletproof_plus_prove(values, blinds);
   auto V = commits(values, blinds);
   const int N = 5;
   auto t0 = std::chrono::steady_clock::now();
   for (int i = 0; i < N; ++i)
      if (!btsct::bulletproof_plus_verify(proof, V)) expect(false, "timing verify");
   auto t1 = std::chrono::steady_clock::now();
   auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / N;
   std::cout << "timing bulletproofs+ verify 1x64-bit us=" << us
             << " bytes=" << btsct::bulletproof_plus_serialize(proof).size() << "\n";

   std::vector<uint64_t> many{1, 2, 3, 4};
   std::vector<btsct::Scalar> mb;
   for (int i = 0; i < 4; ++i) mb.push_back(btsct::Scalar::random_nonzero());
   auto mp = btsct::bulletproof_plus_prove(many, mb);
   auto MV = commits(many, mb);
   t0 = std::chrono::steady_clock::now();
   expect(btsct::bulletproof_plus_verify(mp, MV), "aggregate 4");
   t1 = std::chrono::steady_clock::now();
   std::cout << "timing bulletproofs+ verify 4-agg us="
             << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
             << " bytes=" << btsct::bulletproof_plus_serialize(mp).size() << "\n";
}

} // namespace

int main() {
   try {
      test_pedersen();
      test_range({0}, "range 0");
      test_range({1}, "range 1");
      test_range({2}, "range 2");
      test_range({1000}, "range 1000");
      test_range({UINT64_MAX}, "range 2^64-1");
      test_range({3, 9}, "range aggregate 2");
      test_batch_and_inflation();
      test_clsag();
      test_locked_vector();
      dump_vector();
      test_dos_timing();
   } catch (const std::exception& e) {
      std::cerr << "exception " << e.what() << "\n";
      return 1;
   }
   if (failures) {
      std::cerr << failures << " failure(s)\n";
      return 1;
   }
   std::cout << "all ctcrypto tests passed\n";
   return 0;
}
