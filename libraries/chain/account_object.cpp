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
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

bool account_object::is_authorized_asset(const asset_object& asset_obj) const {
   for( const auto id : blacklisting_accounts )
      if( asset_obj.options.blacklist_authorities.find(id) != asset_obj.options.blacklist_authorities.end() ) return false;

   for( const auto id : whitelisting_accounts )
      if( asset_obj.options.whitelist_authorities.find(id) != asset_obj.options.whitelist_authorities.end() ) return true;
   return false;
}

void account_balance_object::adjust_balance(const asset& delta)
{
   assert(delta.asset_id == asset_type);
   balance += delta.amount;
}

uint16_t account_statistics_object::calculate_bulk_discount_percent(const chain_parameters& params) const
{
   uint64_t bulk_discount_percent = 0;
   if( lifetime_fees_paid >= params.bulk_discount_threshold_max )
      bulk_discount_percent = params.max_bulk_discount_percent_of_fee;
   else if(params.bulk_discount_threshold_max.value !=
           params.bulk_discount_threshold_min.value)
   {
      bulk_discount_percent =
            (params.max_bulk_discount_percent_of_fee *
             (lifetime_fees_paid.value -
              params.bulk_discount_threshold_min.value)) /
            (params.bulk_discount_threshold_max.value -
             params.bulk_discount_threshold_min.value);
   }
   assert( bulk_discount_percent <= GRAPHENE_100_PERCENT );

   return bulk_discount_percent;
}

} } // graphene::chain
