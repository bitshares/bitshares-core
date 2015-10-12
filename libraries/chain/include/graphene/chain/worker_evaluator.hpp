/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {

   /**
     * @defgroup worker_types Implementations of the various worker types in the system
     *
     * The system has various worker types, which do different things with the money they are paid. These worker types
     * and their semantics are specified here.
     *
     * All worker types exist as a struct containing the data this worker needs to evaluate, as well as a method
     * pay_worker, which takes a pay amount and a non-const database reference, and applies the worker's specific pay
     * semantics to the worker_type struct and/or the database. Furthermore, all worker types have an initializer,
     * which is a struct containing the data needed to create that kind of worker.
     *
     * Each initializer type has a method, init, which takes a non-const database reference, a const reference to the
     * worker object being created, and a non-const reference to the specific *_worker_type object to initialize. The
     * init method creates any further objects, and initializes the worker_type object as necessary according to the
     * semantics of that particular worker type.
     *
     * To create a new worker type, define a my_new_worker_type struct with a pay_worker method which updates the
     * my_new_worker_type object and/or the database. Create a my_new_worker_type::initializer struct with an init
     * method and any data members necessary to create a new worker of this type. Reflect my_new_worker_type and
     * my_new_worker_type::initializer into FC's type system, and add them to @ref worker_type and @ref
     * worker_initializer respectively. Make sure the order of types in @ref worker_type and @ref worker_initializer
     * remains the same.
     * @{
     */
   /**
    * @brief A worker who returns all of his pay to the reserve
    *
    * This worker type pays everything he receives back to the network's reserve funds pool.
    */
   struct refund_worker_type
   {
      /// Record of how much this worker has burned in his lifetime
      share_type total_burned;

      void pay_worker(share_type pay, database&);
   };

   /**
    * @brief A worker who sends his pay to a vesting balance
    *
    * This worker type takes all of his pay and places it into a vesting balance
    */
   struct vesting_balance_worker_type
   {
      /// The balance this worker pays into
      vesting_balance_id_type balance;

      void pay_worker(share_type pay, database& db);
   };

   /**
    * @brief A worker who permanently destroys all of his pay
    *
    * This worker sends all pay he receives to the null account.
    */
   struct burn_worker_type
   {
      /// Record of how much this worker has burned in his lifetime
      share_type total_burned;

      void pay_worker(share_type pay, database&);
   };
   ///@}

   // The ordering of types in these two static variants MUST be the same.
   typedef static_variant<
      refund_worker_type,
      vesting_balance_worker_type,
      burn_worker_type
   > worker_type;


   /**
    * @brief Worker object contains the details of a blockchain worker. See @ref workers for details.
    */
   class worker_object : public abstract_object<worker_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id =  worker_object_type;

         /// ID of the account which owns this worker
         account_id_type worker_account;
         /// Time at which this worker begins receiving pay, if elected
         time_point_sec work_begin_date;
         /// Time at which this worker will cease to receive pay. Worker will be deleted at this time
         time_point_sec work_end_date;
         /// Amount in CORE this worker will be paid each day
         share_type daily_pay;
         /// ID of this worker's pay balance
         worker_type worker;
         /// Human-readable name for the worker
         string name;
         /// URL to a web page representing this worker
         string url;

         /// Voting ID which represents approval of this worker
         vote_id_type vote_for;
         /// Voting ID which represents disapproval of this worker
         vote_id_type vote_against;

         uint64_t total_votes_for = 0;
         uint64_t total_votes_against = 0;

         bool is_active(fc::time_point_sec now)const {
            return now >= work_begin_date && now <= work_end_date;
         }

         /// TODO: remove unused argument
         share_type approving_stake(const vector<uint64_t>& stake_vote_tallies)const {
            return int64_t( total_votes_for ) - int64_t( total_votes_against );
         }
   };


   struct by_account;
   struct by_vote_for;
   struct by_vote_against;
   typedef multi_index_container<
      worker_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_account>, member< worker_object, account_id_type, &worker_object::worker_account > >,
         ordered_unique< tag<by_vote_for>, member< worker_object, vote_id_type, &worker_object::vote_for > >,
         ordered_unique< tag<by_vote_against>, member< worker_object, vote_id_type, &worker_object::vote_against > >
      >
   > worker_object_multi_index_type;

   //typedef flat_index<worker_object> worker_index;
   using worker_index = generic_index<worker_object, worker_object_multi_index_type>;

   class worker_create_evaluator : public evaluator<worker_create_evaluator>
   {
      public:
         typedef worker_create_operation operation_type;

         void_result do_evaluate( const operation_type& o );
         object_id_type do_apply( const operation_type& o );
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::refund_worker_type, (total_burned) )
FC_REFLECT( graphene::chain::vesting_balance_worker_type, (balance) )
FC_REFLECT( graphene::chain::burn_worker_type, (total_burned) )
FC_REFLECT_TYPENAME( graphene::chain::worker_type )
FC_REFLECT_DERIVED( graphene::chain::worker_object, (graphene::db::object),
                    (worker_account)
                    (work_begin_date)
                    (work_end_date)
                    (daily_pay)
                    (worker)
                    (vote_for)
                    (vote_against)
                    (total_votes_for)
                    (total_votes_against)
                    (name)
                    (url)
                  )
