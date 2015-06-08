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
#include <graphene/chain/asset.hpp>
#include <graphene/db/object.hpp>

#include <graphene/db/flat_index.hpp>

#include <fc/static_variant.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;
   class database;

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
    * @brief A worker who burns all of his pay
    *
    * This worker type burns all pay he receives, paying it back to the network's reserve funds pool.
    */
   struct refund_worker_type
   {
      /// Record of how much this worker has burned in his lifetime
      share_type total_burned;

      void pay_worker(share_type pay, database&);

      struct initializer
      {
         void init(database&, const worker_object&, refund_worker_type&)const
         {}
      };
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

      struct initializer
      {
         initializer(uint16_t vesting_period = 0)
            : pay_vesting_period_days(vesting_period) {}

         void init(database& db, const worker_object& obj, vesting_balance_worker_type& worker)const;

         uint16_t pay_vesting_period_days;
      };
   };
   ///@}

   // The ordering of types in these two static variants MUST be the same.
   typedef static_variant<
      refund_worker_type,
      vesting_balance_worker_type
   > worker_type;
   typedef static_variant<
      refund_worker_type::initializer,
      vesting_balance_worker_type::initializer
   > worker_initializer;

   /// @brief A visitor for @ref worker_type which initializes the worker within
   struct worker_initialize_visitor
   {
   private:
      const worker_object& worker_obj;
      const worker_initializer& initializer;
      database& db;

   public:
      worker_initialize_visitor(const worker_object& worker, const worker_initializer& initializer, database& db)
         : worker_obj(worker),initializer(initializer),db(db) {}

      typedef void result_type;
      template<typename WorkerType>
      void operator()( WorkerType& worker)const
      {
         static_assert(worker_type::tag<WorkerType>::value ==
                       worker_initializer::tag<typename WorkerType::initializer>::value,
                       "Tag values for worker_type and worker_initializer do not match! "
                       "Are the types in these static_variants in the same order?");
         initializer.get<typename WorkerType::initializer>().init(db, worker_obj, worker);
      }
   };

   /// @brief A visitor for @ref worker_type which calls pay_worker on the worker within
   struct worker_pay_visitor
   {
   private:
      share_type pay;
      database& db;

   public:
      worker_pay_visitor(share_type pay, database& db)
         : pay(pay), db(db) {}

      typedef void result_type;
      template<typename W>
      void operator()(W& worker)const
      {
         worker.pay_worker(pay, db);
      }
   };

   /**
    * @brief Worker object contains the details of a blockchain worker. See @ref workers for details.
    */
   class worker_object : public abstract_object<worker_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id =  worker_object_type;

         /// ID of the account which owns this worker
         account_id_type   worker_account;
         /// Time at which this worker begins receiving pay, if elected
         time_point_sec    work_begin_date;
         /// Time at which this worker will cease to receive pay. Worker will be deleted at this time
         time_point_sec    work_end_date;
         /// Amount in CORE this worker will be paid each day
         share_type        daily_pay;
         /// ID of this worker's pay balance
         worker_type       worker;

         /// Voting ID which represents approval of this worker
         vote_id_type                   vote_for;
         /// Voting ID which represents disapproval of this worker
         vote_id_type                   vote_against;

         bool is_active(fc::time_point_sec now)const {
            return now >= work_begin_date && now <= work_end_date;
         }
         share_type approving_stake(const vector<uint64_t>& stake_vote_tallies)const {
            return stake_vote_tallies[vote_for] - stake_vote_tallies[vote_against];
         }
   };

   typedef flat_index<worker_object> worker_index;

} } // graphene::chain

FC_REFLECT( graphene::chain::refund_worker_type, (total_burned) )
FC_REFLECT( graphene::chain::refund_worker_type::initializer, )
FC_REFLECT( graphene::chain::vesting_balance_worker_type, (balance) )
FC_REFLECT( graphene::chain::vesting_balance_worker_type::initializer, (pay_vesting_period_days) )
FC_REFLECT_DERIVED( graphene::chain::worker_object, (graphene::db::object),
                    (worker_account)
                    (work_begin_date)
                    (work_end_date)
                    (daily_pay)
                    (worker)
                    (vote_for)
                    (vote_against)
                  )
