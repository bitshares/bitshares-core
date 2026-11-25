// Deserializer and verifier fuzz driver.
// Build with -DCTS_LIBFUZZER and -fsanitize=fuzzer to use libFuzzer.
// Without that flag it reads a file or runs an in-process mutation loop.
#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int exercise(const uint8_t* data, size_t size) {
   btsct::BulletproofPlus proof;
   btsct::bulletproof_plus_parse(data, size, proof);
   if (size >= 33) {
      btsct::Point commit;
      if (btsct::Point::from_compressed(data, 33, commit)) {
         try {
            btsct::bulletproof_plus_verify(proof, {commit});
         } catch (...) {
         }
      }
   }
   btsct::ClsagSignature sig;
   btsct::clsag_parse(data, size, sig);
   if (size >= 66) {
      btsct::Point a, b;
      if (btsct::Point::from_compressed(data, 33, a) &&
          btsct::Point::from_compressed(data + 33, 33, b)) {
         uint8_t msg[32] = {0};
         try {
            btsct::clsag_verify(msg, sig, {a}, {b}, a);
         } catch (...) {
         }
      }
   }
   return 0;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
   return exercise(data, size);
}

#ifndef CTS_LIBFUZZER
int main(int argc, char** argv) {
   if (argc == 2) {
      FILE* f = std::fopen(argv[1], "rb");
      if (!f) return 1;
      std::vector<uint8_t> buf(1 << 20);
      size_t n = std::fread(buf.data(), 1, buf.size(), f);
      std::fclose(f);
      return exercise(buf.data(), n);
   }
   // Deterministic mutation loop so CI runs the fuzzer without libFuzzer.
   std::vector<uint8_t> buf(4096);
   uint32_t state = 0xC0FFEEu;
   for (int i = 0; i < 2000; ++i) {
      state = state * 1664525u + 1013904223u;
      buf[i % buf.size()] = static_cast<uint8_t>(state >> 16);
      exercise(buf.data(), static_cast<size_t>(state % buf.size()));
   }
   std::cout << "ct fuzz loop completed\n";
   return 0;
}
#endif
