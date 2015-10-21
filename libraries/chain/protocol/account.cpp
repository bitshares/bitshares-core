/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/chain/protocol/account.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

/**
 * Names must comply with the following grammar (RFC 1035):
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> | <subdomain> "." <label>
 * <label> ::= <letter> [ [ <ldh-str> ] <let-dig> ]
 * <ldh-str> ::= <let-dig-hyp> | <let-dig-hyp> <ldh-str>
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 *
 * Which is equivalent to the following:
 *
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> ("." <label>)*
 * <label> ::= <letter> [ [ <let-dig-hyp>+ ] <let-dig> ]
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 *
 * I.e. a valid name consists of a dot-separated sequence
 * of one or more labels consisting of the following rules:
 *
 * - Each label is three characters or more
 * - Each label begins with a letter
 * - Each label ends with a letter or digit
 * - Each label contains only letters, digits or hyphens
 *
 * In addition we require the following:
 *
 * - All letters are lowercase
 * - Length is between (inclusive) GRAPHENE_MIN_ACCOUNT_NAME_LENGTH and GRAPHENE_MAX_ACCOUNT_NAME_LENGTH
 */
bool is_valid_name( const string& name )
{ try {
    const size_t len = name.size();

    /** this condition will prevent witnesses from including new names before this time, but
     * allow them after this time.   This check can be removed from the code after HARDFORK_385_TIME
     * has passed.
     */
    if( fc::time_point::now() < fc::time_point(HARDFORK_385_TIME) )
       FC_ASSERT( len >= 3 );

    if( len < GRAPHENE_MIN_ACCOUNT_NAME_LENGTH )
    {
          ilog( ".");
        return false;
    }

    if( len > GRAPHENE_MAX_ACCOUNT_NAME_LENGTH )
    {
          ilog( ".");
        return false;
    }

    size_t begin = 0;
    while( true )
    {
       size_t end = name.find_first_of( '.', begin );
       if( end == std::string::npos )
          end = len;
       if( (end - begin) < GRAPHENE_MIN_ACCOUNT_NAME_LENGTH )
       {
          idump( (name) (end)(len)(begin)(GRAPHENE_MAX_ACCOUNT_NAME_LENGTH) );
          return false;
       }
       switch( name[begin] )
       {
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
          case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
          case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
          case 'y': case 'z':
             break;
          default:
          ilog( ".");
             return false;
       }
       switch( name[end-1] )
       {
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
          case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
          case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
          case 'y': case 'z':
          case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          case '8': case '9':
             break;
          default:
          ilog( ".");
             return false;
       }
       for( size_t i=begin+1; i<end-1; i++ )
       {
          switch( name[i] )
          {
             case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h':
             case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p':
             case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x':
             case 'y': case 'z':
             case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
             case '8': case '9':
             case '-':
                break;
             default:
          ilog( ".");
                return false;
          }
       }
       if( end == len )
          break;
       begin = end+1;
    }
    return true;
} FC_CAPTURE_AND_RETHROW( (name) ) }

bool is_cheap_name( const string& n )
{
   bool v = false;
   for( auto c : n )
   {
      if( c >= '0' && c <= '9' ) return true;
      if( c == '.' || c == '-' || c == '/' ) return true;
      switch( c )
      {
         case 'a':
         case 'e':
         case 'i':
         case 'o':
         case 'u':
         case 'y':
            v = true;
      }
   }
   if( !v )
      return true;
   return false;
}

share_type account_create_operation::calculate_fee( const fee_parameters_type& k )const
{
   auto core_fee_required = k.basic_fee;

   if( !is_cheap_name(name) )
      core_fee_required = k.premium_fee;

   // Authorities and vote lists can be arbitrarily large, so charge a data fee for big ones
   auto data_fee =  calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte ); 
   core_fee_required += data_fee;

   return core_fee_required;
}


void account_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( is_valid_name( name ) );
   FC_ASSERT( referrer_percent <= GRAPHENE_100_PERCENT );
   FC_ASSERT( owner.num_auths() != 0 );
   FC_ASSERT( owner.address_auths.size() == 0 );
   FC_ASSERT( active.num_auths() != 0 );
   FC_ASSERT( active.address_auths.size() == 0 );
   FC_ASSERT( !owner.is_impossible(), "cannot create an account with an imposible owner authority threshold" );
   FC_ASSERT( !active.is_impossible(), "cannot create an account with an imposible active authority threshold" );
   options.validate();
}




share_type account_update_operation::calculate_fee( const fee_parameters_type& k )const
{
   auto core_fee_required = k.fee;  
   if( new_options )
      core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte );
   return core_fee_required;
}

void account_update_operation::validate()const
{
   FC_ASSERT( account != GRAPHENE_TEMP_ACCOUNT );
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( account != account_id_type() );
   FC_ASSERT( owner || active || new_options );
   if( owner )
   {
      FC_ASSERT( owner->num_auths() != 0 );
      FC_ASSERT( owner->address_auths.size() == 0 );
      FC_ASSERT( !owner->is_impossible(), "cannot update an account with an imposible owner authority threshold" );
   }
   if( active )
   {
      FC_ASSERT( active->num_auths() != 0 );
      FC_ASSERT( active->address_auths.size() == 0 );
      FC_ASSERT( !active->is_impossible(), "cannot update an account with an imposible active authority threshold" );
   }

   if( new_options )
      new_options->validate();
}


share_type account_upgrade_operation::calculate_fee(const fee_parameters_type& k) const
{
   if( upgrade_to_lifetime_member )
      return k.membership_lifetime_fee;
   return k.membership_annual_fee;
}


void account_upgrade_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

void account_transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
}


} } // graphene::chain
