/* Copyright (c) 2015, Cryptonomex, Inc. */
#pragma once
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

   class splitter_object : public abstract_object<splitter_object>
   {
     public:
        static const uint8_t space_id = protocol_ids;
        static const uint8_t type_id  = splitter_object_type;

        account_id_type              owner;
        asset                        balance;
        vector<payment_target>       targets;
        asset                        min_payment;
        share_type                   max_payment;      ///< same asset_id as min_payment
        share_type                   payout_threshold; ///< same asset_id as min_payment


        struct payout_visitor
        {
            typedef void result_type;
            database& db;
            asset     amount;

            payout_visitor( database& d, asset a ):db(d),amount(a){}

            void operator()( const account_id_type& id )const 
            { 
               db.adjust_balance( id(db), amount );
            }
            void operator()( const market_buyback& t )const 
            {
               const auto& new_order_object = db.create<limit_order_object>([&](limit_order_object& obj){
                   obj.seller     = GRAPHENE_NULL_ACCOUNT;
                   obj.for_sale   = amount.amount;
                   obj.sell_price = t.limit_price;
                   assert( amount.asset_id == t.limit_price.base.asset_id );
               });
               db.apply_order(new_order_object);
            }
        };
        void payout( database& db, bool pay_fee = true  )const
        {
           /** this happens when the threshold is used as the trigger, but not when the payout operation is used */
           if( pay_fee )
           {
              const auto& fee_config = db.get_global_properties().parameters.current_fees->get<splitter_payout_operation>();
              asset fee_in_aobj_units = asset(fee_config.fee);
              const asset_object& aobj = min_payment.asset_id(db);
              const asset_dynamic_data_object& aobj_dyn = aobj.dynamic_asset_data_id(db);
              if( aobj.id != asset_id_type() )
              {
                  fee_in_aobj_units =  asset(fee_config.fee) * aobj.options.core_exchange_rate;
                  /// not enough in fee pool to cover 
                  if( aobj_dyn.fee_pool < fee_config.fee ) return;
              }
              /// not enough to cover payout fee, so don't payout.
              if( fee_in_aobj_units > balance ) return;


              db.modify( *this, [&]( splitter_object& obj ){ obj.balance-= fee_in_aobj_units; } );

              if( aobj.id != asset_id_type() )
              {
                 db.modify( aobj_dyn, [&]( asset_dynamic_data_object& obj ){
                            obj.accumulated_fees += fee_in_aobj_units.amount;
                            obj.fee_pool -= fee_config.fee;
                            });
              }

              db.modify( aobj_dyn, [&]( asset_dynamic_data_object& obj ){
                         obj.current_supply -= fee_config.fee;
                         });
           }


           uint64_t total_weight = 0;
           for( auto& t : targets ) total_weight += t.weight;

           asset remaining = balance;

           for( uint32_t i = 0 ; i < targets.size(); ++i )
           {
              const auto& t = targets[i];

              fc::uint128 tmp( balance.amount.value );
              tmp *= t.weight;
              tmp /= total_weight;

              asset payout_amount( tmp.to_uint64(), balance.asset_id );

              if( payout_amount > remaining || (i == (targets.size() - 1)) ) 
                 payout_amount = remaining;

              if( payout_amount.amount > 0 )
              {
                 t.target.visit( payout_visitor( db, payout_amount ) );
              }
              remaining -= payout_amount;
           }
        }
   };

   struct by_account;

   typedef multi_index_container<
      splitter_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_account>, member< splitter_object, account_id_type, &splitter_object::owner > >
      >
   > splitter_multi_index_type;
   typedef generic_index<splitter_object, splitter_multi_index_type>   splitter_index;
   

   struct target_evalautor
   {
      typedef void result_type;
      const database& db;

      target_evalautor( database& d ):db(d){}

      void operator()( const account_id_type& id )const { id(db); }
      void operator()( const market_buyback& t )const 
      {
         /// dereference these objects to verify they exist
         t.asset_to_buy(db);
         t.limit_price.base.asset_id(db);
      }
      
   };

   class splitter_create_evaluator : public evaluator<splitter_create_evaluator>
   {
      public:
         typedef splitter_create_operation operation_type;

         void_result do_evaluate( const splitter_create_operation& o )
         {
            o.owner(db()); // dereference to prove it exists
            for( auto& t : o.targets )
               t.target.visit( target_evalautor(db()) );
            return void_result();
         }

         object_id_type do_apply( const splitter_create_operation& o )
         {
            const auto& new_splitter_object = db().create<splitter_object>( [&]( splitter_object& obj ){
                obj.owner = o.owner;
                obj.targets = o.targets;
                obj.min_payment = o.min_payment;
                obj.max_payment = o.max_payment;
                obj.payout_threshold = o.payout_threshold;
                obj.balance.asset_id = o.min_payment.asset_id;
            });
            return new_splitter_object.id;
         }
   };

   class splitter_update_evaluator : public evaluator<splitter_update_evaluator>
   {
      public:
         typedef splitter_update_operation operation_type;

         void_result do_evaluate( const splitter_update_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            FC_ASSERT( sp.balance.amount == 0 );
            FC_ASSERT( sp.owner == o.owner );

            for( auto& t : o.targets )
               t.target.visit( target_evalautor(db()) );
            return void_result();
         }
         void_result do_apply( const splitter_update_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            db().modify( sp, [&]( splitter_object& obj ){
                obj.targets = o.targets;
                obj.owner   = o.new_owner;
                obj.min_payment = o.min_payment;
                obj.max_payment = o.max_payment;
                obj.payout_threshold = o.payout_threshold;
                obj.balance.asset_id = o.min_payment.asset_id;
            });
            return void_result();
         }
   };

   class splitter_pay_evaluator : public evaluator<splitter_pay_evaluator>
   {
      public:
         typedef splitter_pay_operation operation_type;

         void_result do_evaluate( const splitter_pay_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            FC_ASSERT( o.payment.asset_id == sp.min_payment.asset_id );
            FC_ASSERT( o.payment >= sp.min_payment );
            FC_ASSERT( o.payment.amount <= sp.max_payment );
            return void_result();
         }
         void_result do_apply( const splitter_pay_operation& o )
         {
            db().adjust_balance( o.paying_account, -o.payment );
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            db().modify( sp, [&]( splitter_object& obj ){
                obj.balance += o.payment;
            });

            if( sp.balance.amount > sp.payout_threshold )
               sp.payout(db());

            return void_result();
         }
   };
   class splitter_payout_evaluator : public evaluator<splitter_payout_evaluator>
   {
      public:
         typedef splitter_payout_operation operation_type;

         void_result do_evaluate( const splitter_payout_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            FC_ASSERT( sp.owner == o.owner );
            FC_ASSERT( sp.balance.amount > 0 );
            return void_result();
         }

         void_result do_apply( const splitter_payout_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            sp.payout(db());
            return void_result();
         }
   };
   class splitter_delete_evaluator : public evaluator<splitter_delete_evaluator>
   {
      public:
         typedef splitter_delete_operation operation_type;

         void_result do_evaluate( const splitter_delete_operation& o )
         {
            const auto& sp = o.splitter_id(db()); // dereference to prove it exists
            FC_ASSERT( sp.owner == o.owner );
            FC_ASSERT( sp.balance.amount == 0 );
            return void_result();
         }
         void_result do_apply( const splitter_delete_operation& o )
         {
            db().remove( o.splitter_id(db()) );
            return void_result();
         }
   };

} } // graphene::chain

FC_REFLECT_DERIVED( graphene::chain::splitter_object,
                    (graphene::db::object),
                    (owner)(balance)(targets)(min_payment)(max_payment)(payout_threshold) 
                  )
