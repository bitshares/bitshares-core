// Exchange proofs with the Python reference. Local only.
#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int hex_nibble(char c) {
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}

std::vector<uint8_t> unhex(const std::string& s) {
   if (s.size() % 2) throw std::runtime_error("odd hex");
   std::vector<uint8_t> o(s.size() / 2);
   for (size_t i = 0; i < o.size(); ++i) {
      int hi = hex_nibble(s[2 * i]);
      int lo = hex_nibble(s[2 * i + 1]);
      if (hi < 0 || lo < 0) throw std::runtime_error("bad hex");
      o[i] = static_cast<uint8_t>((hi << 4) | lo);
   }
   return o;
}

std::string hex_of(const std::vector<uint8_t>& b) {
   static const char* k = "0123456789abcdef";
   std::string s;
   s.resize(b.size() * 2);
   for (size_t i = 0; i < b.size(); ++i) {
      s[2 * i] = k[b[i] >> 4];
      s[2 * i + 1] = k[b[i] & 0xf];
   }
   return s;
}

std::string hex_of(const uint8_t* p, size_t n) {
   return hex_of(std::vector<uint8_t>(p, p + n));
}

btsct::Scalar scalar_dec(const std::string& s) {
   unsigned long long v = std::stoull(s);
   return btsct::Scalar::from_u64(v);
}

std::vector<std::string> split(const std::string& s, char sep) {
   std::vector<std::string> o;
   std::string cur;
   for (char c : s) {
      if (c == sep) {
         if (!cur.empty()) o.push_back(cur);
         cur.clear();
      } else if (c != ' ' && c != '\n' && c != '\r') {
         cur.push_back(c);
      }
   }
   if (!cur.empty()) o.push_back(cur);
   return o;
}

int prove_bp(const std::string& values, const std::string& blinds) {
   std::vector<uint64_t> vs;
   std::vector<btsct::Scalar> bs;
   for (const auto& p : split(values, ',')) vs.push_back(std::stoull(p));
   for (const auto& p : split(blinds, ',')) bs.push_back(scalar_dec(p));
   auto proof = btsct::bulletproof_plus_prove(vs, bs);
   auto bytes = btsct::bulletproof_plus_serialize(proof);
   std::cout << "PROOF " << hex_of(bytes) << "\n";
   for (size_t i = 0; i < vs.size(); ++i) {
      btsct::Point c = btsct::Point::commit(bs[i], vs[i]);
      uint8_t raw[33];
      c.to_compressed(raw);
      std::cout << "COMMIT " << hex_of(raw, 33) << "\n";
   }
   return 0;
}

int verify_bp() {
   std::string line, proof_hex;
   std::vector<btsct::Point> commits;
   while (std::getline(std::cin, line)) {
      if (line.rfind("PROOF ", 0) == 0) proof_hex = line.substr(6);
      else if (line.rfind("COMMIT ", 0) == 0) {
         auto raw = unhex(line.substr(7));
         btsct::Point p;
         if (!btsct::Point::from_compressed(raw.data(), raw.size(), p)) return 2;
         commits.push_back(p);
      }
   }
   auto raw = unhex(proof_hex);
   btsct::BulletproofPlus proof;
   if (!btsct::bulletproof_plus_parse(raw.data(), raw.size(), proof)) {
      std::cerr << "parse failed\n";
      return 2;
   }
   if (!btsct::bulletproof_plus_verify(proof, commits)) {
      std::cerr << "verify failed\n";
      return 3;
   }
   std::cout << "OK\n";
   return 0;
}

int prove_clsag() {
   const size_t n = 4;
   std::vector<btsct::Point> pubs, commits;
   btsct::Scalar spend = btsct::Scalar::from_u64(11);
   btsct::Scalar z = btsct::Scalar::from_u64(13);
   btsct::Point pseudo = btsct::Point::commit(btsct::Scalar::from_u64(5), 7);
   for (size_t i = 0; i < n; ++i) {
      pubs.push_back(btsct::Point::generator() * btsct::Scalar::from_u64(20 + i));
      commits.push_back(btsct::Point::commit(btsct::Scalar::from_u64(30 + i), 7));
   }
   pubs[1] = btsct::Point::generator() * spend;
   commits[1] = pseudo + (btsct::Point::generator() * z);
   uint8_t msg[32] = {9, 8, 7, 6};
   auto sig = btsct::clsag_sign(msg, pubs, spend, commits, z, pseudo, 1);
   std::cout << "MESSAGE " << hex_of(msg, 32) << "\n";
   for (const auto& p : pubs) {
      uint8_t raw[33];
      p.to_compressed(raw);
      std::cout << "PUB " << hex_of(raw, 33) << "\n";
   }
   for (const auto& p : commits) {
      uint8_t raw[33];
      p.to_compressed(raw);
      std::cout << "COMMIT " << hex_of(raw, 33) << "\n";
   }
   uint8_t raw[33];
   pseudo.to_compressed(raw);
   std::cout << "PSEUDO " << hex_of(raw, 33) << "\n";
   auto bytes = btsct::clsag_serialize(sig);
   std::cout << "SIG " << hex_of(bytes) << "\n";
   return 0;
}

int verify_clsag() {
   std::string line;
   std::vector<uint8_t> message, sig;
   std::vector<btsct::Point> pubs, commits;
   btsct::Point pseudo;
   bool have_pseudo = false;
   while (std::getline(std::cin, line)) {
      auto take = [&](const char* tag) -> std::vector<uint8_t> {
         return unhex(line.substr(std::strlen(tag)));
      };
      if (line.rfind("MESSAGE ", 0) == 0) message = take("MESSAGE ");
      else if (line.rfind("PUB ", 0) == 0) {
         auto raw = take("PUB ");
         btsct::Point p;
         if (!btsct::Point::from_compressed(raw.data(), raw.size(), p)) return 2;
         pubs.push_back(p);
      } else if (line.rfind("COMMIT ", 0) == 0) {
         auto raw = take("COMMIT ");
         btsct::Point p;
         if (!btsct::Point::from_compressed(raw.data(), raw.size(), p)) return 2;
         commits.push_back(p);
      } else if (line.rfind("PSEUDO ", 0) == 0) {
         auto raw = take("PSEUDO ");
         if (!btsct::Point::from_compressed(raw.data(), raw.size(), pseudo)) return 2;
         have_pseudo = true;
      } else if (line.rfind("SIG ", 0) == 0) sig = take("SIG ");
   }
   if (message.size() != 32 || !have_pseudo) return 2;
   btsct::ClsagSignature parsed;
   if (!btsct::clsag_parse(sig.data(), sig.size(), parsed)) {
      std::cerr << "clsag parse failed\n";
      return 2;
   }
   if (!btsct::clsag_verify(message.data(), parsed, pubs, commits, pseudo)) {
      std::cerr << "clsag verify failed\n";
      return 3;
   }
   std::cout << "OK\n";
   return 0;
}

} // namespace

int main(int argc, char** argv) {
   try {
      if (argc < 2) return 2;
      std::string cmd = argv[1];
      if (cmd == "prove-bp") return prove_bp(argv[2], argv[3]);
      if (cmd == "verify-bp") return verify_bp();
      if (cmd == "prove-clsag") return prove_clsag();
      if (cmd == "verify-clsag") return verify_clsag();
      return 2;
   } catch (const std::exception& e) {
      std::cerr << e.what() << "\n";
      return 1;
   }
}
