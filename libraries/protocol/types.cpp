/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <graphene/protocol/types.hpp>
#include <graphene/protocol/fee_schedule.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>

#include <cstring>

namespace graphene { namespace protocol {

    public_key_type::public_key_type():key_data(){};

    public_key_type::public_key_type( const fc::ecc::public_key_data& data )
        :key_data( data ) {};

    public_key_type::public_key_type( const fc::ecc::public_key& pubkey )
        :key_data( pubkey ) {};

    public_key_type::public_key_type( const std::string& base58str )
    {
      // TODO:  Refactor syntactic checks into static is_valid()
      //        to make public_key_type API more similar to address API
       std::string prefix( GRAPHENE_ADDRESS_PREFIX );
       const size_t prefix_len = prefix.size();
       FC_ASSERT( base58str.size() > prefix_len );
       FC_ASSERT( base58str.substr( 0, prefix_len ) ==  prefix , "", ("base58str", base58str) );
       auto bin = fc::from_base58( base58str.substr( prefix_len ) );
       auto bin_key = fc::raw::unpack<binary_key>(bin);
       key_data = bin_key.data;
       FC_ASSERT( fc::ripemd160::hash( (char*) key_data.data(), key_data.size() )._hash[0].value() == bin_key.check );
    };

    public_key_type::operator fc::ecc::public_key_data() const
    {
       return key_data;
    };

    public_key_type::operator fc::ecc::public_key() const
    {
       return fc::ecc::public_key( key_data );
    };

    public_key_type::operator std::string() const
    {
       binary_key k;
       k.data = key_data;
       k.check = fc::ripemd160::hash( (char*) k.data.data(), k.data.size() )._hash[0].value();
       auto data = fc::raw::pack( k );
       return GRAPHENE_ADDRESS_PREFIX + fc::to_base58( data.data(), data.size() );
    }

    bool operator == ( const public_key_type& p1, const fc::ecc::public_key& p2)
    {
       return p1.key_data == p2.serialize();
    }

    bool operator == ( const public_key_type& p1, const public_key_type& p2)
    {
       return p1.key_data == p2.key_data;
    }

    bool operator != ( const public_key_type& p1, const public_key_type& p2)
    {
       return p1.key_data != p2.key_data;
    }
    
    bool operator < ( const public_key_type& p1, const public_key_type& p2)
    {
       return address(p1) < address(p2);
    }

    pq_public_key_type::pq_public_key_type()
    {}

    pq_public_key_type::pq_public_key_type( const fc::pq_public_key& k )
       : algorithm( k.algorithm() ), data( k.data() )
    {}

    pq_public_key_type::pq_public_key_type( const std::string& base58str )
    {
       std::string prefix( GRAPHENE_ADDRESS_PREFIX );
       const size_t prefix_len = prefix.size();
       FC_ASSERT( base58str.size() > prefix_len + 1 );
       FC_ASSERT( base58str.substr( 0, prefix_len ) == prefix, "", ("base58str", base58str) );
       FC_ASSERT( base58str[prefix_len] == 'P', "expected post-quantum key string", ("base58str", base58str) );
       auto bin = fc::from_base58( base58str.substr( prefix_len + 1 ) );
       // 1 algorithm byte + a uint32_t checksum, at minimum (data may legitimately be empty
       // only for algorithm == none, which validate() below still requires callers to reject
       // wherever a real key is expected).
       FC_ASSERT( bin.size() >= 1 + sizeof(uint32_t), "malformed post-quantum key string" );
       const size_t payload_len = bin.size() - sizeof(uint32_t);
       uint32_t check_actual = 0;
       memcpy( &check_actual, bin.data() + payload_len, sizeof(check_actual) );
       const uint32_t check_expected =
             fc::ripemd160::hash( bin.data(), payload_len )._hash[0].value();
       FC_ASSERT( check_actual == check_expected, "post-quantum key string has a bad checksum" );
       algorithm = (fc::pq_algorithm)bin[0];
       data.assign( bin.begin() + 1, bin.begin() + payload_len );
       FC_ASSERT( fc::pq_public_key::size_for_algorithm( algorithm ) == 0
                     || data.size() == fc::pq_public_key::size_for_algorithm( algorithm ),
                  "unexpected public key length for algorithm ${alg}", ("alg", (int)algorithm) );
    }

    std::string pq_public_key_type::to_base58() const
    {
       std::vector<char> bin;
       bin.reserve( data.size() + 1 + sizeof(uint32_t) );
       bin.push_back( (char)algorithm );
       bin.insert( bin.end(), data.begin(), data.end() );
       // Base58Check-style integrity check, matching public_key_type's `binary_key.check`:
       // guards against a single mistyped/corrupted character silently decoding into a
       // different, wrong (but structurally valid-length) key instead of being rejected.
       const uint32_t check = fc::ripemd160::hash( bin.data(), bin.size() )._hash[0].value();
       const char* check_bytes = reinterpret_cast<const char*>( &check );
       bin.insert( bin.end(), check_bytes, check_bytes + sizeof(check) );
       return GRAPHENE_ADDRESS_PREFIX + std::string( "P" ) + fc::to_base58( bin.data(), bin.size() );
    }

    pq_public_key_type pq_public_key_type::from_base58( const std::string& str )
    {
       return pq_public_key_type( str );
    }

    fc::pq_public_key pq_public_key_type::to_pqc() const
    {
       return fc::pq_public_key( algorithm, data );
    }

    void pq_public_key_type::validate() const
    {
       FC_ASSERT( algorithm != fc::pq_algorithm::none,
                  "post-quantum key algorithm must be specified" );
       const uint16_t expected = fc::pq_public_key::size_for_algorithm( algorithm );
       FC_ASSERT( expected != 0,
                  "unrecognized post-quantum key algorithm ${alg}", ("alg", (int)algorithm) );
       FC_ASSERT( data.size() == expected,
                  "unexpected post-quantum public key length for algorithm ${alg}: "
                  "got ${got}, expected ${exp}",
                  ("alg", (int)algorithm)("got", data.size())("exp", expected) );
    }

} } // graphene::protocol

namespace fc
{
    using namespace std;
    void to_variant( const graphene::protocol::public_key_type& var,  fc::variant& vo, uint32_t max_depth )
    {
        vo = std::string( var );
    }

    void from_variant( const fc::variant& var,  graphene::protocol::public_key_type& vo, uint32_t max_depth )
    {
        vo = graphene::protocol::public_key_type( var.as_string() );
    }

    void to_variant( const graphene::protocol::pq_public_key_type& var, fc::variant& vo, uint32_t max_depth )
    {
        vo = var.to_base58();
    }

    void from_variant( const fc::variant& var, graphene::protocol::pq_public_key_type& vo, uint32_t max_depth )
    {
        // Accept the canonical base58 form. Objects are still accepted so that anything
        // already persisted in the reflected {algorithm,data} shape keeps loading.
        if( var.is_string() )
           vo = graphene::protocol::pq_public_key_type( var.as_string() );
        else
        {
           const auto& o = var.get_object();
           // pq_algorithm is a reflected enum, so it arrives as its name ("ml_dsa_65"),
           // not as an integer -- let fc's enum conversion handle either spelling.
           fc::from_variant( o["algorithm"], vo.algorithm, max_depth );
           fc::from_variant( o["data"], vo.data, max_depth );
           if( o.contains("legacy") )
              fc::from_variant( o["legacy"], vo.legacy, max_depth );
        }
    }
    
    void from_variant( const fc::variant& var, std::shared_ptr<const graphene::protocol::fee_schedule>& vo,
                       uint32_t max_depth ) {
        // If it's null, just make a new one
        if (!vo) vo = std::make_shared<const graphene::protocol::fee_schedule>();
        // Convert the non-const shared_ptr<const fee_schedule> to a non-const fee_schedule& so we can write it
        // Don't decrement max_depth since we're not actually deserializing at this step
        from_variant(var, const_cast<graphene::protocol::fee_schedule&>(*vo), max_depth);
    }

namespace raw {
   template void pack( datastream<size_t>& s, const graphene::protocol::public_key_type& tx,
                       uint32_t _max_depth=FC_PACK_MAX_DEPTH );
   template void pack( datastream<char*>& s, const graphene::protocol::public_key_type& tx,
                       uint32_t _max_depth=FC_PACK_MAX_DEPTH );
   template void unpack( datastream<const char*>& s, graphene::protocol::public_key_type& tx,
                         uint32_t _max_depth=FC_PACK_MAX_DEPTH );
} } // fc::raw
