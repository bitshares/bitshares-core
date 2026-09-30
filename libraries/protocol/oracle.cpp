/*
 * Copyright (c) 2026 BitShares contributors.
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
#include <graphene/protocol/oracle.hpp>

#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

bool is_valid_oracle_name( const string& name )
{
   if( name.size() < GRAPHENE_ORACLE_MIN_NAME_LENGTH
       || name.size() > GRAPHENE_ORACLE_MAX_NAME_LENGTH )
      return false;

   // Uppercase, digits, '.' and '-'. A restricted set on purpose: names are how humans tell
   // one oracle from another, and lookalike names built from mixed case or unicode would be a
   // way to get a consumer to reference the wrong series.
   char previous = 0;
   for( size_t i = 0; i < name.size(); ++i )
   {
      const char c = name[i];
      const bool is_alnum = ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' );
      const bool is_sep   = ( '.' == c || '-' == c );
      if( !is_alnum && !is_sep )
         return false;
      // no leading, trailing or repeated separator
      if( is_sep && ( 0 == i || i + 1 == name.size() || '.' == previous || '-' == previous ) )
         return false;
      previous = c;
   }
   return true;
}

void oracle_options::validate()const
{
   FC_ASSERT( producers.size() <= GRAPHENE_ORACLE_MAX_PRODUCERS,
              "An oracle may have at most ${m} producers", ("m", GRAPHENE_ORACLE_MAX_PRODUCERS) );

   uint32_t total_weight = 0;
   for( const auto& p : producers )
   {
      FC_ASSERT( p.second > 0,
                 "Producer ${a} has weight zero; remove the producer instead", ("a", p.first) );
      FC_ASSERT( p.second <= GRAPHENE_ORACLE_MAX_PRODUCER_WEIGHT,
                 "Producer weight should not exceed ${m}",
                 ("m", GRAPHENE_ORACLE_MAX_PRODUCER_WEIGHT) );
      total_weight += p.second;
   }
   // Bounded above by MAX_PRODUCERS * MAX_PRODUCER_WEIGHT, so the weighted median's running
   // total cannot overflow the uint32 it is accumulated in.
   FC_ASSERT( total_weight <= uint32_t( GRAPHENE_ORACLE_MAX_PRODUCERS )
                              * GRAPHENE_ORACLE_MAX_PRODUCER_WEIGHT,
              "Total producer weight is too large" );

   FC_ASSERT( minimum_producers > 0,
              "minimum_producers should be positive: an oracle with no quorum would report a "
              "value derived from nothing" );
   FC_ASSERT( minimum_producers <= GRAPHENE_ORACLE_MAX_PRODUCERS,
              "minimum_producers should not exceed ${m}", ("m", GRAPHENE_ORACLE_MAX_PRODUCERS) );
   // An unreachable quorum is almost certainly a mistake, and it would leave the oracle
   // permanently valueless while looking configured.
   FC_ASSERT( producers.empty() || minimum_producers <= producers.size(),
              "minimum_producers (${q}) exceeds the number of producers (${n}), so the oracle "
              "could never report a value",
              ("q", minimum_producers)("n", producers.size()) );

   FC_ASSERT( value_lifetime_sec > 0, "value_lifetime_sec should be positive" );

   // With the method held as a plain integer, an unknown value no longer fails at
   // deserialisation, so it has to fail here -- otherwise nodes would have to agree on how to
   // aggregate using a method none of them knows.
   FC_ASSERT( aggregation <= static_cast<uint8_t>( oracle_aggregation_method::median_over_window ),
              "Unknown aggregation method ${a}", ("a", aggregation) );

   if( oracle_aggregation_method::median_over_window == get_aggregation() )
      FC_ASSERT( window_sec > 0,
                 "window_sec should be positive when aggregating over a window" );
}

void oracle_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( is_valid_oracle_name( name ),
              "Oracle name '${n}' is not valid: ${min}-${max} characters, uppercase letters, "
              "digits, '.' and '-', with no leading, trailing or repeated separator",
              ("n", name)("min", GRAPHENE_ORACLE_MIN_NAME_LENGTH)
              ("max", GRAPHENE_ORACLE_MAX_NAME_LENGTH) );
   FC_ASSERT( description.size() <= GRAPHENE_ORACLE_MAX_DESCRIPTION_LENGTH,
              "Description should not exceed ${m} characters",
              ("m", GRAPHENE_ORACLE_MAX_DESCRIPTION_LENGTH) );
   FC_ASSERT( base_asset != quote_asset,
              "Base and quote asset should be different" );
   options.validate();
}

share_type oracle_create_operation::calculate_fee( const fee_params_t& schedule )const
{
   share_type core_fee_required = schedule.fee;
   core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), schedule.price_per_kbyte );
   return core_fee_required;
}

void oracle_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   FC_ASSERT( new_description.valid() || new_options.valid(),
              "Should change something" );
   if( new_description.valid() )
      FC_ASSERT( new_description->size() <= GRAPHENE_ORACLE_MAX_DESCRIPTION_LENGTH,
                 "Description should not exceed ${m} characters",
                 ("m", GRAPHENE_ORACLE_MAX_DESCRIPTION_LENGTH) );
   if( new_options.valid() )
      new_options->validate();
}

share_type oracle_update_operation::calculate_fee( const fee_params_t& schedule )const
{
   share_type core_fee_required = schedule.fee;
   core_fee_required += calculate_data_fee( fc::raw::pack_size(*this), schedule.price_per_kbyte );
   return core_fee_required;
}

void oracle_delete_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
}

void oracle_publish_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee should not be negative" );
   // A null or non-positive price would otherwise be carried into the aggregate, where a zero
   // reads as "free" to every consumer downstream.
   FC_ASSERT( !value.is_null(), "Published value should not be null" );
   value.validate( true );
}

} } // graphene::protocol

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_options )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_create_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_update_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_delete_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_publish_operation::fee_params_t )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_create_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_update_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_delete_operation )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_publish_operation )
