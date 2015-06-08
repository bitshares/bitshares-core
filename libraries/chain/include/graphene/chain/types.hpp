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
#include <fc/io/raw_fwd.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>
#include <memory>
#include <vector>
#include <deque>
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
    *  There are many types of fees charged by the network
    *  for different operations. These fees are published by
    *  the delegates and can change over time.
    */
   enum fee_type
   {
      key_create_fee_type, ///< the cost to register a public key with the blockchain
      account_create_fee_type, ///< the cost to register the cheapest non-free account
      account_len8_fee_type,
      account_len7_fee_type,
      account_len6_fee_type,
      account_len5_fee_type,
      account_len4_fee_type,
      account_len3_fee_type,
      account_premium_fee_type,  ///< accounts on the reserved list of top 100K domains
      account_whitelist_fee_type, ///< the fee to whitelist an account
      delegate_create_fee_type, ///< fixed fee for registering as a delegate, used to discourage frivioulous delegates
      witness_withdraw_pay_fee_type, ///< fee for withdrawing witness pay
      transfer_fee_type, ///< fee for transferring some asset
      limit_order_fee_type, ///< fee for placing a limit order in the markets
      short_order_fee_type, ///< fee for placing a short order in the markets
      publish_feed_fee_type, ///< fee for publishing a price feed
      asset_create_fee_type, ///< the cost to register the cheapest asset
      asset_update_fee_type, ///< the cost to modify a registered asset
      asset_issue_fee_type, ///< the cost to modify a registered asset
      asset_fund_fee_pool_fee_type, ///< the cost to add funds to an asset's fee pool
      asset_settle_fee_type, ///< the cost to trigger a forced settlement of a market-issued asset
      market_fee_type, ///< a percentage charged on market orders
      transaction_fee_type, ///< a base price for every transaction
      data_fee_type, ///< a price per 1024 bytes of user data
      signature_fee_type, ///< a surcharge on transactions with more than 2 signatures.
      global_parameters_update_fee_type, ///< the cost to update the global parameters
      prime_upgrade_fee_type, ///< the cost to upgrade an account to prime
      withdraw_permission_update_fee_type, ///< the cost to create/update a withdraw permission
      create_bond_offer_fee_type,
      cancel_bond_offer_fee_type,
      accept_bond_offer_fee_type,
      claim_bond_collateral_fee_type,
      file_storage_fee_per_day_type, ///< the cost of leasing a file with 2^16 bytes for 1 day
      vesting_balance_create_fee_type,
      vesting_balance_withdraw_fee_type,
      global_settle_fee_type,
      worker_create_fee_type, ///< the cost to create a new worker
      worker_delete_fee_type, ///< the cost to delete a worker
      FEE_TYPE_COUNT ///< Sentry value which contains the number of different fee types
   };

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
      key_object_type,
      account_object_type,
      asset_object_type,
      force_settlement_object_type,
      delegate_object_type,
      witness_object_type,
      limit_order_object_type,
      short_order_object_type,
      call_order_object_type,
      custom_object_type,
      proposal_object_type,
      operation_history_object_type,
      withdraw_permission_object_type,
      bond_offer_object_type,
      bond_object_type,
      file_object_type,
      vesting_balance_object_type,
      worker_object_type,
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
   class key_object;
   class limit_order_object;
   class short_order_object;
   class call_order_object;
   class custom_object;
   class proposal_object;
   class operation_history_object;
   class withdraw_permission_object;
   class bond_object;
   class bond_offer_object;
   class file_object;
   class vesting_balance_object;
   class witness_schedule_object;
   class worker_object;

   typedef object_id< protocol_ids, key_object_type,                key_object>                   key_id_type;
   typedef object_id< protocol_ids, account_object_type,            account_object>               account_id_type;
   typedef object_id< protocol_ids, asset_object_type,              asset_object>                 asset_id_type;
   typedef object_id< protocol_ids, force_settlement_object_type,   force_settlement_object>      force_settlement_id_type;
   typedef object_id< protocol_ids, delegate_object_type,           delegate_object>              delegate_id_type;
   typedef object_id< protocol_ids, witness_object_type,            witness_object>               witness_id_type;
   typedef object_id< protocol_ids, limit_order_object_type,        limit_order_object>           limit_order_id_type;
   typedef object_id< protocol_ids, short_order_object_type,        short_order_object>           short_order_id_type;
   typedef object_id< protocol_ids, call_order_object_type,         call_order_object>            call_order_id_type;
   typedef object_id< protocol_ids, custom_object_type,             custom_object>                custom_id_type;
   typedef object_id< protocol_ids, proposal_object_type,           proposal_object>              proposal_id_type;
   typedef object_id< protocol_ids, operation_history_object_type,  operation_history_object>     operation_history_id_type;
   typedef object_id< protocol_ids, withdraw_permission_object_type,withdraw_permission_object>   withdraw_permission_id_type;
   typedef object_id< protocol_ids, bond_offer_object_type,         bond_offer_object>            bond_offer_id_type;
   typedef object_id< protocol_ids, bond_object_type,               bond_object>                  bond_id_type;
   typedef object_id< protocol_ids, file_object_type,               file_object>                  file_id_type;
   typedef object_id< protocol_ids, vesting_balance_object_type,    vesting_balance_object>       vesting_balance_id_type;
   typedef object_id< protocol_ids, worker_object_type,             worker_object>                worker_id_type;

   typedef object_id< relative_protocol_ids, key_object_type, key_object>           relative_key_id_type;
   typedef object_id< relative_protocol_ids, account_object_type, account_object>   relative_account_id_type;

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
   typedef object_id< implementation_ids, impl_asset_dynamic_data_type,      asset_dynamic_data_object>                 dynamic_asset_data_id_type;
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

   typedef fc::array<char,GRAPHENE_MAX_SYMBOL_NAME_LENGTH>   symbol_type;
   typedef fc::ripemd160                                block_id_type;
   typedef fc::ripemd160                                checksum_type;
   typedef fc::ripemd160                                transaction_id_type;
   typedef fc::sha256                                   digest_type;
   typedef fc::ecc::compact_signature                   signature_type;
   typedef safe<int64_t>                                share_type;
   typedef fc::sha224                                   secret_hash_type;
   typedef uint16_t                                     weight_type;

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
       fee_schedule_type()
       {
          memset( (char*)this, 0, sizeof(*this) );
       }
       void             set( uint32_t f, share_type v ){ FC_ASSERT( f < FEE_TYPE_COUNT && v.value <= uint32_t(-1) ); *(&key_create_fee + f) = v.value; }
       const share_type at( uint32_t f )const { FC_ASSERT( f < FEE_TYPE_COUNT ); return *(&key_create_fee + f); }
       size_t           size()const{ return FEE_TYPE_COUNT; }


       uint32_t key_create_fee; ///< the cost to register a public key with the blockchain
       uint32_t account_create_fee; ///< the cost to register the cheapest non-free account
       uint32_t account_len8_fee;
       uint32_t account_len7_fee;
       uint32_t account_len6_fee;
       uint32_t account_len5_fee;
       uint32_t account_len4_fee;
       uint32_t account_len3_fee;
       uint32_t account_premium_fee;  ///< accounts on the reserved list of top 100K domains
       uint32_t account_whitelist_fee; ///< the fee to whitelist an account
       uint32_t delegate_create_fee; ///< fixed fee for registering as a delegate; used to discourage frivioulous delegates
       uint32_t witness_withdraw_pay_fee; ///< fee for withdrawing witness pay
       uint32_t transfer_fee; ///< fee for transferring some asset
       uint32_t limit_order_fee; ///< fee for placing a limit order in the markets
       uint32_t short_order_fee; ///< fee for placing a short order in the markets
       uint32_t publish_feed_fee; ///< fee for publishing a price feed
       uint32_t asset_create_fee; ///< the cost to register the cheapest asset
       uint32_t asset_update_fee; ///< the cost to modify a registered asset
       uint32_t asset_issue_fee; ///< the cost to modify a registered asset
       uint32_t asset_fund_fee_pool_fee; ///< the cost to add funds to an asset's fee pool
       uint32_t asset_settle_fee; ///< the cost to trigger a forced settlement of a market-issued asset
       uint32_t market_fee; ///< a percentage charged on market orders
       uint32_t transaction_fee; ///< a base price for every transaction
       uint32_t data_fee; ///< a price per 1024 bytes of user data
       uint32_t signature_fee; ///< a surcharge on transactions with more than 2 signatures.
       uint32_t global_parameters_update_fee; ///< the cost to update the global parameters
       uint32_t prime_upgrade_fee; ///< the cost to upgrade an account to prime
       uint32_t withdraw_permission_update_fee; ///< the cost to create/update a withdraw permission
       uint32_t create_bond_offer_fee;
       uint32_t cancel_bond_offer_fee;
       uint32_t accept_bond_offer_fee;
       uint32_t claim_bond_collateral_fee;
       uint32_t file_storage_fee_per_day; ///< the cost of leasing a file with 2^16 bytes for 1 day
       uint32_t vesting_balance_create_fee;
       uint32_t vesting_balance_withdraw_fee;
       uint32_t global_settle_fee;
       uint32_t worker_create_fee; ///< the cost to create a new worker
       uint32_t worker_delete_fee; ///< the cost to delete a worker
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
      fee_schedule_type       current_fees; ///< current schedule of fees, indexed by @ref fee_type
      uint32_t                witness_pay_percent_of_accumulated  = GRAPHENE_DEFAULT_WITNESS_PAY_PERCENT_OF_ACCUMULATED; ///< percentage of accumulated fees in core asset to pay to witnesses for block production
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
      uint16_t                burn_percent_of_fee                 = GRAPHENE_DEFAULT_BURN_PERCENT_OF_FEE; ///< the percentage of every fee that is taken out of circulation
      uint16_t                witness_percent_of_fee              = GRAPHENE_DEFAULT_WITNESS_PERCENT; ///< percent of revenue paid to witnesses
      uint32_t                cashback_vesting_period_seconds     = GRAPHENE_DEFAULT_CASHBACK_VESTING_PERIOD_SEC; ///< time after cashback rewards are accrued before they become liquid
      uint16_t                max_bulk_discount_percent_of_fee    = GRAPHENE_DEFAULT_MAX_BULK_DISCOUNT_PERCENT; ///< the maximum percentage discount for bulk discounts
      share_type              bulk_discount_threshold_min         = GRAPHENE_DEFAULT_BULK_DISCOUNT_THRESHOLD_MIN; ///< the minimum amount of fees paid to qualify for bulk discounts
      share_type              bulk_discount_threshold_max         = GRAPHENE_DEFAULT_BULK_DISCOUNT_THRESHOLD_MAX; ///< the amount of fees paid to qualify for the max bulk discount percent
      bool                    count_non_prime_votes               = true; ///< set to false to restrict voting privlegages to prime accounts
      bool                    allow_non_prime_whitelists          = false; ///< true if non-prime accounts may set whitelists and blacklists; false otherwise
      share_type              witness_pay_per_block               = GRAPHENE_DEFAULT_WITNESS_PAY_PER_BLOCK; ///< CORE to be allocated to witnesses (per block)
      share_type              worker_budget_per_day               = GRAPHENE_DEFAULT_WORKER_BUDGET_PER_DAY; ///< CORE to be allocated to workers (per day)

      void validate()const
      {
         FC_ASSERT( witness_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( burn_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( max_bulk_discount_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( burn_percent_of_fee + witness_percent_of_fee <= GRAPHENE_100_PERCENT );
         FC_ASSERT( bulk_discount_threshold_min <= bulk_discount_threshold_max );
         FC_ASSERT( bulk_discount_threshold_min > 0 );

         FC_ASSERT( witness_pay_percent_of_accumulated < GRAPHENE_WITNESS_PAY_PERCENT_PRECISION );
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
         for( uint32_t i = 0; i < FEE_TYPE_COUNT; ++i ) { FC_ASSERT( current_fees.at(i) >= 0 ); }
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
                 (key_object_type)
                 (account_object_type)
                 (force_settlement_object_type)
                 (asset_object_type)
                 (delegate_object_type)
                 (witness_object_type)
                 (limit_order_object_type)
                 (short_order_object_type)
                 (call_order_object_type)
                 (custom_object_type)
                 (proposal_object_type)
                 (operation_history_object_type)
                 (withdraw_permission_object_type)
                 (bond_offer_object_type)
                 (bond_object_type)
                 (file_object_type)
                 (vesting_balance_object_type)
                 (worker_object_type)
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
                 (account_len8_fee)
                 (account_len7_fee)
                 (account_len6_fee)
                 (account_len5_fee)
                 (account_len4_fee)
                 (account_len3_fee)
                 (account_premium_fee)
                 (account_whitelist_fee)
                 (delegate_create_fee)
                 (witness_withdraw_pay_fee)
                 (transfer_fee)
                 (limit_order_fee)
                 (short_order_fee)
                 (publish_feed_fee)
                 (asset_create_fee)
                 (asset_update_fee)
                 (asset_issue_fee)
                 (asset_fund_fee_pool_fee)
                 (asset_settle_fee)
                 (market_fee)
                 (transaction_fee)
                 (data_fee)
                 (signature_fee)
                 (global_parameters_update_fee)
                 (prime_upgrade_fee)
                 (withdraw_permission_update_fee)
                 (create_bond_offer_fee)
                 (cancel_bond_offer_fee)
                 (accept_bond_offer_fee)
                 (claim_bond_collateral_fee)
                 (file_storage_fee_per_day)
                 (vesting_balance_create_fee)
                 (vesting_balance_withdraw_fee)
                 (global_settle_fee)
                 (worker_create_fee)
                 (worker_delete_fee)
               )


FC_REFLECT_ENUM( graphene::chain::fee_type,
                 (key_create_fee_type)
                 (account_create_fee_type)
                 (account_len8_fee_type)
                 (account_len7_fee_type)
                 (account_len6_fee_type)
                 (account_len5_fee_type)
                 (account_len4_fee_type)
                 (account_len3_fee_type)
                 (account_premium_fee_type)
                 (account_whitelist_fee_type)
                 (delegate_create_fee_type)
                 (witness_withdraw_pay_fee_type)
                 (transfer_fee_type)
                 (limit_order_fee_type)
                 (short_order_fee_type)
                 (publish_feed_fee_type)
                 (asset_create_fee_type)
                 (asset_update_fee_type)
                 (asset_issue_fee_type)
                 (asset_fund_fee_pool_fee_type)
                 (asset_settle_fee_type)
                 (market_fee_type)
                 (transaction_fee_type)
                 (data_fee_type)
                 (signature_fee_type)
                 (global_parameters_update_fee_type)
                 (prime_upgrade_fee_type)
                 (withdraw_permission_update_fee_type)
                 (create_bond_offer_fee_type)
                 (cancel_bond_offer_fee_type)
                 (accept_bond_offer_fee_type)
                 (claim_bond_collateral_fee_type)
                 (file_storage_fee_per_day_type)
                 (vesting_balance_create_fee_type)
                 (vesting_balance_withdraw_fee_type)
                 (global_settle_fee_type)
                 (worker_create_fee_type)
                 (worker_delete_fee_type)
                 (FEE_TYPE_COUNT)
               )

FC_REFLECT( graphene::chain::chain_parameters,
            (current_fees)
            (witness_pay_percent_of_accumulated)
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
            (burn_percent_of_fee)
            (witness_percent_of_fee)
            (max_bulk_discount_percent_of_fee)
            (cashback_vesting_period_seconds)
            (bulk_discount_threshold_min)
            (bulk_discount_threshold_max)
            (count_non_prime_votes)
            (allow_non_prime_whitelists)
            (witness_pay_per_block)
            (worker_budget_per_day)
          )

FC_REFLECT_TYPENAME( graphene::chain::key_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_id_type )
FC_REFLECT_TYPENAME( graphene::chain::force_settlement_id_type )
FC_REFLECT_TYPENAME( graphene::chain::delegate_id_type )
FC_REFLECT_TYPENAME( graphene::chain::witness_id_type )
FC_REFLECT_TYPENAME( graphene::chain::limit_order_id_type )
FC_REFLECT_TYPENAME( graphene::chain::short_order_id_type )
FC_REFLECT_TYPENAME( graphene::chain::call_order_id_type )
FC_REFLECT_TYPENAME( graphene::chain::custom_id_type )
FC_REFLECT_TYPENAME( graphene::chain::proposal_id_type )
FC_REFLECT_TYPENAME( graphene::chain::operation_history_id_type )
FC_REFLECT_TYPENAME( graphene::chain::withdraw_permission_id_type )
FC_REFLECT_TYPENAME( graphene::chain::bond_offer_id_type )
FC_REFLECT_TYPENAME( graphene::chain::bond_id_type )
FC_REFLECT_TYPENAME( graphene::chain::file_id_type )
FC_REFLECT_TYPENAME( graphene::chain::vesting_balance_id_type )
FC_REFLECT_TYPENAME( graphene::chain::worker_id_type )
FC_REFLECT_TYPENAME( graphene::chain::relative_key_id_type )
FC_REFLECT_TYPENAME( graphene::chain::relative_account_id_type )
FC_REFLECT_TYPENAME( graphene::chain::global_property_id_type )
FC_REFLECT_TYPENAME( graphene::chain::dynamic_global_property_id_type )
FC_REFLECT_TYPENAME( graphene::chain::dynamic_asset_data_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_bitasset_data_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_balance_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_statistics_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_debt_id_type )
FC_REFLECT_TYPENAME( graphene::chain::transaction_obj_id_type )
FC_REFLECT_TYPENAME( graphene::chain::block_summary_id_type )
FC_REFLECT_TYPENAME( graphene::chain::account_transaction_history_id_type )
FC_REFLECT_TYPENAME( graphene::chain::witness_schedule_id_type )

FC_REFLECT_ENUM( graphene::chain::asset_issuer_permission_flags, (charge_market_fee)(white_list)(transfer_restricted)(override_authority)(disable_force_settle)(global_settle) )
