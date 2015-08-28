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

#include <iostream>
#include <string>

#include <fc/crypto/elliptic.hpp>
#include <fc/io/json.hpp>

#include <graphene/chain/protocol/address.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/utilities/key_conversion.hpp>

#ifndef WIN32
#include <csignal>
#endif

using namespace std;

int main( int argc, char** argv )
{
   try
   {
      std::string dev_key_prefix;
      bool need_help;
      if( argc < 2 )
         need_help = true;
      else
      {
         dev_key_prefix = argv[1];
         if(  (dev_key_prefix == "-h")
           || (dev_key_prefix == "--help")
           )
           need_help = true;
      }

      if( need_help )
      {
         std::cerr << "get-dev-key <prefix> <suffix> ...\n"
             "\n"
             "example:\n"
             "\n"
             "get-dev-key wxyz- owner-5 active-7 balance-9 wit-block-signing-3 wit-owner-5 wit-active-33\n"
             "get-dev-key wxyz- wit-block-signing-0:101\n"
             "\n";
         return 1;
      }

      bool comma = false;

      auto show_key = [&]( const fc::ecc::private_key& priv_key )
      {
         fc::mutable_variant_object mvo;
         graphene::chain::public_key_type pub_key = priv_key.get_public_key();
         mvo( "private_key", graphene::utilities::key_to_wif( priv_key ) )
            ( "public_key", std::string( pub_key ) )
            ( "address", graphene::chain::address( pub_key ) )
            ;
         if( comma )
            std::cout << ",\n";
         std::cout << fc::json::to_string( mvo );
         comma = true;
      };

      std::cout << "[";

      for( int i=2; i<argc; i++ )
      {
         std::string arg = argv[i];
         std::string prefix;
         int lep = -1, rep;
         auto dash_pos = arg.rfind('-');
         if( dash_pos != string::npos )
         {
            std::string lhs = arg.substr( 0, dash_pos+1 );
            std::string rhs = arg.substr( dash_pos+1 );
            auto colon_pos = rhs.find(':');
            if( colon_pos != string::npos )
            {
               prefix = lhs;
               lep = std::stoi( rhs.substr( 0, colon_pos ) );
               rep = std::stoi( rhs.substr( colon_pos+1 ) );
            }
         }
         vector< fc::ecc::private_key > keys;
         if( lep >= 0 )
         {
            for( int k=lep; k<rep; k++ )
            {
               std::string s = dev_key_prefix + prefix + std::to_string(k);
               show_key( fc::ecc::private_key::regenerate( fc::sha256::hash( s ) ) );
            }
         }
         else
         {
            show_key( fc::ecc::private_key::regenerate( fc::sha256::hash( dev_key_prefix + arg ) ) );
         }
      }
      std::cout << "]\n";
   }
   catch ( const fc::exception& e )
   {
      std::cout << e.to_detail_string() << "\n";
      return 1;
   }
   return 0;
}
