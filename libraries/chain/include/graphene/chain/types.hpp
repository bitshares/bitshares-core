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
#include <fc/container/flat_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>
#include <fc/io/raw.hpp>
#include <memory>
#include <vector>
#include <deque>
#include <cstdint>
#include <graphene/chain/address.hpp>
#include <graphene/db/object_id.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   using                               std::map;
   using                               std::vector;
   using                               std::unordered_map;
   using                               std::string;
   using                               std::deque;
   using                               std::shared_ptr;
   using                               std::weak_ptr;
   using                               std::unique_ptr;
   using                               std::set;
   using                               std::pair;
   using                               std::enable_shared_from_this;
   using                               std::tie;
   using                               std::make_pair;

   using                               fc::variant_object;
   using                               fc::variant;
   using                               fc::enum_type;
   using                               fc::optional;
   using                               fc::unsigned_int;
   using                               fc::signed_int;
   using                               fc::time_point_sec;
   using                               fc::time_point;
   using                               fc::safe;
   using                               fc::flat_map;
   using                               fc::flat_set;
   using                               fc::static_variant;
   using                               fc::ecc::range_proof_type;
   using                               fc::ecc::range_proof_info;
   using                               fc::ecc::commitment_type;

   typedef fc::ecc::private_key        private_key_type;

   enum asset_issuer_permission_flags
   {
      charge_market_fee    = 0x01, /**< an issuer-specified percentage of all market trades in this asset is paid to the issuer */
      white_list           = 0x02, /**< accounts must be whitelisted in order to hold this asset */
      override_authority   = 0x04, /**< @todo issuer may transfer asset back to himself */
      transfer_restricted  = 0x08, /**< require the issuer to be one party to every transfer */
      disable_force_settle = 0x10, /**< disable force settling */
      global_settle        = 0x20  /**< allow the bitasset issuer to force a global settling -- this may be set in permissions, but not flags */
   };
   const static uint32_t ASSET_ISSUER_PERMISSION_MASK = charge_market_fee|white_list|override_authority|transfer_restricted|disable_force_settle|global_settle;
   const static uint32_t UIA_ASSET_ISSUER_PERMISSION_MASK = charge_market_fee|white_list|override_authority|transfer_restricted;

   enum reserved_spaces
   {
      relative_protocol_ids = 0,
      protocol_ids          = 1,
      implementation_ids    = 2
   };

   inline bool is_relative( object_id_type o ){ return o.space() == 0; }

   /**
    *  List all object types from all namespaces here so they can
    *  be easily reflected and displayed in debug output.  If a 3rd party
    *  wants to extend the core code then they will have to change the
    *  packed_object::type field from enum_type to uint16 to avoid
    *  warnings when converting packed_objects to/from json.
    */
   enum object_type
   {
      null_object_type,
      base_object_type,
      account_object_type,
      asset_object_type,
      force_settlement_object_type,
      delegate_object_type,
      witness_object_type,
      limit_order_object_type,
      call_order_object_type,
      custom_object_type,
      proposal_object_type,
      operation_history_object_type,
      withdraw_permission_object_type,
      vesting_balance_object_type,
      worker_object_type,
      balance_object_type,
      OBJECT_TYPE_COUNT ///< Sentry value which contains the number of different object types
   };

   enum impl_object_type
   {
      impl_global_property_object_type,
      impl_dynamic_global_property_object_type,
      impl_index_meta_object_type,
      impl_asset_dynamic_data_type,
      impl_asset_bitasset_data_type,
      impl_delegate_feeds_object_type,
      impl_account_balance_object_type,
      impl_account_statistics_object_type,
      impl_account_debt_object_type,
      impl_transaction_object_type,
      impl_block_summary_object_type,
      impl_account_transaction_history_object_type,
      impl_witness_schedule_object_type
   };

   enum meta_info_object_type
   {
      meta_asset_object_type,
      meta_account_object_type
   };


   //typedef fc::unsigned_int            object_id_type;
   //typedef uint64_t                    object_id_type;
   class account_object;
   class delegate_object;
   class witness_object;
   class asset_object;
   class force_settlement_object;
   class limit_order_object;
   class call_order_object;
   class custom_object;
   class proposal_object;
   class operation_history_object;
   class withdraw_permission_object;
   class vesting_balance_object;
   class witness_schedule_object;
   class worker_object;
   class balance_object;

   typedef object_id< protocol_ids, account_object_type,            account_object>               account_id_type;
   typedef object_id< protocol_ids, asset_object_type,              asset_object>                 asset_id_type;
   typedef object_id< protocol_ids, force_settlement_object_type,   force_settlement_object>      force_settlement_id_type;
   typedef object_id< protocol_ids, delegate_object_type,           delegate_object>              delegate_id_type;
   typedef object_id< protocol_ids, witness_object_type,            witness_object>               witness_id_type;
   typedef object_id< protocol_ids, limit_order_object_type,        limit_order_object>           limit_order_id_type;
   typedef object_id< protocol_ids, call_order_object_type,         call_order_object>            call_order_id_type;
   typedef object_id< protocol_ids, custom_object_type,             custom_object>                custom_id_type;
   typedef object_id< protocol_ids, proposal_object_type,           proposal_object>              proposal_id_type;
   typedef object_id< protocol_ids, operation_history_object_type,  operation_history_object>     operation_history_id_type;
   typedef object_id< protocol_ids, withdraw_permission_object_type,withdraw_permission_object>   withdraw_permission_id_type;
   typedef object_id< protocol_ids, vesting_balance_object_type,    vesting_balance_object>       vesting_balance_id_type;
   typedef object_id< protocol_ids, worker_object_type,             worker_object>                worker_id_type;
   typedef object_id< protocol_ids, balance_object_type,            balance_object>               balance_id_type;

   // implementation types
   class global_property_object;
   class dynamic_global_property_object;
   class index_meta_object;
   class asset_dynamic_data_object;
   class asset_bitasset_data_object;
   class account_balance_object;
   class account_statistics_object;
   class account_debt_object;
   class transaction_object;
   class block_summary_object;
   class account_transaction_history_object;

   typedef object_id< implementation_ids, impl_global_property_object_type,  global_property_object>                    global_property_id_type;
   typedef object_id< implementation_ids, impl_dynamic_global_property_object_type,  dynamic_global_property_object>    dynamic_global_property_id_type;
   typedef object_id< implementation_ids, impl_asset_dynamic_data_type,      asset_dynamic_data_object>                 asset_dynamic_data_id_type;
   typedef object_id< implementation_ids, impl_asset_bitasset_data_type,     asset_bitasset_data_object>                asset_bitasset_data_id_type;
   typedef object_id< implementation_ids, impl_account_balance_object_type,  account_balance_object>                    account_balance_id_type;
   typedef object_id< implementation_ids, impl_account_statistics_object_type,account_statistics_object>                account_statistics_id_type;
   typedef object_id< implementation_ids, impl_account_debt_object_type,     account_debt_object>                       account_debt_id_type;
   typedef object_id< implementation_ids, impl_transaction_object_type,      transaction_object>                        transaction_obj_id_type;
   typedef object_id< implementation_ids, impl_block_summary_object_type,    block_summary_object>                      block_summary_id_type;

   typedef object_id< implementation_ids,
                      impl_account_transaction_history_object_type,
                      account_transaction_history_object>       account_transaction_history_id_type;
   typedef object_id< implementation_ids, impl_witness_schedule_object_type, witness_schedule_object >                  witness_schedule_id_type;

   typedef fc::array<char, GRAPHENE_MAX_ASSET_SYMBOL_LENGTH>    symbol_type;
   typedef fc::ripemd160                                        block_id_type;
   typedef fc::ripemd160                                        checksum_type;
   typedef fc::ripemd160                                        transaction_id_type;
   typedef fc::sha256                                           digest_type;
   typedef fc::ecc::compact_signature                           signature_type;
   typedef safe<int64_t>                                        share_type;
   typedef fc::sha224                                           secret_hash_type;
   typedef uint16_t                                             weight_type;

   /**
    * @brief An ID for some votable object
    *
    * This class stores an ID for a votable object. The ID is comprised of two fields: a type, and an instance. The
    * type field stores which kind of object is being voted on, and the instance stores which specific object of that
    * type is being referenced by this ID.
    *
    * A value of vote_id_type is implicitly convertible to an unsigned 32-bit integer containing only the instance. It
    * may also be implicitly assigned from a uint32_t, which will update the instance. It may not, however, be
    * implicitly constructed from a uint32_t, as in this case, the type would be unknown.
    *
    * On the wire, a vote_id_type is represented as a 32-bit integer with the type in the lower 8 bits and the instance
    * in the upper 24 bits. This means that types may never exceed 8 bits, and instances may never exceed 24 bits.
    *
    * In JSON, a vote_id_type is represented as a string "type:instance", i.e. "1:5" would be type 1 and instance 5.
    *
    * @note In the Graphene protocol, vote_id_type instances are unique across types; that is to say, if an object of
    * type 1 has instance 4, an object of type 0 may not also have instance 4. In other words, the type is not a
    * namespace for instances; it is only an informational field.
    */
   struct vote_id_type
   {
      /// Lower 8 bits are type; upper 24 bits are instance
      uint32_t content;

      enum vote_type
      {
         committee,
         witness,
         worker,
         VOTE_TYPE_COUNT
      };

      /// Default constructor. Sets type and instance to 0
      vote_id_type():content(0){}
      /// Construct this vote_id_type with provided type and instance
      vote_id_type(vote_type type, uint32_t instance = 0)
         : content(instance<<8 | type)
      {}
      /// Construct this vote_id_type from a serial string in the form "type:instance"
      explicit vote_id_type(const std::string& serial)
      {
         auto colon = serial.find(':');
         if( colon != string::npos )
            *this = vote_id_type(vote_type(std::stoul(serial.substr(0, colon))), std::stoul(serial.substr(colon+1)));
      }

      /// Set the type of this vote_id_type
      void set_type(vote_type type)
      {
         content &= 0xffffff00;
         content |= type & 0xff;
      }
      /// Get the type of this vote_id_type
      vote_type type()const
      {
         return vote_type(content & 0xff);
      }

      /// Set the instance of this vote_id_type
      void set_instance(uint32_t instance)
      {
         assert(instance < 0x01000000);
         content &= 0xff;
         content |= instance << 8;
      }
      /// Get the instance of this vote_id_type
      uint32_t instance()const
      {
         return content >> 8;
      }

      vote_id_type& operator =(vote_id_type other)
      {
         content = other.content;
         return *this;
      }
      /// Set the instance of this vote_id_type
      vote_id_type& operator =(uint32_t instance)
      {
         set_instance(instance);
         return *this;
      }
      /// Get the instance of this vote_id_type
      operator uint32_t()const
      {
         return instance();
      }

      /// Convert this vote_id_type to a serial string in the form "type:instance"
      explicit operator std::string()const
      {
         return std::to_string(type()) + ":" + std::to_string(instance());
      }
   };

   struct fee_schedule_type
   {
      /**
       * @brief The fee_set_visitor struct sets all fees to a particular value in one fell swoop
       *
       * Example:
       * @code
       * fee_schedule_type sch;
       * // Set all fees to 50
       * fc::reflector<fee_schedule_type>::visit(fee_schedule_type::fee_set_visitor{sch, 50});
       * @endcode
       */
      struct fee_set_visitor {
         fee_schedule_type& f;
         uint32_t fee;

         template<typename Member, typename Class, Member (Class::*member)>
         void operator()(const char*)const
         {
            f.*member = fee;
         }
      };

      /// The number of bytes to charge a data fee for
      const static int BYTES_PER_DATA_FEE = 1024;

      template <class... Ts>
      uint32_t total_data_fee(Ts... ts)const {
          return data_size(ts...) / BYTES_PER_DATA_FEE * data_fee;
      }

      uint64_t key_create_fee = 270300; ///< the cost to register a public key with the blockchain
      uint64_t account_create_fee = 666666; ///< the cost to register the cheapest non-free account
      uint64_t account_update_fee = 150000; ///< the cost to update an existing account
      uint64_t account_transfer_fee = 300000; ///< the cost to transfer an account to a new owner
      uint64_t account_whitelist_fee = 300000; ///< the fee to whitelist an account
      uint64_t account_len8up_fee = 5*10000000; ///<  about $1
      uint64_t account_len7_fee   = 5*100000000; ///< about $10
      uint64_t account_len6_fee   = 5*UINT64_C(500000000); ///< about $50
      uint64_t account_len5_fee   = 5*UINT64_C(1000000000); ///< about $100
      uint64_t account_len4_fee   = 5*UINT64_C(2000000000); ///< about $200
      uint64_t account_len3_fee   = 5*UINT64_C(3000000000); ///< about $300
      uint64_t account_len2_fee   = 5*UINT64_C(4000000000); ///< about $400
      uint64_t asset_create_fee = 5*UINT64_C(500000000);   ///< about $35 for LTM, the cost to register the cheapest asset
      uint64_t asset_update_fee = 150000; ///< the cost to modify a registered asset
      uint64_t asset_issue_fee = 700000; ///< the cost to print a UIA and send it to an account
      uint64_t asset_reserve_fee = 1500000; ///< the cost to return an asset to the reserve pool
      uint64_t asset_fund_fee_pool_fee = 150000; ///< the cost to add funds to an asset's fee pool
      uint64_t asset_settle_fee = 7000000; ///< the cost to trigger a forced settlement of a market-issued asset
      uint64_t asset_global_settle_fee = 140000000; ///< the cost to trigger a global forced settlement of a market asset
      uint64_t asset_len7up_fee = 5*UINT64_C(500000000);   ///< about $35 for LTM
      uint64_t asset_len6_fee   = 5*5000000000;  ///< about $350 for LTM
      uint64_t asset_len5_fee   = 5*10000000000; ///< about $700 for LTM
      uint64_t asset_len4_fee   = 5*50000000000; ///< about $3500 for LTM
      uint64_t asset_len3_fee   = 5*70000000000; ///< about $5000 for LTM
      uint64_t delegate_create_fee = 680000000; ///< fee for registering as a delegate; used to discourage frivolous delegates
      uint64_t witness_create_fee = 680000000; /// < fee for registering as a witness
      uint64_t witness_withdraw_pay_fee = 1500000; ///< fee for withdrawing witness pay
      uint64_t transfer_fee = 2700000; ///< fee for transferring some asset
      uint64_t limit_order_create_fee = 666666; ///< fee for placing a limit order in the markets
      uint64_t limit_order_cancel_fee = 0; ///< fee for canceling a limit order
      uint64_t call_order_fee = 800000; ///< fee for placing a call order in the markets
      uint64_t publish_feed_fee = 10000; ///< fee for publishing a price feed
      uint64_t data_fee = 13500000; ///< a price per BYTES_PER_DATA_FEE bytes of user data
      uint64_t global_parameters_update_fee = 1350000; ///< the cost to update the global parameters
      uint64_t membership_annual_fee = 270000000; ///< the annual cost of a membership subscription
      uint64_t membership_lifetime_fee = 1350000000; ///< the cost to upgrade to a lifetime member
      uint64_t withdraw_permission_create_fee = 2700000; ///< the cost to create a withdraw permission
      uint64_t withdraw_permission_update_fee = 150000; ///< the cost to update a withdraw permission
      uint64_t withdraw_permission_claim_fee = 700000; ///< the cost to withdraw from a withdraw permission
      uint64_t withdraw_permission_delete_fee = 0; ///< the cost to delete a withdraw permission
      uint64_t vesting_balance_create_fee = 7000000;
      uint64_t vesting_balance_withdraw_fee = 2700000;
      uint64_t worker_create_fee = 680000000; ///< the cost to create a new worker
      uint64_t assert_op_fee = 150000; ///< fee per assert operation
      uint64_t proposal_create_fee = 7000000; ///< fee for creating a proposed transaction
      uint64_t proposal_update_fee = 1500000; ///< fee for adding or removing approval of a proposed transaction
      uint64_t proposal_delete_fee = 0; ///< fee for deleting a proposed transaction
      uint64_t custom_operation_fee = 300000; ///< fee for a custom operation

   protected:
      size_t data_size()const {
          return 0;
      }
      template <class T, class... Ts>
      size_t data_size(T t, Ts... ts)const {
          return fc::raw::pack_size(t) + data_size(ts...);
      }
   };


   struct public_key_type
   {
       struct binary_key
       {
          binary_key():check(0){}
          uint32_t                 check;
          fc::ecc::public_key_data data;
       };

       fc::ecc::public_key_data key_data;

       public_key_type();
       public_key_type( const fc::ecc::public_key_data& data );
       public_key_type( const fc::ecc::public_key& pubkey );
       explicit public_key_type( const std::string& base58str );
       operator fc::ecc::public_key_data() const;
       operator fc::ecc::public_key() const;
       explicit operator std::string() const;
       friend bool operator == ( const public_key_type& p1, const fc::ecc::public_key& p2);
       friend bool operator == ( const public_key_type& p1, const public_key_type& p2);
       friend bool operator != ( const public_key_type& p1, const public_key_type& p2);
   };

   struct chain_parameters
   {
      fee_schedule_type       current_fees; ///< current schedule of fees
      uint8_t                 block_interval                      = GRAPHENE_DEFAULT_BLOCK_INTERVAL; ///< interval in seconds between blocks
      uint32_t                maintenance_interval                = GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL; ///< interval in sections between blockchain maintenance events
      uint32_t                maximum_transaction_size            = GRAPHENE_DEFAULT_MAX_TRANSACTION_SIZE; ///< maximum allowable size in bytes for a transaction
      uint32_t                maximum_block_size                  = GRAPHENE_DEFAULT_MAX_BLOCK_SIZE; ///< maximum allowable size in bytes for a block
      uint32_t                maximum_undo_history                = GRAPHENE_DEFAULT_MAX_UNDO_HISTORY; ///< maximum number of undo states to keep in RAM
      uint32_t                maximum_time_until_expiration       = GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION; ///< maximum lifetime in seconds for transactions to be valid, before expiring
      uint32_t                maximum_proposal_lifetime           = GRAPHENE_DEFAULT_MAX_PROPOSAL_LIFETIME_SEC; ///< maximum lifetime in seconds for proposed transactions to be kept, before expiring
      uint32_t                genesis_proposal_review_period      = GRAPHENE_DEFAULT_GENESIS_PROPOSAL_REVIEW_PERIOD_SEC; ///< minimum time in seconds that a proposed transaction requiring genesis authority may not be signed, prior to expiration
      uint8_t                 maximum_asset_whitelist_authorities = GRAPHENE_DEFAULT_MAX_ASSET_WHITELIST_AUTHORITIES; ///< maximum number of accounts which an asset may list as authorities for its whitelist OR blacklist
      uint8_t                 maximum_asset_feed_publishers       = GRAPHENE_DEFAULT_MAX_ASSET_FEED_PUBLISHERS; ///< the maximum number of feed publishers for a given asset
      uint16_t                maximum_witness_count               = GRAPHENE_DEFAULT_MAX_WITNESSES; ///< maximum number of active witnesses
      uint16_t                maximum_committee_count             = GRAPHENE_DEFAULT_MAX_COMMITTEE; ///< maximum number of active delegates
      uint16_t                maximum_authority_membership        = GRAPHENE_DEFAULT_MAX_AUTHORITY_MEMBERSHIP; ///< largest number of keys/accounts an authority can have
      uint16_t                reserve_percent_of_fee                 = GRAPHENE_DEFAULT_BURN_PERCENT_OF_FEE; ///< the percentage of the network's allocation of a fee that is taken out of circulation
      uint16_t                network_percent_of_fee              = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE; ///< percent of transaction fees paid to network
      uint16_t                lifetime_referrer_percent_of_fee    = GRAPHENE_DEFAULT_LIFETIME_REFERRER_PERCENT_OF_FEE; ///< percent of transaction fees paid to network
      uint32_t                cashback_vesting_period_seconds     = GRAPHENE_DEFAULT_CASHBACK_VESTING_PERIOD_SEC; ///< time after cashback rewards are accrued before they become liquid
      share_type              cashback_vesting_threshold          = GRAPHENE_DEFAULT_CASHBACK_VESTING_THRESHOLD; ///< the maximum cashback that can be received without vesting
      uint16_t                max_bulk_discount_percent_of_fee    = GRAPHENE_DEFAULT_MAX_BULK_DISCOUNT_PERCENT; ///< the maximum percentage discount for bulk discounts
      share_type              bulk_discount_threshold_min         = GRAPHENE_DEFAULT_BULK_DISCOUNT_THRESHOLD_MIN; ///< the minimum amount of fees paid to qualify for bulk discounts
      share_type              bulk_discount_threshold_max         = GRAPHENE_DEFAULT_BULK_DISCOUNT_THRESHOLD_MAX; ///< the amount of fees paid to qualify for the max bulk discount percent
      bool                    count_non_member_votes              = true; ///< set to false to restrict voting privlegages to member accounts
      bool                    allow_non_member_whitelists         = false; ///< true if non-member accounts may set whitelists and blacklists; false otherwise
      share_type              witness_pay_per_block               = GRAPHENE_DEFAULT_WITNESS_PAY_PER_BLOCK; ///< CORE to be allocated to witnesses (per block)
      share_type              worker_budget_per_day               = GRAPHENE_DEFAULT_WORKER_BUDGET_PER_DAY; ///< CORE to be allocated to workers (per day)
      uint16_t                max_predicate_opcode                = GRAPHENE_DEFAULT_MAX_ASSERT_OPCODE; ///< predicate_opcode must be less than this number
      share_type              fee_liquidation_threshold           = GRAPHENE_DEFAULT_FEE_LIQUIDATION_THRESHOLD; ///< value in CORE at which accumulated fees in blockchain-issued market assets should be liquidated
      uint16_t                accounts_per_fee_scale              = GRAPHENE_DEFAULT_ACCOUNTS_PER_FEE_SCALE; ///< number of accounts between fee scalings
      uint8_t                 account_fee_scale_bitshifts         = GRAPHENE_DEFAULT_ACCOUNT_FEE_SCALE_BITSHIFTS; ///< number of times to left bitshift account registration fee at each scaling

      void validate()const
      {
         FC_ASSERT( reserve_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( network_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( max_bulk_discount_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( lifetime_referrer_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( network_percent_of_fee + lifetime_referrer_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( bulk_discount_threshold_min <= bulk_discount_threshold_max );
         FC_ASSERT( bulk_discount_threshold_min > 0 );

         FC_ASSERT( block_interval <= GRAPHENE_MAX_BLOCK_INTERVAL );
         FC_ASSERT( block_interval > 0 );
         FC_ASSERT( maintenance_interval > block_interval,
                    "Maintenance interval must be longer than block interval" );
         FC_ASSERT( maintenance_interval % block_interval == 0,
                    "Maintenance interval must be a multiple of block interval" );
         FC_ASSERT( maximum_transaction_size >= GRAPHENE_MIN_TRANSACTION_SIZE_LIMIT,
                    "Transaction size limit is too low" );
         FC_ASSERT( maximum_block_size >= GRAPHENE_MIN_BLOCK_SIZE_LIMIT,
                    "Block size limit is too low" );
         FC_ASSERT( maximum_time_until_expiration > block_interval,
                    "Maximum transaction expiration time must be greater than a block interval" );
         FC_ASSERT( maximum_proposal_lifetime - genesis_proposal_review_period > block_interval,
                    "Genesis proposal review period must be less than the maximum proposal lifetime" );
      }
   };

} }  // graphene::chain

namespace fc
{
    void to_variant( const graphene::chain::public_key_type& var,  fc::variant& vo );
    void from_variant( const fc::variant& var,  graphene::chain::public_key_type& vo );
    void to_variant( const graphene::chain::vote_id_type& var, fc::variant& vo );
    void from_variant( const fc::variant& var, graphene::chain::vote_id_type& vo );
}

FC_REFLECT_TYPENAME( graphene::chain::vote_id_type::vote_type )
FC_REFLECT_ENUM( graphene::chain::vote_id_type::vote_type, (witness)(committee)(worker)(VOTE_TYPE_COUNT) )
FC_REFLECT( graphene::chain::vote_id_type, (content) )

FC_REFLECT( graphene::chain::public_key_type, (key_data) )
FC_REFLECT( graphene::chain::public_key_type::binary_key, (data)(check) )

FC_REFLECT_ENUM( graphene::chain::object_type,
                 (null_object_type)
                 (base_object_type)
                 (account_object_type)
                 (force_settlement_object_type)
                 (asset_object_type)
                 (delegate_object_type)
                 (witness_object_type)
                 (limit_order_object_type)
                 (call_order_object_type)
                 (custom_object_type)
                 (proposal_object_type)
                 (operation_history_object_type)
                 (withdraw_permission_object_type)
                 (vesting_balance_object_type)
                 (worker_object_type)
                 (balance_object_type)
                 (OBJECT_TYPE_COUNT)
               )
FC_REFLECT_ENUM( graphene::chain::impl_object_type,
                 (impl_global_property_object_type)
                 (impl_dynamic_global_property_object_type)
                 (impl_index_meta_object_type)
                 (impl_asset_dynamic_data_type)
                 (impl_asset_bitasset_data_type)
                 (impl_delegate_feeds_object_type)
                 (impl_account_balance_object_type)
                 (impl_account_statistics_object_type)
                 (impl_account_debt_object_type)
                 (impl_transaction_object_type)
                 (impl_block_summary_object_type)
                 (impl_account_transaction_history_object_type)
                 (impl_witness_schedule_object_type)
               )

FC_REFLECT_ENUM( graphene::chain::meta_info_object_type, (meta_account_object_type)(meta_asset_object_type) )


FC_REFLECT( graphene::chain::fee_schedule_type,
            (key_create_fee)
            (account_create_fee)
            (account_update_fee)
            (account_transfer_fee)
            (account_len8up_fee)
            (account_len7_fee)
            (account_len6_fee)
            (account_len5_fee)
            (account_len4_fee)
            (account_len3_fee)
            (account_len2_fee)
            (asset_len3_fee)
            (asset_len4_fee)
            (asset_len5_fee)
            (asset_len6_fee)
            (asset_len7up_fee)
            (account_whitelist_fee)
            (delegate_create_fee)
            (witness_create_fee)
            (witness_withdraw_pay_fee)
            (transfer_fee)
            (limit_order_create_fee)
            (limit_order_cancel_fee)
            (call_order_fee)
            (publish_feed_fee)
            (asset_create_fee)
            (asset_update_fee)
            (asset_issue_fee)
            (asset_reserve_fee)
            (asset_fund_fee_pool_fee)
            (asset_settle_fee)
            (data_fee)
            (global_parameters_update_fee)
            (membership_annual_fee)
            (membership_lifetime_fee)
            (withdraw_permission_create_fee)
            (withdraw_permission_update_fee)
            (withdraw_permission_claim_fee)
            (withdraw_permission_delete_fee)
            (vesting_balance_create_fee)
            (vesting_balance_withdraw_fee)
            (asset_global_settle_fee)
            (worker_create_fee)
            (assert_op_fee)
            (proposal_create_fee)
            (proposal_update_fee)
            (proposal_delete_fee)
          )

FC_REFLECT( graphene::chain::chain_parameters,
            (current_fees)
            (block_interval)
            (maintenance_interval)
            (maximum_transaction_size)
            (maximum_block_size)
            (maximum_undo_history)
            (maximum_time_until_expiration)
            (maximum_proposal_lifetime)
            (maximum_asset_whitelist_authorities)
            (maximum_asset_feed_publishers)
            (maximum_authority_membership)
            (reserve_percent_of_fee)
            (network_percent_of_fee)
            (lifetime_referrer_percent_of_fee)
            (max_bulk_discount_percent_of_fee)
            (cashback_vesting_period_seconds)
            (cashback_vesting_threshold)
            (bulk_discount_threshold_min)
            (bulk_discount_threshold_max)
            (count_non_member_votes)
            (allow_non_member_whitelists)
            (witness_pay_per_block)
            (worker_budget_per_day)
            (max_predicate_opcode)
            (fee_liquidation_threshold)
            (accounts_per_fee_scale)
            (account_fee_scale_bitshifts)
          )

FC_REFLECT_TYPENAME( graphene::chain::share_type )

FC_REFLECT_TYPENAME( graphene::chain::account_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_id_type )
FC_REFLECT_TYPENAME( graphene::chain::force_settlement_id_type )
FC_REFLECT_TYPENAME( graphene::chain::delegate_id_type )
FC_REFLECT_TYPENAME( graphene::chain::witness_id_type )
FC_REFLECT_TYPENAME( graphene::chain::limit_order_id_type )
FC_REFLECT_TYPENAME( graphene::chain::call_order_id_type )
FC_REFLECT_TYPENAME( graphene::chain::custom_id_type )
FC_REFLECT_TYPENAME( graphene::chain::proposal_id_type )
FC_REFLECT_TYPENAME( graphene::chain::operation_history_id_type )
FC_REFLECT_TYPENAME( graphene::chain::withdraw_permission_id_type )
FC_REFLECT_TYPENAME( graphene::chain::vesting_balance_id_type )
FC_REFLECT_TYPENAME( graphene::chain::worker_id_type )
FC_REFLECT_TYPENAME( graphene::chain::global_property_id_type )
FC_REFLECT_TYPENAME( graphene::chain::dynamic_global_property_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_dynamic_data_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_bitasset_data_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_balance_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_statistics_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_debt_id_type )
FC_REFLECT_TYPENAME( graphene::chain::transaction_obj_id_type )
FC_REFLECT_TYPENAME( graphene::chain::block_summary_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_transaction_history_id_type )
FC_REFLECT_TYPENAME( graphene::chain::witness_schedule_id_type )

FC_REFLECT_ENUM( graphene::chain::asset_issuer_permission_flags, (charge_market_fee)(white_list)(transfer_restricted)(override_authority)(disable_force_settle)(global_settle) )
