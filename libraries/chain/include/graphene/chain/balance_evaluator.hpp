#pragma once
#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {

   /**
    *  @ingroup operations
    */
   class balance_claim_evaluator : public evaluator<balance_claim_evaluator>
   {
      public:
         typedef balance_claim_operation operation_type;

         void_result do_evaluate( const balance_claim_operation& op )
         {
            return void_result();
         }

         /**
          *  @note the fee is always 0 for this particular operation because once the
          *  balance is claimed it frees up memory and it cannot be used to spam the network
          */
         void_result do_apply( const balance_claim_operation& op )
         {
            const auto& bal_idx = db().get_index_type<balance_index>();
            const auto& by_owner_idx = bal_idx.indices().get<by_owner>();

            asset total(0, op.total_claimed.asset_id);
            for( const auto& owner : op.owners )
            {
               auto itr = by_owner_idx.find( boost::make_tuple( owner, total.asset_id ) );
               if( itr != by_owner_idx.end() )
               {
                  total += itr->balance;
                  db().remove( *itr );
               }
            }

            FC_ASSERT( total == op.total_claimed, "", ("total",total)("op",op) );

            db().adjust_balance( op.deposit_to_account, total );

            return void_result();
         }
   };

} } // graphene::chain
