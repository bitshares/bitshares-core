/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/utilities/key_conversion.hpp>
#include <fc/crypto/base58.hpp>
#include <fc/variant.hpp>

namespace graphene { namespace utilities {

std::string key_to_wif(const fc::ecc::private_key& key)
{
  fc::sha256 secret = key.get_secret();
  const size_t size_of_data_to_hash = sizeof(secret) + 1;
  const size_t size_of_hash_bytes = 4;
  char data[size_of_data_to_hash + size_of_hash_bytes];
  data[0] = (char)0x80;
  memcpy(&data[1], (char*)&secret, sizeof(secret));
  fc::sha256 digest = fc::sha256::hash(data, size_of_data_to_hash);
  digest = fc::sha256::hash(digest);
  memcpy(data + size_of_data_to_hash, (char*)&digest, size_of_hash_bytes);
  return fc::to_base58(data, sizeof(data));
}

fc::optional<fc::ecc::private_key> wif_to_key( const std::string& wif_key )
{
  std::vector<char> wif_bytes;
  try
  {
    wif_bytes = fc::from_base58(wif_key);
  }
  catch (const fc::parse_error_exception&)
  {
    return fc::optional<fc::ecc::private_key>();
  }
  if (wif_bytes.size() < 5)
    return fc::optional<fc::ecc::private_key>();
  std::vector<char> key_bytes(wif_bytes.begin() + 1, wif_bytes.end() - 4);
  fc::ecc::private_key key = fc::variant(key_bytes).as<fc::ecc::private_key>();
  fc::sha256 check = fc::sha256::hash(wif_bytes.data(), wif_bytes.size() - 4);
  fc::sha256 check2 = fc::sha256::hash(check);
    
  if( memcmp( (char*)&check, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 || 
      memcmp( (char*)&check2, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 )
    return key;

  return fc::optional<fc::ecc::private_key>();
}

} } // end namespace graphene::utilities
