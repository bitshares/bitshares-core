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
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/block.hpp>
#include <graphene/chain/asset.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/fork_database.hpp>
#include <graphene/chain/block_database.hpp>

#include <graphene/db/object_database.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/simple_index.hpp>
#include <fc/signals.hpp>

#include <fc/log/logger.hpp>

#include <map>

namespace graphene { namespace chain {
   using graphene::db::abstract_object;
   using graphene::db::object;

   typedef vector<std::pair<fc::static_variant<address, public_key_type>, share_type >> genesis_allocation;

   /**
    *   @class database
    *   @brief tracks the blockchain state in an extensible manner
    */
   class database : public object_database
   {
      public:
         //////////////////// db_management.cpp ////////////////////

         database();
         ~database();

         enum validation_steps
         {
            skip_nothing                = 0x00,
            skip_delegate_signature     = 0x01,  ///< used while reindexing
            skip_transaction_signatures = 0x02,  ///< used by non delegate nodes
            skip_undo_block             = 0x04,  ///< used while reindexing
            skip_undo_transaction       = 0x08,  ///< used while applying block
            skip_transaction_dupe_check = 0x10,  ///< used while reindexing
            skip_fork_db                = 0x20,  ///< used while reindexing
            skip_block_size_check       = 0x40,  ///< used when applying locally generated transactions
            skip_tapos_check            = 0x80,  ///< used while reindexing -- note this skips expiration check as well
            skip_authority_check        = 0x100, ///< used while reindexing -- disables any checking of authority on transactions
            skip_merkle_check           = 0x200  ///< used while reindexing
         };

         void open(const fc::path& data_dir, const genesis_allocation& initial_allocation = genesis_allocation());
         /**
          * @brief Rebuild object graph from block history and open detabase
          *
          * This method may be called after or instead of @ref database::open, and will rebuild the object graph by
          * replaying blockchain history. When this method exits successfully, the database will be open.
          */
         void reindex(fc::path data_dir, const genesis_allocation& initial_allocation = genesis_allocation());

         /**
          * @brief wipe Delete database from disk, and potentially the raw chain as well.
          * @param include_blocks If true, delete the raw chain as well as the database.
          *
          * Will close the database before wiping. Database will be closed when this function returns.
          */
         void wipe(const fc::path& data_dir, bool include_blocks);
         void close(uint32_t blocks_to_rewind = 0);

         //////////////////// db_block.cpp ////////////////////

         /**
          *  @return true if the block is in our fork DB or saved to disk as
          *  part of the official chain, otherwise return false
          */
         bool                       is_known_block( const block_id_type& id )const;
         bool                       is_known_transaction( const transaction_id_type& id )const;
         block_id_type              get_block_id_for_num( uint32_t block_num )const;
         optional<signed_block>     fetch_block_by_id( const block_id_type& id )const;
         optional<signed_block>     fetch_block_by_number( uint32_t num )const;
         const signed_transaction&  get_recent_transaction( const transaction_id_type& trx_id )const;

         bool push_block( const signed_block& b, uint32_t skip = skip_nothing );
         processed_transaction push_transaction( const signed_transaction& trx, uint32_t skip = skip_nothing );
         ///@throws fc::exception if the proposed transaction fails to apply.
         processed_transaction push_proposal( const proposal_object& proposal );

         signed_block generate_block(
            const fc::time_point_sec when,
            witness_id_type witness_id,
            const fc::ecc::private_key& block_signing_private_key,
            uint32_t skip = 0
            );

         void pop_block();
         void clear_pending();

         /**
          *  This method is used to track appied operations during the evaluation of a block, these
          *  operations should include any operation actually included in a transaction as well
          *  as any implied/virtual operations that resulted, such as filling an order.  The
          *  applied operations is cleared after applying each block and calling the block
          *  observers which may want to index these operations.
          *
          *  @return the op_id which can be used to set the result after it has finished being applied.
          */
         uint32_t  push_applied_operation( const operation& op );
         void      set_applied_operation_result( uint32_t op_id, const operation_result& r );
         const vector<operation_history_object>& get_applied_operations()const;

         /**
          *  This signal is emitted after all operations and virtual operation for a
          *  block have been applied but before the get_applied_operations() are cleared.
          *
          *  You may not yield from this callback because the blockchain is holding
          *  the write lock and may be in an "inconstant state" until after it is
          *  released.
          */
         fc::signal<void(const signed_block&)>           applied_block;

         /**
          *  After a block has been applied and committed.  The callback
          *  should not yield and should execute quickly.
          */
         fc::signal<void(const vector<object_id_type>&)> changed_objects;

         //////////////////// db_witness_schedule.cpp ////////////////////

         /**
          * @brief Get the witness scheduled for block production in a slot.
          *
          * slot_num always corresponds to a time in the future.
          *
          * If slot_num == 1, returns the next scheduled witness.
          * If slot_num == 2, returns the next scheduled witness after
          * 1 block gap.
          *
          * Use the get_slot_time() and get_slot_at_time() functions
          * to convert between slot_num and timestamp.
          *
          * Passing slot_num == 0 returns (witness_id_type(), false)
          *
          * The bool value is true if near schedule, false if far schedule.
          */
         pair<witness_id_type, bool> get_scheduled_witness(uint32_t slot_num)const;

         /**
          * Get the time at which the given slot occurs.
          *
          * If slot_num == 0, return time_point_sec().
          *
          * If slot_num == N for N > 0, return the Nth next
          * block-interval-aligned time greater than head_block_time().
          */
         fc::time_point_sec get_slot_time(uint32_t slot_num)const;

         /**
          * Get the last slot which occurs AT or BEFORE the given time.
          *
          * The return value is the greatest value N such that
          * get_slot_time( N ) <= when.
          *
          * If no such N exists, return 0.
          */
         uint32_t get_slot_at_time(fc::time_point_sec when)const;

         /**
          * Get the near schedule.
          */
         vector<witness_id_type> get_near_witness_schedule()const;

         //////////////////// db_getter.cpp ////////////////////

         const asset_object&                    get_core_asset()const;
         const global_property_object&          get_global_properties()const;
         const dynamic_global_property_object&  get_dynamic_global_properties()const;
         const fee_schedule_type&               current_fee_schedule()const;

         time_point_sec head_block_time()const;
         uint32_t       head_block_num()const;
         block_id_type  head_block_id()const;

         decltype( chain_parameters::block_interval ) block_interval( )const;

         //////////////////// db_init.cpp ////////////////////

         void initialize_evaluators();
         /// Reset the object graph in-memory
         void initialize_indexes();
         void init_genesis(const genesis_allocation& initial_allocation = genesis_allocation());

         template<typename EvaluatorType>
         void register_evaluator()
         {
            _operation_evaluators[
               operation::tag<typename EvaluatorType::operation_type>::value].reset( new op_evaluator_impl<EvaluatorType>() );
         }

         template<typename EvaluatorType>
         void register_evaluation_observer( evaluation_observer& observer )
         {
            unique_ptr<op_evaluator>& op_eval = _operation_evaluators[operation::tag<typename EvaluatorType::operation_type>::value];
            op_eval->eval_observers.push_back( &observer );
            return;
         }

         //////////////////// db_balance.cpp ////////////////////

         /**
          * @brief Retrieve a particular account's balance in a given asset
          * @param owner Account whose balance should be retrieved
          * @param asset_id ID of the asset to get balance in
          * @return owner's balance in asset
          */
         asset get_balance(account_id_type owner, asset_id_type asset_id)const;
         /// This is an overloaded method.
         asset get_balance(const account_object& owner, const asset_object& asset_obj)const;
         /// This is an overloaded method.
         asset get_balance(const account_object* owner, const asset_object* asset_obj)const;

         /**
          * @brief Adjust a particular account's balance in a given asset by a delta
          * @param account ID of account whose balance should be adjusted
          * @param delta Asset ID and amount to adjust balance by
          */
         void adjust_balance(account_id_type account, asset delta);
         /// This is an overloaded method.
         void adjust_balance(const account_object& account, asset delta);
         /// This is an overloaded method.
         void adjust_balance(const account_object* account, asset delta);

         /**
          * If delta.asset_id is a core asset, adjusts account statistics
          */
         void adjust_core_in_orders( const account_object& acnt, asset delta );

         // helper to handle cashback rewards
         void deposit_cashback(const account_object& acct, share_type amount, bool require_vesting = true);

         //////////////////// db_debug.cpp ////////////////////

         void debug_dump();

         //////////////////// db_market.cpp ////////////////////

         /// @{ @group Market Helpers
         void globally_settle_asset( const asset_object& bitasset, const price& settle_price );
         void cancel_order(const force_settlement_object& order, bool create_virtual_op = true);
         void cancel_order(const limit_order_object& order, bool create_virtual_op = true);

         /**
          * Matches the two orders,
          *
          * @return a bit field indicating which orders were filled (and thus removed)
          *
          * 0 - no orders were matched
          * 1 - bid was filled
          * 2 - ask was filled
          * 3 - both were filled
          */
         ///@{
         template<typename OrderType>
         int match( const limit_order_object& bid, const OrderType& ask, const price& match_price );
         int match( const limit_order_object& bid, const limit_order_object& ask, const price& trade_price );
         int match( const limit_order_object& bid, const short_order_object& ask, const price& trade_price );
         /// @return the amount of asset settled
         asset match(const call_order_object& call,
                   const force_settlement_object& settle,
                   const price& match_price,
                   asset max_settlement);
         ///@}

         /**
          * @return true if the order was completely filled and thus freed.
          */
         bool fill_order( const limit_order_object& order, const asset& pays, const asset& receives );
         bool fill_order( const short_order_object& order, const asset& pays, const asset& receives );
         bool fill_order( const call_order_object& order, const asset& pays, const asset& receives );
         bool fill_order( const force_settlement_object& settle, const asset& pays, const asset& receives );

         bool check_call_orders( const asset_object& mia );

         // helpers to fill_order
         void pay_order( const account_object& receiver, const asset& receives, const asset& pays );

         bool convert_fees( const asset_object& mia );
         asset calculate_market_fee(const asset_object& recv_asset, const asset& trade_amount);
         asset pay_market_fees( const asset_object& recv_asset, const asset& receives );

         ///@}

         /**
          * @}
          */
   protected:
         //Mark pop_undo() as protected -- we do not want outside calling pop_undo(); it should call pop_block() instead
         void pop_undo() { object_database::pop_undo(); }

      private:
         optional<undo_database::session>       _pending_block_session;
         vector< unique_ptr<op_evaluator> >     _operation_evaluators;

         template<class ObjectType>
         vector<std::reference_wrapper<const ObjectType>> sort_votable_objects(size_t count)const;

         //////////////////// db_block.cpp ////////////////////

         void                  apply_block( const signed_block& next_block, uint32_t skip = skip_nothing );
         processed_transaction apply_transaction( const signed_transaction& trx, uint32_t skip = skip_nothing );
         operation_result      apply_operation( transaction_evaluation_state& eval_state, const operation& op );

         ///Steps involved in applying a new block
         ///@{

         const witness_object& validate_block_header( uint32_t skip, const signed_block& next_block )const;
         void create_block_summary(const signed_block& next_block);

         //////////////////// db_update.cpp ////////////////////
         void update_global_dynamic_data( const signed_block& b );
         void update_signing_witness(const witness_object& signing_witness, const signed_block& new_block);
         void update_pending_block(const signed_block& next_block, uint8_t current_block_interval);
         void clear_expired_transactions();
         void clear_expired_proposals();
         void clear_expired_orders();
         void update_expired_feeds();
         void update_withdraw_permissions();

         //////////////////// db_witness_schedule.cpp ////////////////////
         void update_witness_schedule(signed_block next_block);    /// no-op except for scheduling blocks

         ///Steps performed only at maintenance intervals
         ///@{

         //////////////////// db_maint.cpp ////////////////////

         share_type get_max_budget( fc::time_point_sec now )const;
         void process_budget();
         void pay_workers( share_type& budget );
         void perform_chain_maintenance(const signed_block& next_block, const global_property_object& global_props);
         void update_active_witnesses();
         void update_active_delegates();

         template<class... Types>
         void perform_account_maintenance(std::tuple<Types...> helpers);
         ///@}
         ///@}

         signed_block                           _pending_block;
         fork_database                          _fork_db;

         /**
          *  Note: we can probably store blocks by block num rather than
          *  block id because after the undo window is past the block ID
          *  is no longer relevant and its number is irreversible.
          *
          *  During the "fork window" we can cache blocks in memory
          *  until the fork is resolved.  This should make maintaining
          *  the fork tree relatively simple.
          */
         block_database   _block_id_to_block;

         /**
          * Contains the set of ops that are in the process of being applied from
          * the current block.  It contains real and virtual operations in the
          * order they occur and is cleared after the applied_block signal is
          * emited.
          */
         vector<operation_history_object>  _applied_ops;

         uint32_t                          _current_block_num    = 0;
         uint16_t                          _current_trx_in_block = 0;
         uint16_t                          _current_op_in_trx    = 0;
         uint16_t                          _current_virtual_op   = 0;

         vector<uint64_t>                  _vote_tally_buffer;
         vector<uint64_t>                  _witness_count_histogram_buffer;
         vector<uint64_t>                  _committee_count_histogram_buffer;
         uint64_t                          _total_voting_stake;
   };

   namespace detail
   {
       template<int... Is>
       struct seq { };

       template<int N, int... Is>
       struct gen_seq : gen_seq<N - 1, N - 1, Is...> { };

       template<int... Is>
       struct gen_seq<0, Is...> : seq<Is...> { };

       template<typename T, int... Is>
       void for_each(T&& t, const account_object& a, seq<Is...>)
       {
           auto l = { (std::get<Is>(t)(a), 0)... };
           (void)l;
       }
   }
   template<class... Types>
   void database::perform_account_maintenance(std::tuple<Types...> helpers)
   {
      const auto& idx = get_index_type<account_index>().indices();
      for( const account_object& a : idx )
         detail::for_each(helpers, a, detail::gen_seq<sizeof...(Types)>());
   }

} }
