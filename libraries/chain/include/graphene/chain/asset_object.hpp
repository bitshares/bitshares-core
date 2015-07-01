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
#include <boost/multi_index/composite_key.hpp>
#include <graphene/chain/asset.hpp>
#include <graphene/db/flat_index.hpp>
#include <graphene/db/generic_index.hpp>

/**
 * @defgroup prediction_market Prediction Market
 *
 * A prediction market is a specialized BitAsset such that total debt and total collateral are always equal amounts
 * (although asset IDs differ). No margin calls or force settlements may be performed on a prediction market asset. A
 * prediction market is globally settled by the issuer after the event being predicted resolves, thus a prediction
 * market must always have the @ref global_settle permission enabled. The maximum price for global settlement or short
 * sale of a prediction market asset is 1-to-1.
 */

namespace graphene { namespace chain {
   class account_object;
   class database;
   using namespace graphene::db;

   /**
    *  @brief tracks the asset information that changes frequently
    *  @ingroup object
    *  @ingroup implementation
    *
    *  Because the asset_object is very large it doesn't make sense to save an undo state
    *  for all of the parameters that never change.   This object factors out the parameters
    *  of an asset that change in almost every transaction that involves the asset.
    *
    *  This object exists as an implementation detail and its ID should never be referenced by
    *  a blockchain operation.
    */
   class asset_dynamic_data_object : public abstract_object<asset_dynamic_data_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_asset_dynamic_data_type;

         /// The number of shares currently in existence
         share_type current_supply;
         share_type accumulated_fees; ///< fees accumulate to be paid out over time
         share_type fee_pool;         ///< in core asset
   };

   /**
    *  @brief tracks the parameters of an asset
    *  @ingroup object
    *
    *  All assets have a globally unique symbol name that controls how they are traded and an issuer who
    *  has authority over the parameters of the asset.
    */
   class asset_object : public graphene::db::annotated_object<asset_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = asset_object_type;

         /// This function does not check if any registered asset has this symbol or not; it simply checks whether the
         /// symbol would be valid.
         /// @return true if symbol is a valid ticker symbol; false otherwise.
         static bool is_valid_symbol( const string& symbol );

         /// @return true if accounts must be on a whitelist in order to hold this asset; false otherwise.
         bool enforce_white_list()const { return options.flags & white_list; }
         /// @return true if this is a market-issued asset; false otherwise.
         bool is_market_issued()const { return bitasset_data_id.valid(); }
         /// @return true if users may request force-settlement of this market-issued asset; false otherwise
         bool can_force_settle()const { return !(options.flags & disable_force_settle); }
         /// @return true if the issuer of this market-issued asset may globally settle the asset; false otherwise
         bool can_global_settle()const { return options.issuer_permissions & global_settle; }
         /// @return true if this asset charges a fee for the issuer on market operations; false otherwise
         bool charges_market_fees()const { return options.flags & charge_market_fee; }
         /// @return true if this asset may only be transferred to/from the issuer or market orders
         bool is_transfer_restricted()const { return options.flags & transfer_restricted; }
         bool can_override()const { return options.flags & override_authority; }

         /// Helper function to get an asset object with the given amount in this asset's type
         asset amount(share_type a)const { return asset(a, id); }
         /// Convert a string amount (i.e. "123.45") to an asset object with this asset's type
         /// The string may have a decimal and/or a negative sign.
         asset amount_from_string(string amount_string)const;
         /// Convert an asset to a textual representation, i.e. "123.45"
         string amount_to_string(share_type amount)const;
         /// Convert an asset to a textual representation, i.e. "123.45"
         string amount_to_string(const asset& amount)const
         { FC_ASSERT(amount.asset_id == id); return amount_to_string(amount.amount); }
         /// Convert an asset to a textual representation with symbol, i.e. "123.45 USD"
         string amount_to_pretty_string(share_type amount)const
         { return amount_to_string(amount) + " " + symbol; }
         /// Convert an asset to a textual representation with symbol, i.e. "123.45 USD"
         string amount_to_pretty_string(const asset &amount)const
         { FC_ASSERT(amount.asset_id == id); return amount_to_pretty_string(amount.amount); }

         /// Ticker symbol for this asset, i.e. "USD"
         string                  symbol;
         /// Maximum number of digits after the decimal point (must be <= 12)
         uint8_t                 precision;
         /// ID of the account which issued this asset.
         account_id_type         issuer;

         /**
          * @brief The asset_options struct contains options available on all assets in the network
          *
          * @note Changes to this struct will break protocol compatibility
          */
         struct asset_options {
            /// The maximum supply of this asset which may exist at any given time. This can be as large as
            /// GRAPHENE_MAX_SHARE_SUPPLY
            share_type              max_supply         = GRAPHENE_MAX_SHARE_SUPPLY;
            /// When this asset is traded on the markets, this percentage of the total traded will be exacted and paid
            /// to the issuer. This is a fixed point value, representing hundredths of a percent, i.e. a value of 100
            /// in this field means a 1% fee is charged on market trades of this asset.
            uint16_t                market_fee_percent = 0;
            share_type              max_market_fee = GRAPHENE_MAX_SHARE_SUPPLY;

            /// The flags which the issuer has permission to update. See @ref asset_issuer_permission_flags
            uint16_t                issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
            /// The currently active flags on this permission. See @ref asset_issuer_permission_flags
            uint16_t                flags = 0;

            /// When a non-core asset is used to pay a fee, the blockchain must convert that asset to core asset in
            /// order to accept the fee. If this asset's fee pool is funded, the chain will automatically deposite fees
            /// in this asset to its accumulated fees, and withdraw from the fee pool the same amount as converted at
            /// the core exchange rate.
            price core_exchange_rate;

            /// A set of accounts which maintain whitelists to consult for this asset. If enforce_white_list() returns
            /// true, an account may only send, receive, trade, etc. in this asset if one of these accounts appears in
            /// its account_object::whitelisting_accounts field.
            flat_set<account_id_type> whitelist_authorities;
            /// A set of accounts which maintain blacklists to consult for this asset. If enforce_white_list() returns
            /// true, an account may only send, receive, trade, etc. in this asset if none of these accounts appears in
            /// its account_object::blacklisting_accounts field. If the account is blacklisted, it may not transact in
            /// this asset even if it is also whitelisted.
            flat_set<account_id_type> blacklist_authorities;

            /** defines the assets that this asset may be traded against in the market */
            flat_set<asset_id_type>   whitelist_markets;
            /** defines the assets that this asset may not be traded against in the market, must not overlap whitelist */
            flat_set<asset_id_type>   blacklist_markets;

            /**
             * data that describes the meaning/purpose of this asset, fee will be charged proportional to
             * size of description.
             */
            string                    description;

            /// Perform internal consistency checks.
            /// @throws fc::exception if any check fails
            void validate()const;
         } options;
         /**
          * @brief The bitasset_options struct contains configurable options available only to BitAssets.
          *
          * @note Changes to this struct will break protocol compatibility
          */
         struct bitasset_options {
            /// Time before a price feed expires
            uint32_t feed_lifetime_sec = GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME;
            /// Minimum number of unexpired feeds required to extract a median feed from
            uint8_t minimum_feeds = 1;
            /// This is the delay between the time a long requests settlement and the chain evaluates the settlement
            uint32_t force_settlement_delay_sec = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_DELAY;
            /// This is the percent to adjust the feed price in the short's favor in the event of a forced settlement
            uint16_t force_settlement_offset_percent = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_OFFSET;
            /// Force settlement volume can be limited such that only a certain percentage of the total existing supply
            /// of the asset may be force-settled within any given chain maintenance interval. This field stores the
            /// percentage of the current supply which may be force settled within the current maintenance interval. If
            /// force settlements come due in an interval in which the maximum volume has already been settled, the new
            /// settlements will be enqueued and processed at the beginning of the next maintenance interval.
            uint16_t maximum_force_settlement_volume = GRAPHENE_DEFAULT_FORCE_SETTLEMENT_MAX_VOLUME;
            /// This speicifies which asset type is used to collateralize short sales
            /// This field may only be updated if the current supply of the asset is zero.
            asset_id_type short_backing_asset;

            /// Perform internal consistency checks.
            /// @throws fc::exception if any check fails
            void validate()const;
         };

         /// Current supply, fee pool, and collected fees are stored in a separate object as they change frequently.
         dynamic_asset_data_id_type  dynamic_asset_data_id;
         /// Extra data associated with BitAssets. This field is non-null if and only if is_market_issued() returns true
         optional<asset_bitasset_data_id_type> bitasset_data_id;

         asset_id_type get_id()const { return id; }

         void validate()const
         {
            // UIAs may not be prediction markets, have force settlement, or global settlements
            if( !is_market_issued() )
            {
               FC_ASSERT(!(options.flags & disable_force_settle || options.flags & global_settle));
               FC_ASSERT(!(options.issuer_permissions & disable_force_settle || options.issuer_permissions & global_settle));
            }
         }

         template<class DB>
         const asset_bitasset_data_object& bitasset_data(const DB& db)const
         { assert(bitasset_data_id); return db.get(*bitasset_data_id); }

         template<class DB>
         const asset_dynamic_data_object& dynamic_data(const DB& db)const
         { return db.get(dynamic_asset_data_id); }

         /**
          *  The total amount of an asset that is reserved for future issuance. 
          */
         template<class DB>
         share_type reserved( const DB& db )const
         { return options.max_supply - dynamic_data(db).current_supply; }
   };

   /**
    *  @brief contains properties that only apply to bitassets (market issued assets)
    *
    *  @ingroup object
    *  @ingroup implementation
    */
   class asset_bitasset_data_object : public abstract_object<asset_bitasset_data_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_asset_bitasset_data_type;

         /// The tunable options for BitAssets are stored in this field.
         asset_object::bitasset_options options;

         /// Feeds published for this asset. If issuer is not genesis, the keys in this map are the feed publishing
         /// accounts; otherwise, the feed publishers are the currently active delegates and witnesses and this map
         /// should be treated as an implementation detail. The timestamp on each feed is the time it was published.
         flat_map<account_id_type, pair<time_point_sec,price_feed>> feeds;
         /// This is the currently active price feed, calculated as the median of values from the currently active
         /// feeds.
         price_feed current_feed;
         /// This is the publication time of the oldest feed which was factored into current_feed.
         time_point_sec current_feed_publication_time;

         /// True if this asset implements a @ref prediction_market
         bool is_prediction_market = false;

         /// This is the volume of this asset which has been force-settled this maintanence interval
         share_type force_settled_volume;
         /// Calculate the maximum force settlement volume per maintenance interval, given the current share supply
         share_type max_force_settlement_volume(share_type current_supply)const;

         /** return true if there has been a black swan, false otherwise */
         bool has_settlement()const   { return !settlement_price.is_null(); }

         /**
          *  In the event of a black swan, the swan price is saved in the settlement price,
          *  and all margin positions are settled at the same price with the siezed collateral
          *  being moved into the settlement fund.  From this point on no further updates to
          *  the asset are permitted (no feeds, etc) and forced settlement occurs using
          *  the settlement price and fund.
          */
         ///@{
         price      settlement_price;
         share_type settlement_fund;
         ///@}

         time_point_sec feed_expiration_time()const
         { return current_feed_publication_time + options.feed_lifetime_sec; }
         bool feed_is_expired(time_point_sec current_time)const
         { return feed_expiration_time() >= current_time; }
         void update_median_feeds(time_point_sec current_time);
   };


   struct by_feed_expiration;
   typedef multi_index_container<
      asset_bitasset_data_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_feed_expiration>,
            const_mem_fun< asset_bitasset_data_object, time_point_sec, &asset_bitasset_data_object::feed_expiration_time >
         >
      >
   > asset_bitasset_data_object_multi_index_type;
   typedef flat_index<asset_bitasset_data_object> asset_bitasset_data_index;

   struct by_symbol;
   typedef multi_index_container<
      asset_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_unique< tag<by_symbol>, member<asset_object, string, &asset_object::symbol> >
      >
   > asset_object_multi_index_type;
   typedef generic_index<asset_object, asset_object_multi_index_type> asset_index;


} } // graphene::chain
FC_REFLECT_DERIVED( graphene::chain::asset_dynamic_data_object, (graphene::db::object),
                    (current_supply)(accumulated_fees)(fee_pool) )

FC_REFLECT_DERIVED( graphene::chain::asset_bitasset_data_object, (graphene::db::object),
                    (feeds)
                    (current_feed)
                    (current_feed_publication_time)
                    (options)
                    (force_settled_volume)
                    (is_prediction_market)
                    (settlement_price)
                    (settlement_fund)
                  )

FC_REFLECT( graphene::chain::asset_object::asset_options,
            (max_supply)
            (market_fee_percent)
            (max_market_fee)
            (issuer_permissions)
            (flags)
            (core_exchange_rate)
            (whitelist_authorities)
            (blacklist_authorities)
            (whitelist_markets)
            (blacklist_markets)
            (description)
          )
FC_REFLECT( graphene::chain::asset_object::bitasset_options,
            (feed_lifetime_sec)
            (minimum_feeds)
            (force_settlement_delay_sec)
            (force_settlement_offset_percent)
            (maximum_force_settlement_volume)
            (short_backing_asset)
          )

FC_REFLECT_DERIVED( graphene::chain::asset_object,
                    (graphene::db::annotated_object<graphene::chain::asset_object>),
                    (symbol)
                    (precision)
                    (issuer)
                    (options)
                    (dynamic_asset_data_id)
                    (bitasset_data_id)
                  )

