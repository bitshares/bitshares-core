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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( uia_tests, database_fixture )

BOOST_AUTO_TEST_CASE( create_advanced_uia )
{
   try {
      asset_id_type test_asset_id = db.get_index<asset_object>().get_next_id();
      asset_create_operation creator;
      creator.issuer = account_id_type();
      creator.fee = asset();
      creator.symbol = "ADVANCED";
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_MASK & ~(disable_force_settle|global_settle);
      creator.common_options.flags = ASSET_ISSUER_PERMISSION_MASK & ~(disable_force_settle|global_settle|transfer_restricted);
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,1)});
      creator.common_options.whitelist_authorities = creator.common_options.blacklist_authorities = {account_id_type()};
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "ADVANCED");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK(test_asset.enforce_white_list());
      BOOST_CHECK(test_asset.options.max_supply == 100000000);
      BOOST_CHECK(!test_asset.bitasset_data_id.valid());
      BOOST_CHECK(test_asset.options.market_fee_percent == GRAPHENE_MAX_MARKET_FEE_PERCENT/100);

      const asset_dynamic_data_object& test_asset_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK(test_asset_dynamic_data.current_supply == 0);
      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_asset_dynamic_data.fee_pool == 0);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( override_transfer_test )
{ try {
   ACTORS( (dan)(eric)(sam) );
   const asset_object& advanced = create_user_issued_asset( "ADVANCED", sam, override_authority );
   issue_uia( dan, advanced.amount( 1000 ) );
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 1000 );

   trx.operations.clear();
   override_transfer_operation otrans;
   otrans.issuer = advanced.issuer;
   otrans.from = dan.id;
   otrans.to   = eric.id;
   otrans.amount = advanced.amount(100);
   trx.operations.push_back(otrans);

   BOOST_TEST_MESSAGE( "Require throwing without signature" );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), tx_missing_active_auth );
   BOOST_TEST_MESSAGE( "Require throwing with dan's signature" );
   trx.sign( dan_private_key );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), tx_missing_active_auth );
   BOOST_TEST_MESSAGE( "Pass with issuer's signature" );
   trx.signatures.clear();
   trx.sign( sam_private_key );
   PUSH_TX( db, trx, 0 );

   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 900 );
   BOOST_REQUIRE_EQUAL( get_balance( eric, advanced ), 100 );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( override_transfer_test2 )
{ try {
   ACTORS( (dan)(eric)(sam) );
   const asset_object& advanced = create_user_issued_asset( "ADVANCED", sam, 0 );
   issue_uia( dan, advanced.amount( 1000 ) );
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 1000 );

   trx.operations.clear();
   override_transfer_operation otrans;
   otrans.issuer = advanced.issuer;
   otrans.from = dan.id;
   otrans.to   = eric.id;
   otrans.amount = advanced.amount(100);
   trx.operations.push_back(otrans);

   BOOST_TEST_MESSAGE( "Require throwing without signature" );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), fc::exception);
   BOOST_TEST_MESSAGE( "Require throwing with dan's signature" );
   trx.sign( dan_private_key );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), fc::exception);
   BOOST_TEST_MESSAGE( "Fail because overide_authority flag is not set" );
   trx.signatures.clear();
   trx.sign( sam_private_key );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), fc::exception );

   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 1000 );
   BOOST_REQUIRE_EQUAL( get_balance( eric, advanced ), 0 );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( issue_whitelist_uia )
{
   try {
      INVOKE(create_advanced_uia);
      const asset_object& advanced = get_asset("ADVANCED");
      const account_object& nathan = create_account("nathan");
      upgrade_to_lifetime_member(nathan);
      trx.clear();

      asset_issue_operation op;
      op.issuer = advanced.issuer;
      op.asset_to_issue = advanced.amount(1000);
      op.issue_to_account = nathan.id; //({asset(), advanced.issuer, advanced.amount(1000), nathan.id});
      trx.operations.emplace_back(op);
      //Fail because nathan is not whitelisted.
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      account_whitelist_operation wop;
      wop.authorizing_account = account_id_type();
      wop.account_to_list = nathan.id;
      wop.new_listing = account_whitelist_operation::white_listed;

      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK(nathan.is_authorized_asset(advanced));
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan, advanced), 1000);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer_whitelist_uia )
{
   try {
      INVOKE(issue_whitelist_uia);
      const asset_object& advanced = get_asset("ADVANCED");
      const account_object& nathan = get_account("nathan");
      const account_object& dan = create_account("dan");
      upgrade_to_lifetime_member(dan);
      trx.clear();

      BOOST_TEST_MESSAGE( "Atempting to transfer asset ADVANCED from nathan to dan when dan is not whitelisted, should fail" );
      transfer_operation op;
      op.fee = advanced.amount(0);
      op.from = nathan.id;
      op.to = dan.id;
      op.amount = advanced.amount(100); //({advanced.amount(0), nathan.id, dan.id, advanced.amount(100)});
      trx.operations.push_back(op);
      //Fail because dan is not whitelisted.
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), transfer_to_account_not_whitelisted );

      BOOST_TEST_MESSAGE( "Adding dan to whitelist for asset ADVANCED" );
      account_whitelist_operation wop;
      wop.authorizing_account = account_id_type();
      wop.account_to_list = dan.id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );
      BOOST_TEST_MESSAGE( "Attempting to trnsfer from nathan to dan after whitelisting dan, should succeed" );
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan, advanced), 900);
      BOOST_CHECK_EQUAL(get_balance(dan, advanced), 100);

      BOOST_TEST_MESSAGE( "Attempting to blacklist nathan" );
      wop.new_listing |= account_whitelist_operation::black_listed;
      wop.account_to_list = nathan.id;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE( "Attempting to transfer from nathan after blacklisting, should fail" );
      op.amount = advanced.amount(50);
      trx.operations.back() = op;
      //Fail because nathan is blacklisted
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), transfer_from_account_not_whitelisted );


      BOOST_TEST_MESSAGE( "Attempting to burn from nathan after blacklisting, should fail" );
      asset_reserve_operation burn;
      burn.payer = nathan.id;
      burn.amount_to_reserve = advanced.amount(10);
      trx.operations.back() = burn;
      //Fail because nathan is blacklisted
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      BOOST_TEST_MESSAGE( "Attempting transfer from dan back to nathan, should fail because nathan is blacklisted" );
      std::swap(op.from, op.to);
      trx.operations.back() = op;
      //Fail because nathan is blacklisted
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      {
         BOOST_TEST_MESSAGE( "Changing the blacklist authority to dan" );
         asset_update_operation op;
         op.asset_to_update = advanced.id;
         op.new_options = advanced.options;
         op.new_options.blacklist_authorities.clear();
         op.new_options.blacklist_authorities.insert(dan.id);
         trx.operations.back() = op;
         PUSH_TX( db, trx, ~0 );
         BOOST_CHECK(advanced.options.blacklist_authorities.find(dan.id) != advanced.options.blacklist_authorities.end());
      }

      BOOST_TEST_MESSAGE( "Attempting to transfer from dan back to nathan" );
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan, advanced), 950);
      BOOST_CHECK_EQUAL(get_balance(dan, advanced), 50);

      BOOST_TEST_MESSAGE( "Blacklisting nathan by dan" );
      wop.authorizing_account = dan.id;
      wop.account_to_list = nathan.id;
      wop.new_listing = account_whitelist_operation::black_listed;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );

      trx.operations.back() = op;
      //Fail because nathan is blacklisted
      BOOST_CHECK(!nathan.is_authorized_asset(advanced));
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      //Remove nathan from committee's whitelist, add him to dan's. This should not authorize him to hold ADVANCED.
      wop.authorizing_account = account_id_type();
      wop.account_to_list = nathan.id;
      wop.new_listing = account_whitelist_operation::no_listing;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );
      wop.authorizing_account = dan.id;
      wop.account_to_list = nathan.id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );

      trx.operations.back() = op;
      //Fail because nathan is not whitelisted
      BOOST_CHECK(!nathan.is_authorized_asset(advanced));
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      burn.payer = dan.id;
      burn.amount_to_reserve = advanced.amount(10);
      trx.operations.back() = burn;
      PUSH_TX(db, trx, ~0);
      BOOST_CHECK_EQUAL(get_balance(dan, advanced), 40);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/**
 * verify that issuers can halt transfers
 */
BOOST_AUTO_TEST_CASE( transfer_restricted_test )
{
   try
   {
      ACTORS( (sam)(alice)(bob) );

      BOOST_TEST_MESSAGE( "Issuing 1000 UIA to Alice" );

      auto _issue_uia = [&]( const account_object& recipient, asset amount )
      {
         asset_issue_operation op;
         op.issuer = amount.asset_id(db).issuer;
         op.asset_to_issue = amount;
         op.issue_to_account = recipient.id;
         transaction tx;
         tx.operations.push_back( op );
         tx.set_expiration( db.head_block_time() + fc::minutes(5) );
         PUSH_TX( db, tx, database::skip_authority_check | database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

      asset           fee;
      account_id_type issuer;
      asset_id_type   asset_to_update;

      /// If the asset is to be given a new issuer, specify his ID here.
      optional<account_id_type>   new_issuer;
      asset_options               new_options;
      extensions_type             extensions;

      const asset_object& uia = create_user_issued_asset( "TXRX", sam, transfer_restricted );
      _issue_uia( alice, uia.amount( 1000 ) );

      auto _restrict_xfer = [&]( bool xfer_flag )
      {
         asset_update_operation op;
         op.issuer = sam_id;
         op.asset_to_update = uia.id;
         op.new_options = uia.options;
         if( xfer_flag )
            op.new_options.flags |= transfer_restricted;
         else
            op.new_options.flags &= ~transfer_restricted;
         transaction tx;
         tx.operations.push_back( op );
         tx.set_expiration( db.head_block_time() + fc::minutes(5) );
         PUSH_TX( db, tx, database::skip_authority_check | database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

      BOOST_TEST_MESSAGE( "Enable transfer_restricted, send fails" );

      transfer_operation xfer_op;
      xfer_op.from = alice_id;
      xfer_op.to = bob_id;
      xfer_op.amount = uia.amount(100);
      signed_transaction xfer_tx;
      xfer_tx.operations.push_back( xfer_op );
      xfer_tx.set_expiration( db.head_block_time() + fc::minutes(5) );
      sign( xfer_tx, alice_private_key );

      _restrict_xfer( true );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, xfer_tx ), transfer_restricted_transfer_asset );

      BOOST_TEST_MESSAGE( "Disable transfer_restricted, send succeeds" );

      _restrict_xfer( false );
      PUSH_TX( db, xfer_tx );

      xfer_op.amount = uia.amount(101);

   }
   catch(fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_AUTO_TEST_SUITE_END()
