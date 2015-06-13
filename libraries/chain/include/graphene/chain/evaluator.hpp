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
#pragma once
#include <graphene/chain/operations.hpp>
#include <graphene/chain/authority.hpp>

namespace graphene { namespace chain {

   class database;
   class generic_evaluator;
   class transaction_evaluation_state;

   /**
    * Observes evaluation events, providing
    * pre- and post-evaluation hooks.
    *
    * Every call to pre_evaluate() is followed by
    * a call to either post_evaluate() or evaluation_failed().
    *
    * A subclass which needs to do a "diff" can gather some
    * "before" state into its members in pre_evaluate(),
    * then post_evaluate() will have both "before"
    * and "after" state, and will be able to do the diff.
    *
    * evaluation_failed() is a cleanup method which notifies
    * the subclass to "throw away" the diff.
    */
   class evaluation_observer
   {
      public:
         virtual ~evaluation_observer(){}

         virtual void pre_evaluate(
             const transaction_evaluation_state& eval_state,
             const operation& op,
             bool apply,
             generic_evaluator* ge ) {}

         virtual void post_evaluate(
             const transaction_evaluation_state& eval_state,
             const operation& op,
             bool apply,
             generic_evaluator* ge,
             const operation_result& result ) {}

         virtual void evaluation_failed(
             const transaction_evaluation_state& eval_state,
             const operation& op,
             bool apply,
             generic_evaluator* ge,
             const operation_result& result ) {}
   };

   class generic_evaluator
   {
      public:
         virtual ~generic_evaluator(){}

         virtual int get_type()const = 0;
         virtual operation_result start_evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply  );

         /** @note derived classes should ASSUME that the default validation that is
          * indepenent of chain state should be performed by op.validate() and should
          * not perform these extra checks.
          */
         virtual operation_result evaluate( const operation& op ) = 0;
         virtual operation_result apply( const operation& op ) = 0;

         database& db()const;

         void check_required_authorities(const operation& op);
   protected:
         /**
          * @brief Fetch objects relevant to fee payer and set pointer members
          * @param account_id Account which is paying the fee
          * @param fee The fee being paid. May be in assets other than core.
          *
          * This method verifies that the fee is valid and sets the object pointer members and the fee fields. It should
          * be called during do_evaluate.
          */
         void prepare_fee(account_id_type account_id, asset fee);
         /// Pays the fee and returns the number of CORE asset that were paid.
         void pay_fee();

         bool       verify_authority( const account_object&, authority::classification );
         //bool       verify_signature( const key_object& );

         object_id_type get_relative_id( object_id_type rel_id )const;

         authority resolve_relative_ids( const authority& a )const;

         asset                            fee_from_account;
         share_type                       core_fee_paid;
         const account_object*            fee_paying_account = nullptr;
         const account_statistics_object* fee_paying_account_statistics = nullptr;
         const asset_object*              fee_asset          = nullptr;
         const asset_dynamic_data_object* fee_asset_dyn_data = nullptr;
         transaction_evaluation_state*    trx_state;
   };

   class op_evaluator
   {
      public:
         virtual ~op_evaluator(){}
         virtual operation_result evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply ) = 0;

         vector< evaluation_observer* > eval_observers;
   };

   template<typename T>
   class op_evaluator_impl : public op_evaluator
   {
      public:
         virtual operation_result evaluate( transaction_evaluation_state& eval_state, const operation& op, bool apply = true ) override
         {
             // fc::exception from observers are suppressed.
             // fc::exception from evaluation is deferred (re-thrown
             // after all observers receive evaluation_failed)

             T eval;
             optional< fc::exception > evaluation_exception;
             size_t observer_count = 0;
             operation_result result;

             for( const auto& obs : eval_observers )
             {
                try
                {
                   obs->pre_evaluate( eval_state, op, apply, &eval );
                }
                catch( const fc::exception& e )
                {
                   elog( "suppressed exception in observer pre method:\n${e}", ( "e", e.to_detail_string() ) );
                }
                observer_count++;
             }

             try
             {
                result = eval.start_evaluate( eval_state, op, apply );
             }
             catch( const fc::exception& e )
             {
                evaluation_exception = e;
             }

             while( observer_count > 0 )
             {
                --observer_count;
                const auto& obs = eval_observers[ observer_count ];
                try
                {
                   if( !evaluation_exception.valid() )
                      obs->post_evaluate( eval_state, op, apply, &eval, result );
                   else
                      obs->evaluation_failed( eval_state, op, apply, &eval, result );
                }
                catch( const fc::exception& e )
                {
                   elog( "suppressed exception in observer post method:\n${e}", ( "e", e.to_detail_string() ) );
                }
             }

             if( evaluation_exception.valid() )
                throw *evaluation_exception;
             return result;
         }
   };

   template<typename DerivedEvaluator>
   class evaluator : public generic_evaluator
   {
      public:
         virtual int get_type()const override { return operation::tag<typename DerivedEvaluator::operation_type>::value; }

         virtual operation_result evaluate( const operation& o ) final override
         {
            auto* eval = static_cast<DerivedEvaluator*>(this);
            const auto& op = o.get<typename DerivedEvaluator::operation_type>();

            prepare_fee(op.fee_payer(), op.fee);
            FC_ASSERT( core_fee_paid >= op.calculate_fee(db().current_fee_schedule()) );

            return eval->do_evaluate( op );
         }
         virtual operation_result apply( const operation& o ) final override
         {
            auto* eval = static_cast<DerivedEvaluator*>(this);
            const auto& op = o.get<typename DerivedEvaluator::operation_type>();

            pay_fee();

            auto result = eval->do_apply( op );

            db().adjust_balance(op.fee_payer(), -fee_from_account);

            return result;
         }
   };
} }
