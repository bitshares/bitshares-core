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
#include <graphene/protocol/account.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

/**
 * Names must comply with the following grammar (RFC 1035):
 * @code
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> | <subdomain> "." <label>
 * <label> ::= <letter> [ [ <ldh-str> ] <let-dig> ]
 * <ldh-str> ::= <let-dig-hyp> | <let-dig-hyp> <ldh-str>
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 * @endcode
 *
 * Which is equivalent to the following:
 *
 * @code
 * <domain> ::= <subdomain> | " "
 * <subdomain> ::= <label> ("." <label>)*
 * <label> ::= <letter> [ [ <let-dig-hyp>+ ] <let-dig> ]
 * <let-dig-hyp> ::= <let-dig> | "-"
 * <let-dig> ::= <letter> | <digit>
 * @endcode
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

   if( len < GRAPHENE_MIN_ACCOUNT_NAME_LENGTH )
   {
      return false;
   }

   if( len > GRAPHENE_MAX_ACCOUNT_NAME_LENGTH )
   {
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

void account_options::validate() const
{
   auto needed_witnesses = num_witness;
   auto needed_committee = num_committee;

   for( vote_id_type id : votes )
   {
      if( id.type() == vote_id_type::witness && needed_witnesses > 0 )
         --needed_witnesses;
      else if ( id.type() == vote_id_type::committee && needed_committee > 0 )
         --needed_committee;
   }

   FC_ASSERT( needed_witnesses == 0 && needed_committee == 0,
              "May not specify fewer witnesses or committee members than the number voted for.");

   if( pq_memo_key.valid() )
   {
      // A memo key is for encapsulation, never for signing. Accepting an ML-DSA key here would
      // publish a signing key in a field senders treat as a KEM key, and every encapsulation
      // against it would fail; reject the whole class rather than only the wrong sizes.
      //
      // ML-KEM-768 specifically, not any ML-KEM parameter set: memo_data::set_message_pq
      // encapsulates at 768 (NIST Category 3, matching the ML-DSA-65 used for signatures), and
      // the memo carries no algorithm tag of its own. Accepting a 512 or 1024 key here would
      // publish something no sender could encapsulate to. Widening this requires the memo
      // construction to become algorithm-aware first.
      FC_ASSERT( pq_memo_key->algorithm == fc::pq_algorithm::ml_kem_768,
                 "The post-quantum memo key must be an ML-KEM-768 (FIPS 203) key." );
      // pq_public_key_type performs no length checking on raw deserialization, so a malformed
      // key would otherwise only be caught by the first sender who tried to use it.
      pq_memo_key->validate();
   }
}

share_type account_create_operation::calculate_fee( const fee_params_t& k )const
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
   FC_ASSERT( !owner.is_impossible(), "cannot create an account with an impossible owner authority threshold" );
   FC_ASSERT( !active.is_impossible(), "cannot create an account with an impossible active authority threshold" );
   options.validate();
   if( extensions.value.owner_special_authority.valid() )
      validate_special_authority( *extensions.value.owner_special_authority );
   if( extensions.value.active_special_authority.valid() )
      validate_special_authority( *extensions.value.active_special_authority );
   if( extensions.value.buyback_options.valid() )
   {
      FC_ASSERT( !(extensions.value.owner_special_authority.valid()) );
      FC_ASSERT( !(extensions.value.active_special_authority.valid()) );
      FC_ASSERT( owner == authority::null_authority() );
      FC_ASSERT( active == authority::null_authority() );
      size_t n_markets = extensions.value.buyback_options->markets.size();
      FC_ASSERT( n_markets > 0 );
      for( const asset_id_type& m : extensions.value.buyback_options->markets )
      {
         FC_ASSERT( m != extensions.value.buyback_options->asset_to_buy );
      }
   }
}

share_type account_update_operation::calculate_fee( const fee_params_t& k )const
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

   bool has_action = (
         owner.valid()
      || active.valid()
      || new_options.valid()
      || extensions.value.owner_special_authority.valid()
      || extensions.value.active_special_authority.valid()
      );

   FC_ASSERT( has_action );

   if( owner )
   {
      FC_ASSERT( owner->num_auths() != 0 );
      FC_ASSERT( owner->address_auths.size() == 0 );
      FC_ASSERT( !owner->is_impossible(), "cannot update an account with an impossible owner authority threshold" );
   }
   if( active )
   {
      FC_ASSERT( active->num_auths() != 0 );
      FC_ASSERT( active->address_auths.size() == 0 );
      FC_ASSERT( !active->is_impossible(), "cannot update an account with an impossible active authority threshold" );
   }

   if( new_options )
      new_options->validate();
   if( extensions.value.owner_special_authority.valid() )
      validate_special_authority( *extensions.value.owner_special_authority );
   if( extensions.value.active_special_authority.valid() )
      validate_special_authority( *extensions.value.active_special_authority );
}

share_type account_upgrade_operation::calculate_fee(const fee_params_t& k) const
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

} } // graphene::protocol

namespace fc { namespace raw {

namespace detail {

// account_options is embedded in account_create_operation and account_update_operation, both on
// chain since genesis, so pq_memo_key must not appear in the legacy encoding. Gate it the way
// authority gates pq_key_auths, and append rather than insert, so legacy bytes stay
// byte-identical to what a pre-PQ node produces.
template<typename Stream>
void pack_account_options_impl( Stream& s, const graphene::protocol::account_options& v,
                                uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, v.memo_key, _max_depth );
   fc::raw::pack( s, v.voting_account, _max_depth );
   fc::raw::pack( s, v.num_witness, _max_depth );
   fc::raw::pack( s, v.num_committee, _max_depth );
   fc::raw::pack( s, v.votes, _max_depth );
   fc::raw::pack( s, v.extensions, _max_depth );
   fc::raw::pack( s, v.pq_memo_key, _max_depth );   // gates itself; see fc::pq_gated
}

template<typename Stream>
void unpack_account_options_impl( Stream& s, graphene::protocol::account_options& v,
                                  uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, v.memo_key, _max_depth );
   fc::raw::unpack( s, v.voting_account, _max_depth );
   fc::raw::unpack( s, v.num_witness, _max_depth );
   fc::raw::unpack( s, v.num_committee, _max_depth );
   fc::raw::unpack( s, v.votes, _max_depth );
   fc::raw::unpack( s, v.extensions, _max_depth );
   fc::raw::unpack( s, v.pq_memo_key, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking account_options" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::account_options& v, uint32_t d )
   { detail::pack_account_options_impl( s, v, d ); }
void pack( sha256::encoder& s, const graphene::protocol::account_options& v, uint32_t d )
   { detail::pack_account_options_impl( s, v, d ); }
void pack( datastream<char*>& s, const graphene::protocol::account_options& v, uint32_t d )
   { detail::pack_account_options_impl( s, v, d ); }
void unpack( datastream<const char*>& s, graphene::protocol::account_options& v, uint32_t d )
   { detail::unpack_account_options_impl( s, v, d ); }

template std::vector<char> pack( const graphene::protocol::account_options& v, uint32_t _max_depth );
template size_t pack_size( const graphene::protocol::account_options& v );

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_whitelist_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_upgrade_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_transfer_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_whitelist_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_upgrade_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::account_transfer_operation )
