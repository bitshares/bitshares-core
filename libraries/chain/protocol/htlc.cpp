/*
 * Copyright (c) 2018 jmjatlanta and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/chain/protocol/htlc.hpp>

namespace graphene { namespace chain {

   void htlc_create_operation::validate()const {
     FC_ASSERT( source != destination );
      FC_ASSERT( fee.amount >= 0 );
      FC_ASSERT( amount.amount > 0 );
   }

   void htlc_redeem_operation::validate()const {
      FC_ASSERT( fee.amount >= 0 );
      FC_ASSERT( preimage.size() > 0 );
   }
   void htlc_extend_operation::validate()const {
      FC_ASSERT( fee.amount >= 0 );
   }

   std::string hash_algorithm_to_string(fc::enum_type<uint8_t, hash_algorithm> algo)
   {
      switch(algo)
      {
         case (hash_algorithm::ripemd160):
            return "ripemd160";
         case (hash_algorithm::sha256):
            return "sha256";
         case (hash_algorithm::sha1):
            return "sha1";
         default:
            break;
      }
      return "unknown";
   }

   fc::enum_type<uint8_t, hash_algorithm> string_to_hash_algorithm(std::string incoming)
   {
      std::transform(incoming.begin(), incoming.end(), incoming.begin(), ::toupper);
      if (incoming == "RIPEMD160")
         return hash_algorithm::ripemd160;
      if (incoming == "SHA256")
         return hash_algorithm::sha256;
      if (incoming == "SHA1")
         return hash_algorithm::sha1;
      return hash_algorithm::unknown;
   }
} }
