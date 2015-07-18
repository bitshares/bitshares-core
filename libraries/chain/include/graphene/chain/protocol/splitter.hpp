/* Copyright (c) 2015, Cryptonomex, Inc. */
#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain {

   struct market_buyback
   {
      asset_id_type asset_to_buy;
      price         limit_price;
      void validate()const { 
         limit_price.validate();
         FC_ASSERT( limit_price.quote.asset_id == asset_to_buy );
      }
   };

   typedef static_variant<account_id_type,market_buyback> payment_target_type;

   struct payment_target
   {
      uint16_t             weight = 0;
      payment_target_type  target;
   };

   struct payment_target_validate
   {
      typedef void result_type;
      void operator()( const account_id_type& id )const { }
      void operator()( const market_buyback& t )const 
      {
         FC_ASSERT( t.asset_to_buy == t.limit_price.quote.asset_id );
         t.limit_price.validate();
      }
   };

   struct splitter_create_operation : public base_operation
   {
      /// TODO: charge fee based upon size
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                        fee;
      account_id_type              payer;
      account_id_type              owner;
      vector<payment_target>       targets;
      asset                        min_payment;
      share_type                   max_payment;      ///< same asset_id as min_payment
      share_type                   payout_threshold; ///< same asset_id as min_payment

      void            validate()const
      {
         FC_ASSERT( fee.amount >= 0 );
         FC_ASSERT( min_payment.amount > 0 );
         FC_ASSERT( min_payment.amount <= max_payment );
         FC_ASSERT( payout_threshold >= 0 );
         for( const auto& t : targets ) 
         {
            FC_ASSERT( t.weight > 0 );
            t.target.visit( payment_target_validate() );
         }
      }
      account_id_type fee_payer()const { return payer; }
   };

   struct splitter_update_operation : public base_operation
   {
      /// TODO: charge fee based upon size
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;
      splitter_id_type         splitter_id;
      account_id_type          owner; ///< must match splitter_id->owner
      account_id_type          new_owner;
      vector<payment_target>   targets;
      asset                    min_payment;
      share_type               max_payment;      ///< same asset_id as min_payment
      share_type               payout_threshold; ///< same asset_id as min_payment

      void            validate()const
      {
         FC_ASSERT( fee.amount >= 0 );
         FC_ASSERT( min_payment.amount > 0 );
         FC_ASSERT( min_payment.amount <= max_payment );
         FC_ASSERT( payout_threshold >= 0 );
         for( const auto& t : targets ) FC_ASSERT( t.weight > 0 );
      }

      account_id_type fee_payer()const { return owner; }
   };

   struct splitter_pay_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;
      splitter_id_type         splitter_id;
      account_id_type          paying_account; ///< also fee payer
      asset                    payment;

      void            validate()const
      {
         FC_ASSERT( payment.amount > 0 );
         FC_ASSERT( fee.amount >= 0 );
      }

      account_id_type fee_payer()const { return paying_account; }
   };

   struct splitter_payout_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;
      splitter_id_type         splitter_id;
      account_id_type          owner; ///< must match splitter_id->owner

      void            validate()const { FC_ASSERT( fee.amount >= 0 ); }
      account_id_type fee_payer()const { return owner; }
   };


   struct splitter_delete_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset                    fee;
      splitter_id_type         splitter_id;
      account_id_type          owner; ///< must match splitter_id->owner

      void            validate()const { FC_ASSERT( fee.amount >= 0 ); }
      account_id_type fee_payer()const { return owner; }
   };

} }

FC_REFLECT( graphene::chain::market_buyback, (asset_to_buy)(limit_price) )
FC_REFLECT( graphene::chain::payment_target, (weight)(target) )
FC_REFLECT( graphene::chain::splitter_create_operation, (fee)(payer)(owner)(targets)(min_payment)(max_payment)(payout_threshold) )
FC_REFLECT( graphene::chain::splitter_update_operation, (fee)(owner)(new_owner)(targets)(min_payment)(max_payment)(payout_threshold) )
FC_REFLECT( graphene::chain::splitter_pay_operation, (fee)(splitter_id)(paying_account)(payment) )
FC_REFLECT( graphene::chain::splitter_payout_operation, (fee)(splitter_id)(owner) )
FC_REFLECT( graphene::chain::splitter_delete_operation, (fee)(splitter_id)(owner) )
FC_REFLECT( graphene::chain::splitter_create_operation::fee_parameters_type, (fee) );
FC_REFLECT( graphene::chain::splitter_update_operation::fee_parameters_type, (fee) );
FC_REFLECT( graphene::chain::splitter_pay_operation::fee_parameters_type, (fee) );
FC_REFLECT( graphene::chain::splitter_payout_operation::fee_parameters_type, (fee) );
FC_REFLECT( graphene::chain::splitter_delete_operation::fee_parameters_type, (fee) );

