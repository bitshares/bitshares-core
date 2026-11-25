// libFuzzer harness for Bulletproofs+ parsing and verification.
// Build: clang++ -fsanitize=fuzzer,address,undefined -O1
#include <bts_ct/bulletproofs_plus.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
   if (size > 8192) return 0;
   btsct::BulletproofPlus proof;
   if (!btsct::bulletproof_plus_parse(data, size, proof)) return 0;
   std::vector<btsct::Point> commits;
   // Commitments are caller-supplied. Feed the fuzzer bytes through the
   // point parser so verification runs on both honest seeds and mutations.
   for (size_t off = 0; off + 33 <= size && commits.size() < 16; off += 33) {
      btsct::Point p;
      if (!btsct::Point::from_compressed(data + off, 33, p)) break;
      commits.push_back(p);
   }
   if (commits.empty()) {
      btsct::Point p;
      if (btsct::Point::from_compressed(data, size >= 33 ? 33 : size, p))
         commits.push_back(p);
   }
   if (commits.empty()) return 0;
   try {
      (void)btsct::bulletproof_plus_verify(proof, commits);
      if (commits.size() > 1) {
         const btsct::BulletproofPlus* pp = &proof;
         const std::vector<btsct::Point>* cc = &commits;
         (void)btsct::bulletproof_plus_verify_batch({pp}, {cc}, {btsct::Scalar::from_u64(3)});
      }
   } catch (...) {
   }
   return 0;
}
