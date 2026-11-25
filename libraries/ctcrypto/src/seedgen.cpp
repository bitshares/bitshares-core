// Write a small libFuzzer corpus of honest proofs. Not a consensus tool.
#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static void write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
   std::ofstream out(path, std::ios::binary);
   out.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

int main(int argc, char** argv) {
   if (argc != 3) return 2;
   const std::string bp_dir = argv[1];
   const std::string cl_dir = argv[2];
   const uint64_t values[] = {0, 1, 2, 1000, 0xffffffffffffffffull};
   for (uint64_t v : values) {
      btsct::Scalar blind = btsct::Scalar::from_u64(3 + (v & 0xff));
      auto proof = btsct::bulletproof_plus_prove({v}, {blind});
      auto bytes = btsct::bulletproof_plus_serialize(proof);
      write_file(bp_dir + "/bp-" + std::to_string(v) + ".bin", bytes);
   }
   {
      std::vector<uint64_t> vs = {1, 2, 3, 4};
      std::vector<btsct::Scalar> blinds;
      for (size_t i = 0; i < vs.size(); ++i) blinds.push_back(btsct::Scalar::from_u64(9 + i));
      auto proof = btsct::bulletproof_plus_prove(vs, blinds);
      write_file(bp_dir + "/bp-agg4.bin", btsct::bulletproof_plus_serialize(proof));
   }
   for (size_t n : {4u, 16u}) {
      std::vector<btsct::Point> pubs, commits;
      btsct::Scalar spend = btsct::Scalar::from_u64(11);
      btsct::Scalar z = btsct::Scalar::from_u64(13);
      btsct::Point pseudo = btsct::Point::commit(btsct::Scalar::from_u64(5), 7);
      for (size_t i = 0; i < n; ++i) {
         btsct::Scalar s = btsct::Scalar::from_u64(20 + i);
         pubs.push_back(btsct::Point::generator() * s);
         commits.push_back(btsct::Point::commit(btsct::Scalar::from_u64(30 + i), 7));
      }
      pubs[1] = btsct::Point::generator() * spend;
      commits[1] = pseudo + (btsct::Point::generator() * z);
      uint8_t msg[32] = {9, 8, 7, 6};
      auto sig = btsct::clsag_sign(msg, pubs, spend, commits, z, pseudo, 1);
      write_file(cl_dir + "/clsag-" + std::to_string(n) + ".bin", btsct::clsag_serialize(sig));
   }
   std::puts("seeds written");
   return 0;
}
