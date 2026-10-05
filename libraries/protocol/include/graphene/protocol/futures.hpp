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

   constexpr size_t GRAPHENE_FUTURES_MIN_SYMBOL_LENGTH = 3;
   constexpr size_t GRAPHENE_FUTURES_MAX_SYMBOL_LENGTH = 32;
   constexpr size_t GRAPHENE_FUTURES_MAX_DESCRIPTION_LENGTH = 1000;

   /// Leverage is bounded by the initial margin ratio; 1% initial margin is 100x, which is
   /// already beyond what most venues offer and well beyond what a thin book can liquidate.
   constexpr uint16_t GRAPHENE_FUTURES_MIN_INITIAL_MARGIN_RATIO = 100;   // 1%, i.e. 100x

   /// A dated contract more than this far out is almost certainly a typo in the timestamp.
   constexpr uint32_t GRAPHENE_FUTURES_MAX_EXPIRY_DAYS = 3650;

   /// Per-interval funding cap. 1% of notional per interval is already extreme.
   constexpr uint32_t GRAPHENE_FUTURES_MAX_FUNDING_RATE_PPM = 10000;
   /// 1% of notional. Well above any venue's taker fee, and low enough that a market
   /// cannot be configured to consume a position through trading costs alone.
   constexpr uint32_t GRAPHENE_FUTURES_MAX_TRADING_FEE_PPM = 10000;
   /// Bounds the book walk a single premium sample can cause.
   constexpr uint32_t GRAPHENE_FUTURES_MAX_IMPACT_SIZE = 1000000;

   constexpr uint32_t GRAPHENE_FUTURES_MIN_FUNDING_INTERVAL_SEC = 60;

   /// Owner-settable market policy. Shared by create and update.
   struct futures_market_options
   {
      /**
       * Fraction of notional that must be posted to open, in GRAPHENE_100_PERCENT units.
       * This is what bounds leverage: 1000 (10%) is 10x.
       */
      uint16_t initial_margin_ratio = 1000;

      /**
       * Fraction of notional below which a position is liquidated, in GRAPHENE_100_PERCENT
       * units. Must be below @ref initial_margin_ratio, or a position would be liquidatable
       * the instant it opened.
       */
      uint16_t maintenance_margin_ratio = 500;

      /**
       * How fast the mark price may move, in parts per million of the mark PER SECOND. Zero
       * disables the limit, which is the default.
       *
       * The mark is what margin, liquidation and settlement are measured against, and without
       * a limit it is whatever the oracle last said. A single print -- a manipulated one, or a
       * genuine wick that reverts in the next block -- becomes the mark immediately and can
       * cascade liquidations across every position in the market. Rate-limiting it means a
       * sustained move still arrives, just not all at once, which is the trade every venue
       * that quotes a mark separately from an index makes.
       *
       * Per second rather than per update, because updates happen on oracle publishes: a
       * per-update limit would let a producer walk the mark as fast as they cared to publish.
       *
       * Defaults to 0.1% per second, which is chosen rather than inherited:
       *
       *   a one-block spike (5s) is clipped to 0.5%, so flashing a print is useless;
       *   a genuine 10% move is fully priced in within 100 seconds;
       *   the allowance over a minute is 6%, which tracks real crypto volatility.
       *
       * Tighter and liquidations lag a real crash badly, leaving bad debt for the insurance
       * fund; looser and a single print still repricing everything. Zero disables the limit
       * entirely, which an owner may choose for a market whose oracle they already trust to
       * aggregate -- but it must be a choice, because the unprotected case is the dangerous
       * one and a default of zero would have made it the common one.
       */
      uint32_t max_mark_move_ppm = 1000;

      /**
       * Trading fees, in parts per million of the notional filled.
       *
       * The taker pays; the maker is paid `maker_rebate_ppm` of it and the remainder
       * capitalises the insurance fund, which is what absorbs a bankruptcy before the winning
       * side is asked to. Paying the maker is the point: resting an order is what gives the
       * market a book to trade against, and a venue that charges both sides equally is asking
       * for liquidity it declines to pay for.
       *
       * Both default to zero, and unlike max_mark_move_ppm that is the right default. A fee is
       * not a safety control -- it is the operator's commercial choice, it changes what every
       * trade costs, and a market that charges nothing is a coherent thing to run. The mark
       * limit defaults ON because an unprotected market is dangerous; a free market is not.
       */
      uint32_t taker_fee_ppm = 0;
      uint32_t maker_rebate_ppm = 0;

      /// Perpetuals only, ignored by dated contracts.
      uint32_t funding_interval_sec = 28800;   // 8 hours, the common venue default
      uint32_t max_funding_rate_ppm = 750;     // 0.075% per interval

      /**
       * Contracts the funding premium is priced over: the IMPACT size.
       *
       * The premium used to be sampled from the mid of best bid and best ask, with no regard
       * for the size behind either quote. Two one-contract orders therefore moved the funding
       * rate exactly as far as two million-contract orders would, so anyone holding a position
       * could bracket the book with a single contract a side and collect the capped rate on
       * their whole position -- paid by the other side, and risking only the one contract that
       * was actually exposed.
       *
       * Sampling the volume-weighted price of filling this many contracts instead means moving
       * the rate requires moving real depth, and the cost of doing so scales with impact_size
       * rather than staying flat at one contract. This is what BitMEX and Binance do, and for
       * the same reason.
       *
       * A thin side is priced over whatever depth exists rather than being skipped: that is the
       * honest reading of a thin book, and it is still strictly harder to move than one quote.
       */
      uint32_t impact_size = 10;

      /**
       * Share of a liquidated position's notional kept as a penalty, in GRAPHENE_100_PERCENT
       * units. It is what pays the liquidator for taking the position on, so a market with no
       * penalty has no liquidators and therefore no liquidation.
       */
      uint16_t liquidation_penalty_ratio = 100;   // 1%

      /// Whether new orders are accepted. Lets an owner halt a market without deleting it,
      /// which matters because deleting one with open positions must never be possible.
      bool enabled = true;

      extensions_type extensions;

      void validate()const;
   };

   /**
    * @brief Defines a futures contract.
    * @ingroup operations
    *
    * The price of a contract is quoted as an integer amount of @ref collateral_asset per
    * contract, never as a ratio. See FUTURES-DESIGN.md: it is what makes position accounting
    * exact, and exactness is what keeps the market solvent.
    */
   struct futures_market_create_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 1000 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset            fee;
      account_id_type  owner;

      /// Unique, e.g. "BTC-PERP". Uppercase letters, digits, '.' and '-'.
      string           symbol;
      string           description;

      /// Index price source. Its quote asset must be @ref collateral_asset, so that PnL and
      /// margin are denominated in the same thing and no conversion is ever needed.
      oracle_id_type   oracle_id;

      /// Margin and PnL currency.
      asset_id_type    collateral_asset;

      /// Units of the oracle's base asset represented by one contract.
      share_type       contract_size;

      /// Absent means perpetual. Present means the contract settles at the oracle price at
      /// this time and then accepts nothing further.
      optional<time_point_sec> expiry;

      futures_market_options options;

      extensions_type  extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
      share_type      calculate_fee( const fee_params_t& k )const;
   };

   /**
    * @brief Changes a futures market's policy.
    * @ingroup operations
    *
    * The oracle, collateral asset, contract size and expiry cannot be changed. Traders hold
    * positions priced against all four, and altering any of them underneath an open position
    * would silently change what that position is worth.
    */
   struct futures_market_update_operation : public base_operation
   {
      struct fee_params_t {
         uint64_t fee = 50 * GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      };

      asset                    fee;
      account_id_type          owner;
      futures_market_id_type   market_id;

      optional<string>                 new_description;
      optional<futures_market_options> new_options;

      extensions_type          extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
      share_type      calculate_fee( const fee_params_t& k )const;
   };

   /**
    * @brief Places a limit order on a futures market.
    * @ingroup operations
    *
    * Margin for the whole order is reserved when it is placed, not when it fills. An order
    * that could not be paid for if it filled has no business resting on the book.
    */
   struct futures_order_create_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 2; };

      asset                   fee;
      account_id_type         owner;
      futures_market_id_type  market_id;

      /// true to buy contracts (go long), false to sell (go short)
      bool                    is_long = true;

      /// Integer collateral per contract. Never a ratio; see FUTURES-DESIGN.md.
      share_type              price_per_contract;

      /// Number of contracts.
      share_type              size;

      /// If true, any unfilled remainder is discarded rather than resting on the book.
      bool                    fill_or_kill = false;

      extensions_type         extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
   };

   /**
    * @brief Cancels a resting futures order, returning its reserved margin.
    * @ingroup operations
    */
   struct futures_order_cancel_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = 0; };

      asset                  fee;
      account_id_type        owner;
      futures_order_id_type  order_id;

      extensions_type        extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
   };

   /**
    * @brief Virtual operation emitted when two futures orders match.
    * @ingroup operations
    *
    * Never signed. It exists so history and block explorers can see what the chain did, the
    * same reason fill_order_operation exists for spot.
    */
   struct futures_fill_operation : public base_operation
   {
      struct fee_params_t {};

      futures_fill_operation(){}
      futures_fill_operation( futures_market_id_type m, account_id_type a, bool l,
                              share_type s, share_type p, bool maker )
         : market_id(m), account_id(a), is_long(l), size(s), fill_price(p), is_maker(maker) {}

      asset                  fee;   ///< always zero; virtual operations charge nothing
      futures_market_id_type market_id;
      account_id_type        account_id;
      bool                   is_long = true;
      share_type             size;
      share_type             fill_price;
      bool                   is_maker = true;

      account_id_type fee_payer()const { return account_id; }
      void            validate()const { FC_ASSERT( !"virtual operation" ); }

      /// Virtual operations are not signed, so they are never charged.
      share_type calculate_fee( const fee_params_t& )const { return 0; }
   };

   /**
    * @brief Adds or withdraws margin on an open position.
    * @ingroup operations
    */
   struct futures_position_adjust_margin_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 2; };

      asset                     fee;
      account_id_type           owner;
      futures_position_id_type  position_id;

      /// Positive to add margin, negative to withdraw it. A withdrawal that would leave the
      /// position below its initial margin requirement is refused.
      share_type                delta;

      extensions_type           extensions;

      account_id_type fee_payer()const { return owner; }
      void            validate()const;
   };

   /**
    * @brief Liquidates an under-margined position.
    * @ingroup operations
    *
    * Permissionless on purpose. Scanning every position whenever the mark moves would put
    * unbounded work into consensus, so liquidation is an economic activity instead: anyone may
    * call it, it fails unless the position really is under water, and the liquidator takes the
    * position over along with the penalty that makes doing so worthwhile.
    */
   struct futures_liquidate_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 2; };

      asset                     fee;
      account_id_type           liquidator;
      futures_position_id_type  position_id;

      extensions_type           extensions;

      account_id_type fee_payer()const { return liquidator; }
      void            validate()const;
   };

   /**
    * @brief Settles a dated contract at expiry, and closes one position at the settled price.
    * @ingroup operations
    *
    * Permissionless, and takes one position at a time, for the same reason liquidation does:
    * walking every position in a market at expiry would put unbounded work into a block.
    * The first caller after expiry snapshots the oracle into the settlement price; every
    * caller after that closes whichever position they name against it.
    */
   struct futures_settle_operation : public base_operation
   {
      struct fee_params_t { uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION / 2; };

      asset                   fee;
      account_id_type         payer;
      futures_market_id_type  market_id;

      /// Absent settles the market itself without closing anything, which is what lets a
      /// market with no open positions still reach a settled state.
      optional<futures_position_id_type> position_id;

      extensions_type         extensions;

      account_id_type fee_payer()const { return payer; }
      void            validate()const;
   };

   /// @return whether @p symbol is acceptable as a futures market symbol
   bool is_valid_futures_symbol( const string& symbol );

} } // graphene::protocol

FC_REFLECT( graphene::protocol::futures_market_options,
            (initial_margin_ratio)(maintenance_margin_ratio)(max_mark_move_ppm)
            (taker_fee_ppm)(maker_rebate_ppm)
            (funding_interval_sec)(max_funding_rate_ppm)(impact_size)
            (liquidation_penalty_ratio)(enabled)(extensions) )

FC_REFLECT( graphene::protocol::futures_position_adjust_margin_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::futures_liquidate_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::futures_position_adjust_margin_operation,
            (fee)(owner)(position_id)(delta)(extensions) )
FC_REFLECT( graphene::protocol::futures_liquidate_operation,
            (fee)(liquidator)(position_id)(extensions) )
FC_REFLECT( graphene::protocol::futures_settle_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::futures_settle_operation,
            (fee)(payer)(market_id)(position_id)(extensions) )

FC_REFLECT( graphene::protocol::futures_market_create_operation::fee_params_t,
            (fee)(price_per_kbyte) )
FC_REFLECT( graphene::protocol::futures_market_update_operation::fee_params_t,
            (fee)(price_per_kbyte) )

FC_REFLECT( graphene::protocol::futures_market_create_operation,
            (fee)(owner)(symbol)(description)(oracle_id)(collateral_asset)(contract_size)
            (expiry)(options)(extensions) )
FC_REFLECT( graphene::protocol::futures_market_update_operation,
            (fee)(owner)(market_id)(new_description)(new_options)(extensions) )

FC_REFLECT( graphene::protocol::futures_order_create_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::futures_order_cancel_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::futures_fill_operation::fee_params_t, )

FC_REFLECT( graphene::protocol::futures_order_create_operation,
            (fee)(owner)(market_id)(is_long)(price_per_contract)(size)(fill_or_kill)(extensions) )
FC_REFLECT( graphene::protocol::futures_order_cancel_operation,
            (fee)(owner)(order_id)(extensions) )
FC_REFLECT( graphene::protocol::futures_fill_operation,
            (fee)(market_id)(account_id)(is_long)(size)(fill_price)(is_maker) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_options )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_market_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_market_update_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_market_update_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_order_create_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_order_cancel_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_fill_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_order_create_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_order_cancel_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_fill_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_position_adjust_margin_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_liquidate_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_position_adjust_margin_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_liquidate_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(
      graphene::protocol::futures_settle_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::futures_settle_operation )
