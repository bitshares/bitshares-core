/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>

#include <fc/crypto/digest.hpp>

#include <locale>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

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
      creator.common_options.issuer_permissions = charge_market_fee|white_list|override_authority|transfer_restricted|disable_confidential;
      creator.common_options.flags = charge_market_fee|white_list|override_authority|disable_confidential;
      creator.common_options.core_exchange_rate = price(asset(2),asset(1,asset_id_type(1)));
      creator.common_options.whitelist_authorities = creator.common_options.blacklist_authorities = {account_id_type()};
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "ADVANCED");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK(test_asset.options.flags & white_list);
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
   BOOST_TEST_MESSAGE( "Issuing 1000 ADVANCED to dan" );
   issue_uia( dan, advanced.amount( 1000 ) );
   BOOST_TEST_MESSAGE( "Checking dan's balance" );
   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 1000 );

   override_transfer_operation otrans;
   otrans.issuer = advanced.issuer;
   otrans.from = dan.id;
   otrans.to   = eric.id;
   otrans.amount = advanced.amount(100);
   trx.operations.push_back(otrans);

   BOOST_TEST_MESSAGE( "Require throwing without signature" );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), tx_missing_active_auth );
   BOOST_TEST_MESSAGE( "Require throwing with dan's signature" );
   sign( trx,  dan_private_key  );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), tx_missing_active_auth );
   BOOST_TEST_MESSAGE( "Pass with issuer's signature" );
   trx.clear_signatures();
   sign( trx,  sam_private_key  );
   PUSH_TX( db, trx, 0 );

   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 900 );
   BOOST_REQUIRE_EQUAL( get_balance( eric, advanced ), 100 );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( override_transfer_test2 )
{ try {
   ACTORS( (dan)(eric)(sam) );
   const asset_object& advanced = create_user_issued_asset( "ADVANCED", sam, 0 );
   issue_uia( dan, advanced.amount( 1000 ) );
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
   sign( trx,  dan_private_key  );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), fc::exception);
   BOOST_TEST_MESSAGE( "Fail because overide_authority flag is not set" );
   trx.clear_signatures();
   sign( trx,  sam_private_key  );
   GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, 0 ), fc::exception );

   BOOST_REQUIRE_EQUAL( get_balance( dan, advanced ), 1000 );
   BOOST_REQUIRE_EQUAL( get_balance( eric, advanced ), 0 );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( issue_whitelist_uia )
{
   try {
      account_id_type izzy_id = create_account("izzy").id;
      const asset_id_type uia_id = create_user_issued_asset(
         "ADVANCED", izzy_id(db), white_list ).id;
      account_id_type nathan_id = create_account("nathan").id;
      account_id_type vikram_id = create_account("vikram").id;
      trx.clear();

      asset_issue_operation op;
      op.issuer = uia_id(db).issuer;
      op.asset_to_issue = asset(1000, uia_id);
      op.issue_to_account = nathan_id;
      trx.operations.emplace_back(op);
      set_expiration( db, trx );
      //Fail because nathan is not whitelisted, but only before hardfork time
      if( db.head_block_time() <= HARDFORK_415_TIME )
      {
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
         generate_blocks( HARDFORK_415_TIME );
         generate_block();
         set_expiration( db, trx );
      }
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK(is_authorized_asset( db, nathan_id(db), uia_id(db) ));
      BOOST_CHECK_EQUAL(get_balance(nathan_id, uia_id), 1000);

      // Make a whitelist, now it should fail
      {
         BOOST_TEST_MESSAGE( "Changing the whitelist authority" );
         asset_update_operation uop;
         uop.issuer = izzy_id;
         uop.asset_to_update = uia_id;
         uop.new_options = uia_id(db).options;
         uop.new_options.whitelist_authorities.insert(izzy_id);
         trx.operations.back() = uop;
         PUSH_TX( db, trx, ~0 );
         BOOST_CHECK( uia_id(db).options.whitelist_authorities.find(izzy_id) != uia_id(db).options.whitelist_authorities.end() );
      }

      // Fail because there is a whitelist authority and I'm not whitelisted
      trx.operations.back() = op;
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );

      account_whitelist_operation wop;
      wop.authorizing_account = izzy_id;
      wop.account_to_list = vikram_id;
      wop.new_listing = account_whitelist_operation::white_listed;

      trx.operations.back() = wop;
      // Fail because whitelist function is restricted to members only
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );
      upgrade_to_lifetime_member( izzy_id );
      trx.operations.clear();
      trx.operations.push_back( wop );
      PUSH_TX( db, trx, ~0 );

      // Still fail after an irrelevant account was added
      trx.operations.back() = op;
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, trx, ~0 ), fc::exception );

      wop.account_to_list = nathan_id;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );
      trx.operations.back() = op;
      BOOST_CHECK_EQUAL(get_balance(nathan_id, uia_id), 1000);
      // Finally succeed when we were whitelisted
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan_id, uia_id), 2000);

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
      account_id_type izzy_id = get_account("izzy").id;
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
      wop.authorizing_account = izzy_id;
      wop.account_to_list = dan.id;
      wop.new_listing = account_whitelist_operation::white_listed;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );
      BOOST_TEST_MESSAGE( "Attempting to transfer from nathan to dan after whitelisting dan, should succeed" );
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan, advanced), 1900);
      BOOST_CHECK_EQUAL(get_balance(dan, advanced), 100);

      BOOST_TEST_MESSAGE( "Attempting to blacklist nathan" );
      {
         BOOST_TEST_MESSAGE( "Changing the blacklist authority" );
         asset_update_operation uop;
         uop.issuer = izzy_id;
         uop.asset_to_update = advanced.id;
         uop.new_options = advanced.options;
         uop.new_options.blacklist_authorities.insert(izzy_id);
         trx.operations.back() = uop;
         PUSH_TX( db, trx, ~0 );
         BOOST_CHECK( advanced.options.blacklist_authorities.find(izzy_id) != advanced.options.blacklist_authorities.end() );
      }

      wop.new_listing |= account_whitelist_operation::black_listed;
      wop.account_to_list = nathan.id;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK( !(is_authorized_asset( db, nathan, advanced )) );

      BOOST_TEST_MESSAGE( "Attempting to transfer from nathan after blacklisting, should fail" );
      op.amount = advanced.amount(50);
      trx.operations.back() = op;
      //Fail because nathan is blacklisted
      if( db.head_block_time() <= HARDFORK_419_TIME )
      {
         // before the hardfork time, it fails because the whitelist check fails
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), transfer_from_account_not_whitelisted );
      }
      else
      {
         // after the hardfork time, it fails because the fees are not in a whitelisted asset
         GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception );
      }

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
         op.issuer = izzy_id;
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
      BOOST_CHECK_EQUAL(get_balance(nathan, advanced), 1950);
      BOOST_CHECK_EQUAL(get_balance(dan, advanced), 50);

      BOOST_TEST_MESSAGE( "Blacklisting nathan by dan" );
      wop.authorizing_account = dan.id;
      wop.account_to_list = nathan.id;
      wop.new_listing = account_whitelist_operation::black_listed;
      trx.operations.back() = wop;
      PUSH_TX( db, trx, ~0 );

      trx.operations.back() = op;
      //Fail because nathan is blacklisted
      BOOST_CHECK(!is_authorized_asset( db, nathan, advanced ));
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      //Remove nathan from committee's whitelist, add him to dan's. This should not authorize him to hold ADVANCED.
      wop.authorizing_account = izzy_id;
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
      BOOST_CHECK(!is_authorized_asset( db, nathan, advanced ));
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
         set_expiration( db, tx );
         PUSH_TX( db, tx, database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

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
         set_expiration( db, tx );
         PUSH_TX( db, tx, database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

      BOOST_TEST_MESSAGE( "Enable transfer_restricted, send fails" );

      transfer_operation xfer_op;
      xfer_op.from = alice_id;
      xfer_op.to = bob_id;
      xfer_op.amount = uia.amount(100);
      signed_transaction xfer_tx;
      xfer_tx.operations.push_back( xfer_op );
      set_expiration( db, xfer_tx );
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

/***
 * Test to see if a asset name is valid
 * @param db the database
 * @param acct the account that will attempt to create the asset
 * @param asset_name the asset_name
 * @param allowed whether the creation should be successful
 * @returns true if meets expectations
 */
bool test_asset_name(graphene::chain::database_fixture* db, const graphene::chain::account_object& acct, std::string asset_name, bool allowed)
{
   if (allowed)
   {
      try
      {
         db->create_user_issued_asset(asset_name, acct, 0);
      } catch (...)
      {
         return false;
      }
   }
   else
   {
      try
      {
         db->create_user_issued_asset(asset_name, acct, 0);
         return false;
      } catch (fc::exception& ex) 
      {
         return true;
      } catch (...)
      {
         return false;
      }
   }
   return true;
}

/***
 * Test to see if an ascii character can be used in an asset name
 * @param c the ascii character (NOTE: includes extended ascii up to 255)
 * @param allowed_beginning true if it should be allowed as the first character of an asset name
 * @param allowed_middle true if it should be allowed in the middle of an asset name
 * @param allowed_end true if it should be allowed at the end of an asset name
 * @returns true if tests met expectations
 */
bool test_asset_char(graphene::chain::database_fixture* db, const graphene::chain::account_object& acct, const unsigned char& c, bool allowed_beginning, bool allowed_middle, bool allowed_end)
{
   std::ostringstream asset_name;
   // beginning
   asset_name << c << "CHARLIE";
   if (!test_asset_name(db, acct, asset_name.str(), allowed_beginning))
      return false;

   // middle
   asset_name.str("");
   asset_name.clear();
   asset_name << "CHAR" << c << "LIE";
   if (!test_asset_name(db, acct, asset_name.str(), allowed_middle))
      return false;

   // end
   asset_name.str("");
   asset_name.clear();
   asset_name << "CHARLIE" << c;
   return test_asset_name(db, acct, asset_name.str(), allowed_end);
}

BOOST_AUTO_TEST_CASE( asset_name_test )
{
   try
   {
      ACTORS( (alice)(bob)(sam) );

      auto has_asset = [&]( std::string symbol ) -> bool
      {
         const auto& assets_by_symbol = db.get_index_type<asset_index>().indices().get<by_symbol>();
         return assets_by_symbol.find( symbol ) != assets_by_symbol.end();
      };

      // Alice creates asset "ALPHA"
      BOOST_CHECK( !has_asset("ALPHA") );    BOOST_CHECK( !has_asset("ALPHA.ONE") );
      create_user_issued_asset( "ALPHA", alice_id(db), 0 );
      BOOST_CHECK(  has_asset("ALPHA") );    BOOST_CHECK( !has_asset("ALPHA.ONE") );

      // Nobody can create another asset named ALPHA
      GRAPHENE_REQUIRE_THROW( create_user_issued_asset( "ALPHA",   bob_id(db), 0 ), fc::exception );
      BOOST_CHECK(  has_asset("ALPHA") );    BOOST_CHECK( !has_asset("ALPHA.ONE") );
      GRAPHENE_REQUIRE_THROW( create_user_issued_asset( "ALPHA", alice_id(db), 0 ), fc::exception );
      BOOST_CHECK(  has_asset("ALPHA") );    BOOST_CHECK( !has_asset("ALPHA.ONE") );

      generate_blocks( HARDFORK_385_TIME );
      generate_block();

      // Bob can't create ALPHA.ONE
      GRAPHENE_REQUIRE_THROW( create_user_issued_asset( "ALPHA.ONE", bob_id(db), 0 ), fc::exception );
      BOOST_CHECK(  has_asset("ALPHA") );    BOOST_CHECK( !has_asset("ALPHA.ONE") );

      // Alice can create ALPHA.ONE
      create_user_issued_asset( "ALPHA.ONE", alice_id(db), 0 );
      BOOST_CHECK(  has_asset("ALPHA") );    BOOST_CHECK( has_asset("ALPHA.ONE") );

      // Sam tries to create asset ending in a number but fails before hf_620
      GRAPHENE_REQUIRE_THROW( create_user_issued_asset( "SP500", sam_id(db), 0 ), fc::assert_exception );
      BOOST_CHECK(  !has_asset("SP500") );

      // create a proposal to create asset ending in a number, this will fail before hf_620
      auto& core = asset_id_type()(db);
      asset_create_operation op_p;
      op_p.issuer = alice_id;
      op_p.symbol = "SP500";
      op_p.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
      op_p.fee = core.amount(0);

      const auto& curfees = *db.get_global_properties().parameters.current_fees;
      const auto& proposal_create_fees = curfees.get<proposal_create_operation>();
      proposal_create_operation prop;
      prop.fee_paying_account = alice_id;
      prop.proposed_ops.emplace_back( op_p );
      prop.expiration_time =  db.head_block_time() + fc::days(1);
      prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );

      signed_transaction tx;
      tx.operations.push_back( prop );
      db.current_fee_schedule().set_fee( tx.operations.back() );
      set_expiration( db, tx );
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, tx ), fc::assert_exception);

      generate_blocks( HARDFORK_CORE_620_TIME + 1);
      generate_block();

      // Sam can create asset ending in number after hf_620
      create_user_issued_asset( "NIKKEI225", sam_id(db), 0 );
      BOOST_CHECK(  has_asset("NIKKEI225") );

      // make sure other assets can still be created after hf_620
      create_user_issued_asset( "ALPHA2", alice_id(db), 0 );
      create_user_issued_asset( "ALPHA2.ONE", alice_id(db), 0 );
      BOOST_CHECK(  has_asset("ALPHA2") );
      BOOST_CHECK( has_asset("ALPHA2.ONE") );

      // proposal to create asset ending in number will now be created successfully as we are in > hf_620 time
      prop.expiration_time =  db.head_block_time() + fc::days(3);
      signed_transaction tx_hf620;
      tx_hf620.operations.push_back( prop );
      db.current_fee_schedule().set_fee( tx_hf620.operations.back() );
      set_expiration( db, tx_hf620 );
      sign( tx_hf620, alice_private_key );
      PUSH_TX( db, tx_hf620 );

      // assets with invalid characters should not be allowed
      unsigned char c = 0;
      do
      {
         if ( (c >= 48 && c <= 57) ) // numbers
            BOOST_CHECK_MESSAGE( test_asset_char(this, alice_id(db), c, false, true, true), "Failed on good ASCII value " + std::to_string(c) );
         else if ( c >= 65 && c <= 90) // letters
            BOOST_CHECK_MESSAGE( test_asset_char(this, alice_id(db), c, true, true, true), "Failed on good ASCII value " + std::to_string(c) );
         else                       // everything else
            BOOST_CHECK_MESSAGE( test_asset_char(this, alice_id(db), c, false, false, false), "Failed on bad ASCII value " + std::to_string(c) );
         c++;
      } while (c != 0);
   }
   catch(fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
