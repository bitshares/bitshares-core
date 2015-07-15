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
#include <fc/uint128.hpp>
#include <fc/static_variant.hpp>
#include <fc/smart_ref_fwd.hpp>

#include <memory>
#include <vector>
#include <deque>
#include <cstdint>
#include <graphene/chain/protocol/address.hpp>
#include <graphene/db/object_id.hpp>
#include <graphene/chain/protocol/config.hpp>

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

   using                               fc::smart_ref;
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
   struct void_t{};

   typedef fc::ecc::private_key        private_key_type;

   enum asset_issuer_permission_flags
   {
      charge_market_fee    = 0x01, /**< an issuer-specified percentage of all market trades in this asset is paid to the issuer */
      white_list           = 0x02, /**< accounts must be whitelisted in order to hold this asset */
      override_authority   = 0x04, /**< issuer may transfer asset back to himself */
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
      committee_member_object_type,
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
      impl_committee_member_feeds_object_type,
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

   struct chain_parameters;


   //typedef fc::unsigned_int            object_id_type;
   //typedef uint64_t                    object_id_type;
   class account_object;
   class committee_member_object;
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
   typedef object_id< protocol_ids, committee_member_object_type,           committee_member_object>              committee_member_id_type;
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
   typedef fc::ripemd160                                        secret_hash_type;
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
       // TODO: This is temporary for testing
       bool is_valid_v1( const std::string& base58str );
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
FC_REFLECT_TYPENAME( fc::flat_set<graphene::chain::vote_id_type> )

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
                 (committee_member_object_type)
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
                 (impl_committee_member_feeds_object_type)
                 (impl_account_balance_object_type)
                 (impl_account_statistics_object_type)
                 (impl_account_debt_object_type)
                 (impl_transaction_object_type)
                 (impl_block_summary_object_type)
                 (impl_account_transaction_history_object_type)
                 (impl_witness_schedule_object_type)
               )

FC_REFLECT_ENUM( graphene::chain::meta_info_object_type, (meta_account_object_type)(meta_asset_object_type) )

FC_REFLECT_TYPENAME( graphene::chain::share_type )

FC_REFLECT_TYPENAME( graphene::chain::account_id_type )
FC_REFLECT_TYPENAME( graphene::chain::asset_id_type )
FC_REFLECT_TYPENAME( graphene::chain::force_settlement_id_type )
FC_REFLECT_TYPENAME( graphene::chain::committee_member_id_type )
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
FC_REFLECT( graphene::chain::void_t, )

FC_REFLECT_ENUM( graphene::chain::asset_issuer_permission_flags, (charge_market_fee)(white_list)(transfer_restricted)(override_authority)(disable_force_settle)(global_settle) )
