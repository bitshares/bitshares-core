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
