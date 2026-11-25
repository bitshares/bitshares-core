// libFuzzer harness for CLSAG parsing and verification.
#include <bts_ct/clsag.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
   if (size > 8192) return 0;
   btsct::ClsagSignature sig;
   if (!btsct::clsag_parse(data, size, sig)) return 0;
   const size_t n = sig.s.size();
   if (n == 0 || size < 33) return 0;
   std::vector<btsct::Point> pubs;
   std::vector<btsct::Point> commits;
   pubs.reserve(n);
   commits.reserve(n);
   for (size_t i = 0; i < n; ++i) {
      const size_t off = (i * 33) % (size - 32);
      btsct::Point p;
      if (!btsct::Point::from_compressed(data + off, 33, p)) return 0;
      pubs.push_back(p);
      btsct::Point c;
      const size_t off2 = ((i + 1) * 33) % (size - 32);
      if (!btsct::Point::from_compressed(data + off2, 33, c)) return 0;
      commits.push_back(c);
   }
   uint8_t msg[32] = {0};
   const size_t msg_off = size >= 32 ? size - 32 : 0;
   for (size_t i = 0; i < 32 && msg_off + i < size; ++i) msg[i] = data[msg_off + i];
   try {
      (void)btsct::clsag_verify(msg, sig, pubs, commits, pubs[0]);
   } catch (...) {
   }
   return 0;
}
