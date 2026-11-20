/*
 * Copyright (c) 2026 BitShares contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>

namespace graphene { namespace protocol {

   /// Upper bound on an oracle's producer set. Bounds the cost of aggregating, which runs
   /// whenever a value is published, and bounds the object's serialized size.
   constexpr uint16_t GRAPHENE_ORACLE_MAX_PRODUCERS = 32;

   /// Upper bound on retained history. State per oracle is fixed at creation rather than
   /// growing with age or publication rate.
   constexpr uint16_t GRAPHENE_ORACLE_MAX_HISTORY = 64;

   /// Upper bound on how many market-issued assets may bind to one oracle. Publishing
   /// refreshes every bound asset's feed, so this bounds the work a single publish can cause.
   constexpr uint16_t GRAPHENE_ORACLE_MAX_SUBSCRIBERS = 64;

   constexpr size_t GRAPHENE_ORACLE_MIN_NAME_LENGTH = 3;
   constexpr size_t GRAPHENE_ORACLE_MAX_NAME_LENGTH = 32;
   constexpr size_t GRAPHENE_ORACLE_MAX_DESCRIPTION_LENGTH = 1000;

   /// A producer's weight. Weight is set by the owner, never earned; see ORACLE-DESIGN.md.
   constexpr uint16_t GRAPHENE_ORACLE_MAX_PRODUCER_WEIGHT = 1000;

   /**
    * How an oracle turns its producers' submissions into one value.
    *
    * Both methods *select* an observed value rather than computing a new one. That is a
    * consensus requirement, not a stylistic choice: selection cannot round, cannot overflow,
    * and cannot differ between nodes.
    */
   enum class oracle_aggregation_method : uint8_t
   {
      /// Weighted median of the submissions still within value_lifetime_sec. With every
      /// weight equal to 1 this is exactly the behaviour of the legacy price feed median.
      median_of_latest = 0,
      /// Median of the recorded aggregates inside window_sec. Deliberately a windowed median
      /// and not a time-weighted average -- see ORACLE-DESIGN.md for why an exact selection
      /// is preferred over an accumulated mean.
      median_over_window = 1
   };

   /// Shared by create and update. Every field is policy the owner controls.
   struct oracle_options
   {
      /// Producers permitted to publish, and their weights. Empty means nobody can publish,
      /// which is legal: it parks an oracle without deleting it.
      flat_map<account_id_type, uint16_t> producers;

      /// Fewer live submissions than this and the oracle reports no value at all, rather than
      /// reporting a value derived from too few sources.
      uint16_t minimum_producers = 1;

      /// A submission older than this stops counting toward the aggregate.
      uint32_t value_lifetime_sec = 86400;

      /**
       * Aggregation method, held as the underlying integer of @ref oracle_aggregation_method.
       *
       * Protocol data, so not the enum type itself: fc reflects an enum as a signed 64-bit
       * value, which both wastes space in every signed transaction and moves the rejection of
       * an unknown method into deserialisation instead of validate(). The existing protocol
       * does the same with black_swan_response_method. Read it through get_aggregation().
       */
      uint8_t aggregation = static_cast<uint8_t>( oracle_aggregation_method::median_of_latest );

      /// Window for median_over_window. Ignored by median_of_latest.
      uint32_t window_sec = 3600;

      // There is deliberately no outlier filter. A weighted median already confines a minority
      // to the range of the honest values. A filter anchored on the round's median, which a
      // minority can shift, let two colluders out of five get honest producers excluded and
      // push the value outside that range. See ORACLE-DESIGN.md and the test
      // a_colluding_minority_cannot_push_the_value_outside_the_honest_range.

      extensions_type extensions;

      /// @return the aggregation method; only valid after validate() has accepted the value
      oracle_aggregation_method get_aggregation()const
      { return static_cast<oracle_aggregation_method>( aggregation ); }

      void validate()const;
   };

   /**
    * @brief Creates a named price series that consumers can reference by id.
    * @ingroup operations
    */
   struct oracle_create_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 500 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset           fee;
      account_id_type owner;         ///< Creator and administrator; pays the fee

      /// Unique, e.g. "BTC.USD". Uppercase letters, digits, '.' and '-'.
      string          name;
      string          description;

      /// What a published price means. Every published value must use exactly these, in this
      /// orientation, so a consumer never has to guess whether a number is inverted.
      asset_id_type   base_asset;
      asset_id_type   quote_asset;

      oracle_options  options;

      extensions_type extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
      share_type      calculate_fee( const fee_params_t& k )const;
   };

   /**
    * @brief Changes an oracle's policy.
    * @ingroup operations
    *
    * Deliberately cannot change base_asset, quote_asset or name: consumers reference an
    * oracle by id and would have no way to notice that what it measures had changed
    * underneath them.
    */
   struct oracle_update_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 50 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset            fee;
      account_id_type  owner;
      oracle_id_type   oracle_id;

      optional<string>         new_description;
      optional<oracle_options> new_options;

      extensions_type  extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
      share_type      calculate_fee( const fee_params_t& k )const;
   };

   /**
    * @brief Removes an oracle.
    * @ingroup operations
    *
    * Refused while anything on chain still references it.
    */
   struct oracle_delete_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 0; };

      asset            fee;
      account_id_type  owner;
      oracle_id_type   oracle_id;

      extensions_type  extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
   };

   /**
    * @brief Submits one value to an oracle.
    * @ingroup operations
    *
    * The narrowest operation in the set on purpose. A producer supplies a price and nothing
    * else: it cannot alter policy, add producers, or affect any oracle it is not a producer
    * of.
    */
   struct oracle_publish_operation : public base_operation
   {
      /// Published often by design, so the fee is low. Spam is bounded by the producer set
      /// being an allow-list the owner controls, not by the fee.
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 10; };

      asset            fee;
      account_id_type  producer;
      oracle_id_type   oracle_id;

      /// Must be quoted as base_asset/quote_asset of the referenced oracle.
      price            value;

      extensions_type  extensions;

      account_id_type fee_payer()const { return producer; }
      void            validate()const;
   };

   /// @return whether @p name is acceptable as an oracle name
   bool is_valid_oracle_name( const string& name );

} } // graphene::protocol

FC_REFLECT_ENUM( graphene::protocol::oracle_aggregation_method,
                 (median_of_latest)(median_over_window) )

FC_REFLECT( graphene::protocol::oracle_options,
            (producers)(minimum_producers)(value_lifetime_sec)(aggregation)(window_sec)
            (extensions) )

FC_REFLECT( graphene::protocol::oracle_create_operation::fee_params_t, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::oracle_update_operation::fee_params_t, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::oracle_delete_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::oracle_publish_operation::fee_params_t, (fee) )

FC_REFLECT( graphene::protocol::oracle_create_operation,
            (fee)(owner)(name)(description)(base_asset)(quote_asset)(options)(extensions) )
FC_REFLECT( graphene::protocol::oracle_update_operation,
            (fee)(owner)(oracle_id)(new_description)(new_options)(extensions) )
FC_REFLECT( graphene::protocol::oracle_delete_operation,
            (fee)(owner)(oracle_id)(extensions) )
FC_REFLECT( graphene::protocol::oracle_publish_operation,
            (fee)(producer)(oracle_id)(value)(extensions) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_options )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_update_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_delete_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_publish_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_delete_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::oracle_publish_operation )
