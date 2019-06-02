/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#define GRAPHENE_SYMBOL "BTS"
#define GRAPHENE_ADDRESS_PREFIX "BTS"

#define GRAPHENE_BLOCKCHAIN_PRECISION        uint64_t( 100000 )
#define GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS 5

#define GRAPHENE_MIN_ACCOUNT_NAME_LENGTH 1
#define GRAPHENE_MAX_ACCOUNT_NAME_LENGTH 63

#define GRAPHENE_MIN_ASSET_SYMBOL_LENGTH 3
#define GRAPHENE_MAX_ASSET_SYMBOL_LENGTH 16

#define GRAPHENE_MAX_SHARE_SUPPLY int64_t(1000000000000000ll)

#define GRAPHENE_MAX_WORKER_NAME_LENGTH                       63
#define GRAPHENE_MAX_URL_LENGTH                               127

#define GRAPHENE_MAX_SIG_CHECK_DEPTH 2

#define GRAPHENE_IRREVERSIBLE_THRESHOLD                      (70 * GRAPHENE_1_PERCENT)

/**
 * every second, the fraction of burned core asset which cycles is
 * GRAPHENE_CORE_ASSET_CYCLE_RATE / (1 << GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS)
 */
#define GRAPHENE_CORE_ASSET_CYCLE_RATE                        17
#define GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS                   32

/**
 * Don't allow the committee_members to publish a limit that would
 * make the network unable to operate.
 */
#define GRAPHENE_MIN_TRANSACTION_SIZE_LIMIT 1024
#define GRAPHENE_MIN_BLOCK_INTERVAL   1 /* seconds */
#define GRAPHENE_MAX_BLOCK_INTERVAL  30 /* seconds */

#define GRAPHENE_DEFAULT_BLOCK_INTERVAL  5 /* seconds */
#define GRAPHENE_DEFAULT_MAX_TRANSACTION_SIZE 2048
#define GRAPHENE_DEFAULT_MAX_BLOCK_SIZE  (2*1000*1000) /* < 2 MiB (less than MAX_MESSAGE_SIZE in graphene/net/config.hpp) */
#define GRAPHENE_DEFAULT_MAX_TIME_UNTIL_EXPIRATION (60*60*24) // seconds,  aka: 1 day
#define GRAPHENE_DEFAULT_MAINTENANCE_INTERVAL  (60*60*24) // seconds, aka: 1 day
#define GRAPHENE_DEFAULT_MAINTENANCE_SKIP_SLOTS 3  // number of slots to skip for maintenance interval

#define GRAPHENE_DEFAULT_FORCE_SETTLEMENT_DELAY                 (60*60*24) ///< 1 day
#define GRAPHENE_DEFAULT_FORCE_SETTLEMENT_OFFSET                0 ///< 1%
#define GRAPHENE_DEFAULT_FORCE_SETTLEMENT_MAX_VOLUME            (20* GRAPHENE_1_PERCENT) ///< 20%
#define GRAPHENE_DEFAULT_PRICE_FEED_LIFETIME                    (60*60*24) ///< 1 day
#define GRAPHENE_DEFAULT_MAX_AUTHORITY_MEMBERSHIP               10
#define GRAPHENE_DEFAULT_MAX_ASSET_WHITELIST_AUTHORITIES        10
#define GRAPHENE_DEFAULT_MAX_ASSET_FEED_PUBLISHERS              10

#define GRAPHENE_DEFAULT_MIN_WITNESS_COUNT                    (11)
#define GRAPHENE_DEFAULT_MIN_COMMITTEE_MEMBER_COUNT           (11)
#define GRAPHENE_DEFAULT_MAX_WITNESSES                        (1001) // SHOULD BE ODD
#define GRAPHENE_DEFAULT_MAX_COMMITTEE                        (1001) // SHOULD BE ODD
#define GRAPHENE_DEFAULT_MAX_PROPOSAL_LIFETIME_SEC            (60*60*24*7*4) // Four weeks
#define GRAPHENE_DEFAULT_COMMITTEE_PROPOSAL_REVIEW_PERIOD_SEC (60*60*24*7*2) // Two weeks
#define GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE               (20*GRAPHENE_1_PERCENT)
#define GRAPHENE_DEFAULT_LIFETIME_REFERRER_PERCENT_OF_FEE     (30*GRAPHENE_1_PERCENT)
#define GRAPHENE_DEFAULT_CASHBACK_VESTING_PERIOD_SEC          (60*60*24*365) ///< 1 year
#define GRAPHENE_DEFAULT_CASHBACK_VESTING_THRESHOLD           (GRAPHENE_BLOCKCHAIN_PRECISION*int64_t(100))
#define GRAPHENE_DEFAULT_BURN_PERCENT_OF_FEE                  (20*GRAPHENE_1_PERCENT)
#define GRAPHENE_DEFAULT_MAX_ASSERT_OPCODE                    1
#define GRAPHENE_DEFAULT_FEE_LIQUIDATION_THRESHOLD            GRAPHENE_BLOCKCHAIN_PRECISION * 100;
#define GRAPHENE_DEFAULT_ACCOUNTS_PER_FEE_SCALE               1000
#define GRAPHENE_DEFAULT_ACCOUNT_FEE_SCALE_BITSHIFTS          4
#define GRAPHENE_DEFAULT_MAX_BUYBACK_MARKETS                  4

#define GRAPHENE_DEFAULT_WITNESS_PAY_PER_BLOCK            (GRAPHENE_BLOCKCHAIN_PRECISION * int64_t( 10) )
#define GRAPHENE_DEFAULT_WITNESS_PAY_VESTING_SECONDS      (60*60*24)
#define GRAPHENE_DEFAULT_WORKER_BUDGET_PER_DAY            (GRAPHENE_BLOCKCHAIN_PRECISION * int64_t(500) * 1000 )
#define GRAPHENE_DEFAULT_MINIMUM_FEEDS                       7

#define GRAPHENE_MIN_BLOCK_SIZE_LIMIT (GRAPHENE_MIN_TRANSACTION_SIZE_LIMIT*5) // 5 transactions per block

/** percentage fields are fixed point with a denominator of 10,000 */
#define GRAPHENE_100_PERCENT                                    10000
#define GRAPHENE_1_PERCENT                                      (GRAPHENE_100_PERCENT/100)
/** NOTE: making this a power of 2 (say 2^15) would greatly accelerate fee calcs */

#define GRAPHENE_MAX_MARKET_FEE_PERCENT                         GRAPHENE_100_PERCENT
/**
 *  These ratios are fixed point numbers with a denominator of GRAPHENE_COLLATERAL_RATIO_DENOM, the
 *  minimum maitenance collateral is therefore 1.001x and the default
 *  maintenance ratio is 1.75x
 */
///@{
#define GRAPHENE_COLLATERAL_RATIO_DENOM                 1000
#define GRAPHENE_MIN_COLLATERAL_RATIO                   1001  ///< lower than this could result in divide by 0
#define GRAPHENE_MAX_COLLATERAL_RATIO                   32000 ///< higher than this is unnecessary and may exceed int16 storage
#define GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO   1750 ///< Call when collateral only pays off 175% the debt
#define GRAPHENE_DEFAULT_MAX_SHORT_SQUEEZE_RATIO        1500 ///< Stop calling when collateral only pays off 150% of the debt
///@}

/**
 *  Reserved Account IDs with special meaning
 */
///@{
/// Represents the current committee members, two-week review period
#define GRAPHENE_COMMITTEE_ACCOUNT (graphene::protocol::account_id_type(0))
/// Represents the current witnesses
#define GRAPHENE_WITNESS_ACCOUNT (graphene::protocol::account_id_type(1))
/// Represents the current committee members
#define GRAPHENE_RELAXED_COMMITTEE_ACCOUNT (graphene::protocol::account_id_type(2))
/// Represents the canonical account with NO authority (nobody can access funds in null account)
#define GRAPHENE_NULL_ACCOUNT (graphene::protocol::account_id_type(3))
/// Represents the canonical account with WILDCARD authority (anybody can access funds in temp account)
#define GRAPHENE_TEMP_ACCOUNT (graphene::protocol::account_id_type(4))
/// Represents the canonical account for specifying you will vote directly (as opposed to a proxy)
#define GRAPHENE_PROXY_TO_SELF_ACCOUNT (graphene::protocol::account_id_type(5))
/// Sentinel value used in the scheduler.
#define GRAPHENE_NULL_WITNESS (graphene::protocol::witness_id_type(0))
///@}

#define GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET (asset_id_type(743))
