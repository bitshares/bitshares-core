/*  (C) 2019 Peter Conrad
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

#pragma once

#include <graphene/protocol/asset.hpp>

namespace graphene { namespace chain {
   class stored_value;

   class stored_debt
   {
   public:
      explicit stored_debt( const graphene::protocol::asset_id_type asset = graphene::protocol::asset_id_type() );
      ~stored_debt();

      stored_debt( const stored_debt& copy ) = delete;
      stored_debt( stored_debt& copy ) = delete;
      stored_debt( stored_debt&& move );

      stored_debt& operator=( const stored_debt& copy ) = delete;
      stored_debt& operator=( stored_debt& copy ) = delete;
      stored_debt& operator=( stored_debt&& move );

      graphene::protocol::asset get_value()const;
      graphene::protocol::asset_id_type get_asset()const { return _asset; }
      graphene::protocol::share_type get_amount()const { return _amount; }

      stored_value issue( const graphene::protocol::share_type amount );
      void burn( stored_value&& amount );

      template< typename Stream >
      void unpack( Stream s, uint32_t _max_depth )
      {
         graphene::protocol::asset amount;
         fc::raw::unpack( s, amount, _max_depth );
         _asset = amount.asset_id;
         _amount = amount.amount;
      }

   protected:
      graphene::protocol::asset_id_type _asset;
      graphene::protocol::share_type _amount;

      void restore( const graphene::protocol::asset& backup );
      friend class account_balance_object;
      friend class account_statistics_object;
      friend class asset_dynamic_data_object;
      friend class asset_bitasset_data_object;
      friend class balance_object;
      friend class fba_accumulator_object;
      friend class dynamic_global_property_object;
      friend class htlc_object;
      friend class limit_order_object;
      friend class call_order_object;
      friend class force_settlement_object;
      friend class collateral_bid_object;
      friend class vesting_balance_object;
      friend class worker_object;
   };

   class stored_value : public stored_debt
   {
   public:
      explicit stored_value( const graphene::protocol::asset_id_type asset = graphene::protocol::asset_id_type() );
      stored_value( stored_value&& move ) : stored_debt( std::move(move) ) {}

      stored_value& operator=( stored_value&& move ) {
         this->stored_debt::operator=( std::move(move) );
         return *this;
      }

      stored_value split( const graphene::protocol::share_type amount );
      stored_value& operator+=( stored_value&& other );

   protected:
      static stored_value issue( const graphene::protocol::asset_id_type asset,
                                 const graphene::protocol::share_type amount );
      void burn();
      friend class stored_debt;
   };

} } // graphene::chain

namespace fc {

template<>
void from_variant( const fc::variant& var, graphene::chain::stored_debt& value, uint32_t max_depth )
{
   FC_THROW_EXCEPTION( fc::assert_exception, "Unsupported!" );
}

template<>
void from_variant( const fc::variant& var, graphene::chain::stored_value& value, uint32_t max_depth )
{
   FC_THROW_EXCEPTION( fc::assert_exception, "Unsupported!" );
}

template<>
void to_variant( const graphene::chain::stored_debt& value, fc::variant& var, uint32_t max_depth )
{
   to_variant( value.get_value(), var, max_depth );
}

template<>
void to_variant( const graphene::chain::stored_value& value, fc::variant& var, uint32_t max_depth )
{
   to_variant( value.get_value(), var, max_depth );
}

namespace raw {

template< typename Stream >
void pack( Stream& stream, const graphene::chain::stored_debt& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   pack( stream, value.get_value(), _max_depth );
}

template< typename Stream >
void pack( Stream& stream, const graphene::chain::stored_value& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   pack( stream, value.get_value(), _max_depth );
}

template< typename Stream >
void unpack( Stream& s, graphene::chain::stored_debt& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   value.unpack( s, _max_depth );
}

template< typename Stream >
void unpack( Stream& s, graphene::chain::stored_value& value, uint32_t _max_depth=FC_PACK_MAX_DEPTH )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   value.unpack( s, _max_depth );
}

} // fc::raw

template<>
struct get_typename< graphene::chain::stored_debt >
{
   static const char* name()
   {
      return "graphene::chain::stored_debt";
   }
};

template<>
struct get_typename< graphene::chain::stored_value >
{
   static const char* name()
   {
      return "graphene::chain::stored_value";
   }
};

} // fc
