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

#include <fc/exception/exception.hpp>

namespace graphene { namespace chain {
   // registered in chain_database.cpp

   FC_DECLARE_EXCEPTION( chain_exception, 30000, "Blockchain Exception" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_pts_address,               graphene::chain::chain_exception, 30001, "invalid pts address" );
   FC_DECLARE_DERIVED_EXCEPTION( addition_overflow,                 graphene::chain::chain_exception, 30002, "addition overflow" );
   FC_DECLARE_DERIVED_EXCEPTION( subtraction_overflow,              graphene::chain::chain_exception, 30003, "subtraction overflow" );
   FC_DECLARE_DERIVED_EXCEPTION( asset_type_mismatch,               graphene::chain::chain_exception, 30004, "asset/price mismatch" );
   FC_DECLARE_DERIVED_EXCEPTION( unsupported_chain_operation,       graphene::chain::chain_exception, 30005, "unsupported chain operation" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_transaction,               graphene::chain::chain_exception, 30006, "unknown transaction" );
   FC_DECLARE_DERIVED_EXCEPTION( duplicate_transaction,             graphene::chain::chain_exception, 30007, "duplicate transaction" );
   FC_DECLARE_DERIVED_EXCEPTION( zero_amount,                       graphene::chain::chain_exception, 30008, "zero amount" );
   FC_DECLARE_DERIVED_EXCEPTION( zero_price,                        graphene::chain::chain_exception, 30009, "zero price" );
   FC_DECLARE_DERIVED_EXCEPTION( asset_divide_by_self,              graphene::chain::chain_exception, 30010, "asset divide by self" );
   FC_DECLARE_DERIVED_EXCEPTION( asset_divide_by_zero,              graphene::chain::chain_exception, 30011, "asset divide by zero" );
   FC_DECLARE_DERIVED_EXCEPTION( new_database_version,              graphene::chain::chain_exception, 30012, "new database version" );
   FC_DECLARE_DERIVED_EXCEPTION( unlinkable_block,                  graphene::chain::chain_exception, 30013, "unlinkable block" );
   FC_DECLARE_DERIVED_EXCEPTION( price_out_of_range,                graphene::chain::chain_exception, 30014, "price out of range" );

   FC_DECLARE_DERIVED_EXCEPTION( block_numbers_not_sequential,      graphene::chain::chain_exception, 30015, "block numbers not sequential" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_previous_block_id,         graphene::chain::chain_exception, 30016, "invalid previous block" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_block_time,                graphene::chain::chain_exception, 30017, "invalid block time" );
   FC_DECLARE_DERIVED_EXCEPTION( time_in_past,                      graphene::chain::chain_exception, 30018, "time is in the past" );
   FC_DECLARE_DERIVED_EXCEPTION( time_in_future,                    graphene::chain::chain_exception, 30019, "time is in the future" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_block_digest,              graphene::chain::chain_exception, 30020, "invalid block digest" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_delegate_signee,           graphene::chain::chain_exception, 30021, "invalid delegate signee" );
   FC_DECLARE_DERIVED_EXCEPTION( failed_checkpoint_verification,    graphene::chain::chain_exception, 30022, "failed checkpoint verification" );
   FC_DECLARE_DERIVED_EXCEPTION( wrong_chain_id,                    graphene::chain::chain_exception, 30023, "wrong chain id" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_block,                     graphene::chain::chain_exception, 30024, "unknown block" );
   FC_DECLARE_DERIVED_EXCEPTION( block_older_than_undo_history,     graphene::chain::chain_exception, 30025, "block is older than our undo history allows us to process" );

   FC_DECLARE_EXCEPTION( evaluation_error, 31000, "Evaluation Error" );
   FC_DECLARE_DERIVED_EXCEPTION( negative_deposit,                  graphene::chain::evaluation_error, 31001, "negative deposit" );
   FC_DECLARE_DERIVED_EXCEPTION( not_a_delegate,                    graphene::chain::evaluation_error, 31002, "not a delegate" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_balance_record,            graphene::chain::evaluation_error, 31003, "unknown balance record" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_funds,                graphene::chain::evaluation_error, 31004, "insufficient funds" );
   FC_DECLARE_DERIVED_EXCEPTION( missing_signature,                 graphene::chain::evaluation_error, 31005, "missing signature" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_claim_password,            graphene::chain::evaluation_error, 31006, "invalid claim password" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_withdraw_condition,        graphene::chain::evaluation_error, 31007, "invalid withdraw condition" );
   FC_DECLARE_DERIVED_EXCEPTION( negative_withdraw,                 graphene::chain::evaluation_error, 31008, "negative withdraw" );
   FC_DECLARE_DERIVED_EXCEPTION( not_an_active_delegate,            graphene::chain::evaluation_error, 31009, "not an active delegate" );
   FC_DECLARE_DERIVED_EXCEPTION( expired_transaction,               graphene::chain::evaluation_error, 31010, "expired transaction" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_transaction_expiration,    graphene::chain::evaluation_error, 31011, "invalid transaction expiration" );
   FC_DECLARE_DERIVED_EXCEPTION( oversized_transaction,             graphene::chain::evaluation_error, 31012, "transaction exceeded the maximum transaction size" );

   FC_DECLARE_DERIVED_EXCEPTION( invalid_account_name,              graphene::chain::evaluation_error, 32001, "invalid account name" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_account_id,                graphene::chain::evaluation_error, 32002, "unknown account id" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_account_name,              graphene::chain::evaluation_error, 32003, "unknown account name" );
   FC_DECLARE_DERIVED_EXCEPTION( missing_parent_account_signature,  graphene::chain::evaluation_error, 32004, "missing parent account signature" );
   FC_DECLARE_DERIVED_EXCEPTION( parent_account_retracted,          graphene::chain::evaluation_error, 32005, "parent account retracted" );
   FC_DECLARE_DERIVED_EXCEPTION( account_expired,                   graphene::chain::evaluation_error, 32006, "account expired" );
   FC_DECLARE_DERIVED_EXCEPTION( account_already_registered,        graphene::chain::evaluation_error, 32007, "account already registered" );
   FC_DECLARE_DERIVED_EXCEPTION( account_key_in_use,                graphene::chain::evaluation_error, 32008, "account key already in use" );
   FC_DECLARE_DERIVED_EXCEPTION( account_retracted,                 graphene::chain::evaluation_error, 32009, "account retracted" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_parent_account_name,       graphene::chain::evaluation_error, 32010, "unknown parent account name" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_delegate_slate,            graphene::chain::evaluation_error, 32011, "unknown delegate slate" );
   FC_DECLARE_DERIVED_EXCEPTION( too_may_delegates_in_slate,        graphene::chain::evaluation_error, 32012, "too many delegates in slate" );
   FC_DECLARE_DERIVED_EXCEPTION( pay_balance_remaining,             graphene::chain::evaluation_error, 32013, "pay balance remaining" );

   FC_DECLARE_DERIVED_EXCEPTION( not_a_delegate_signature,          graphene::chain::evaluation_error, 33002, "not delegates signature" );

   FC_DECLARE_DERIVED_EXCEPTION( invalid_precision,                 graphene::chain::evaluation_error, 35001, "invalid precision" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_asset_symbol,              graphene::chain::evaluation_error, 35002, "invalid asset symbol" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_asset_id,                  graphene::chain::evaluation_error, 35003, "unknown asset id" );
   FC_DECLARE_DERIVED_EXCEPTION( asset_symbol_in_use,               graphene::chain::evaluation_error, 35004, "asset symbol in use" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_asset_amount,              graphene::chain::evaluation_error, 35005, "invalid asset amount" );
   FC_DECLARE_DERIVED_EXCEPTION( negative_issue,                    graphene::chain::evaluation_error, 35006, "negative issue" );
   FC_DECLARE_DERIVED_EXCEPTION( over_issue,                        graphene::chain::evaluation_error, 35007, "over issue" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_asset_symbol,              graphene::chain::evaluation_error, 35008, "unknown asset symbol" );
   FC_DECLARE_DERIVED_EXCEPTION( asset_id_in_use,                   graphene::chain::evaluation_error, 35009, "asset id in use" );
   FC_DECLARE_DERIVED_EXCEPTION( not_user_issued,                   graphene::chain::evaluation_error, 35010, "not user issued" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_asset_name,                graphene::chain::evaluation_error, 35011, "invalid asset name" );

   FC_DECLARE_DERIVED_EXCEPTION( delegate_vote_limit,               graphene::chain::evaluation_error, 36001, "delegate_vote_limit" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_fee,                  graphene::chain::evaluation_error, 36002, "insufficient fee" );
   FC_DECLARE_DERIVED_EXCEPTION( negative_fee,                      graphene::chain::evaluation_error, 36003, "negative fee" );
   FC_DECLARE_DERIVED_EXCEPTION( missing_deposit,                   graphene::chain::evaluation_error, 36004, "missing deposit" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_relay_fee,            graphene::chain::evaluation_error, 36005, "insufficient relay fee" );

   FC_DECLARE_DERIVED_EXCEPTION( invalid_market,                    graphene::chain::evaluation_error, 37001, "invalid market" );
   FC_DECLARE_DERIVED_EXCEPTION( unknown_market_order,              graphene::chain::evaluation_error, 37002, "unknown market order" );
   FC_DECLARE_DERIVED_EXCEPTION( shorting_base_shares,              graphene::chain::evaluation_error, 37003, "shorting base shares" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_collateral,           graphene::chain::evaluation_error, 37004, "insufficient collateral" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_depth,                graphene::chain::evaluation_error, 37005, "insufficient depth" );
   FC_DECLARE_DERIVED_EXCEPTION( insufficient_feeds,                graphene::chain::evaluation_error, 37006, "insufficient feeds" );
   FC_DECLARE_DERIVED_EXCEPTION( invalid_feed_price,                graphene::chain::evaluation_error, 37007, "invalid feed price" );

   FC_DECLARE_DERIVED_EXCEPTION( price_multiplication_overflow,     graphene::chain::evaluation_error, 38001, "price multiplication overflow" );
   FC_DECLARE_DERIVED_EXCEPTION( price_multiplication_underflow,    graphene::chain::evaluation_error, 38002, "price multiplication underflow" );
   FC_DECLARE_DERIVED_EXCEPTION( price_multiplication_undefined,    graphene::chain::evaluation_error, 38003, "price multiplication undefined product 0*inf" );

} } // graphene::chain
