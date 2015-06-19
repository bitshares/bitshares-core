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
#include <graphene/chain/asset.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {
      bool operator < ( const asset& a, const asset& b )
      {
         return std::tie( a.asset_id, a.amount ) < std::tie( b.asset_id, b.amount);
      }
      bool operator <= ( const asset& a, const asset& b )
      {
         return std::tie( a.asset_id, a.amount ) <= std::tie( b.asset_id, b.amount);
      }
      bool operator < ( const price& a, const price& b )
      {
         if( a.base.asset_id < b.base.asset_id ) return true;
         if( a.base.asset_id > b.base.asset_id ) return false;
         if( a.quote.asset_id < b.quote.asset_id ) return true;
         if( a.quote.asset_id > b.quote.asset_id ) return false;
         auto amult = fc::uint128(b.quote.amount.value) * a.base.amount.value;
         auto bmult = fc::uint128(a.quote.amount.value) * b.base.amount.value;
         assert( (a.to_real() < b.to_real()) == (amult < bmult) );
         return amult < bmult;
      }
      bool operator <= ( const price& a, const price& b )
      {
         if( a.base.asset_id < b.base.asset_id ) return true;
         if( a.base.asset_id > b.base.asset_id ) return false;
         if( a.quote.asset_id < b.quote.asset_id ) return true;
         if( a.quote.asset_id > b.quote.asset_id ) return false;
         auto amult = fc::uint128(b.quote.amount.value) * a.base.amount.value;
         auto bmult = fc::uint128(a.quote.amount.value) * b.base.amount.value;
         assert( (a.to_real() <= b.to_real()) == (amult <= bmult) );
         return amult <= bmult;
      }
      bool operator == ( const price& a, const price& b )
      {
         if( a.base.asset_id < b.base.asset_id ) return true;
         if( a.base.asset_id > b.base.asset_id ) return false;
         if( a.quote.asset_id < b.quote.asset_id ) return true;
         if( a.quote.asset_id > b.quote.asset_id ) return false;
         auto amult = fc::uint128(a.quote.amount.value) * b.base.amount.value;
         auto bmult = fc::uint128(b.quote.amount.value) * a.base.amount.value;
         return amult == bmult;
      }
      bool operator != ( const price& a, const price& b )
      {
         return !(a==b);
      }

      bool operator >= ( const price& a, const price& b )
      {
         return !(a < b);
      }
      bool operator > ( const price& a, const price& b )
      {
         return !(a <= b);
      }

      asset operator * ( const asset& a, const price& b )
      {
         if( a.asset_id == b.base.asset_id )
         {
            FC_ASSERT( b.base.amount.value > 0 );
            auto result = (fc::uint128(a.amount.value) * b.quote.amount.value)/b.base.amount.value;
            FC_ASSERT( result <= GRAPHENE_MAX_SHARE_SUPPLY );
            return asset( result.to_uint64(), b.quote.asset_id );
         }
         else if( a.asset_id == b.quote.asset_id )
         {
            FC_ASSERT( b.quote.amount.value > 0 );
            auto result = (fc::uint128(a.amount.value) * b.base.amount.value)/b.quote.amount.value;
            FC_ASSERT( result <= GRAPHENE_MAX_SHARE_SUPPLY );
            return asset( result.to_uint64(), b.base.asset_id );
         }
         FC_ASSERT( !"invalid asset * price", "", ("asset",a)("price",b) );
      }

      price operator / ( const asset& base, const asset& quote )
      {
         FC_ASSERT( base.asset_id != quote.asset_id );
         return price{base,quote};
      }
      price price::max( asset_id_type base, asset_id_type quote ) { return asset( share_type(GRAPHENE_MAX_SHARE_SUPPLY), base ) / asset( share_type(1), quote); }
      price price::min( asset_id_type base, asset_id_type quote ) { return asset( 1, base ) / asset( GRAPHENE_MAX_SHARE_SUPPLY, quote); }

      price price::call_price(const asset& debt, const asset& collateral, uint16_t collateral_ratio)
      { try {
         fc::uint128 tmp( collateral.amount.value );
         tmp *= collateral_ratio - 1000;
         tmp /= 1000;
         FC_ASSERT( tmp <= GRAPHENE_MAX_SHARE_SUPPLY );
         return asset( tmp.to_uint64(), collateral.asset_id) / debt;
      } FC_CAPTURE_AND_RETHROW( (debt)(collateral)(collateral_ratio) ) }

      bool price::is_null() const { return *this == price(); }

      void price::validate() const
      { try {
         FC_ASSERT( base.amount > share_type(0) );
         FC_ASSERT( quote.amount > share_type(0) );
         FC_ASSERT( base.asset_id != quote.asset_id );
      } FC_CAPTURE_AND_RETHROW( (base)(quote) ) }

      void price_feed::validate() const
      { try {
         if( !settlement_price.is_null() )
            settlement_price.validate();
         FC_ASSERT( maximum_short_squeeze_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
         FC_ASSERT( maximum_short_squeeze_ratio <= GRAPHENE_MAX_COLLATERAL_RATIO );
         FC_ASSERT( maintenance_collateral_ratio >= GRAPHENE_MIN_COLLATERAL_RATIO );
         FC_ASSERT( maintenance_collateral_ratio <= maximum_short_squeeze_ratio );
      } FC_CAPTURE_AND_RETHROW( (*this) ) }

      price price_feed::max_short_squeeze_price()const
      {
         asset collateral = settlement_price.quote;
         fc::uint128 tmp( collateral.amount.value );
         tmp *= maximum_short_squeeze_ratio;
         tmp /= 1000;
         FC_ASSERT( tmp <= GRAPHENE_MAX_SHARE_SUPPLY );
         collateral.amount = tmp.to_uint64();
         return settlement_price.base / collateral;
      }
      price price_feed::maintenance_price()const
      {
         asset collateral = settlement_price.quote;
         fc::uint128 tmp( collateral.amount.value );
         tmp *= maintenance_collateral_ratio;
         tmp /= 1000;
         FC_ASSERT( tmp <= GRAPHENE_MAX_SHARE_SUPPLY );
         collateral.amount = tmp.to_uint64();
         return settlement_price.base / collateral;
      }


} } // graphene::chain
