/*
 * Copyright (c) 2021 Abit More, and contributors.
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

#include "../common/database_fixture.hpp"

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsrm_tests, database_fixture )

/// Tests scenarios that unable to have BSDM-related asset issuer permission or extensions before hardfork
BOOST_AUTO_TEST_CASE( hardfork_protection_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask = ASSET_ISSUER_PERMISSION_MASK;

      uint16_t bitflag = VALID_FLAGS_MASK & ~committee_fed_asset;

      vector<operation> ops;

      // Testing asset_create_operation
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = bitflag;
      acop.common_options.issuer_permissions = old_bitmask;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 3;

      trx.operations.clear();
      trx.operations.push_back( acop );

      {
         auto& op = trx.operations.front().get<asset_create_operation>();

         // Unable to set new permission bit
         op.common_options.issuer_permissions = new_bitmask;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.common_options.issuer_permissions = old_bitmask;

         // Unable to set new extensions in bitasset options
         op.bitasset_opts->extensions.value.black_swan_response_method = 0;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.bitasset_opts->extensions.value.black_swan_response_method = {};

         acop = op;
      }

      // Able to create asset without new data
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& samcoin = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type samcoin_id = samcoin.id;

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 100 );
      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 3 );

      // Able to propose the good operation
      propose( acop );

      // Testing asset_update_operation
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = samcoin_id;
      auop.new_options = samcoin_id(db).options;

      trx.operations.clear();
      trx.operations.push_back( auop );

      {
         auto& op = trx.operations.front().get<asset_update_operation>();
         op.new_options.market_fee_percent = 200;

         // Unable to set new permission bit
         op.new_options.issuer_permissions = new_bitmask;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.issuer_permissions = old_bitmask;

         auop = op;
      }

      // Able to update asset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 200 );

      // Able to propose the good operation
      propose( auop );

      // Testing asset_update_bitasset_operation
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = samcoin_id;
      aubop.new_options = samcoin_id(db).bitasset_data(db).options;

      trx.operations.clear();
      trx.operations.push_back( aubop );

      {
         auto& op = trx.operations.front().get<asset_update_bitasset_operation>();
         op.new_options.minimum_feeds = 1;

         // Unable to set new extensions
         op.new_options.extensions.value.black_swan_response_method = 1;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.extensions.value.black_swan_response_method = {};

         aubop = op;
      }

      // Able to update bitasset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 1 );

      // Able to propose the good operation
      propose( aubop );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );

      // Check what we have now
      idump( (samcoin) );
      idump( (samcoin.bitasset_data(db)) );

      generate_block();

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Now able to propose the operations that was invalid
      for( const operation& op : ops )
         propose( op );

      generate_block();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests scenarios about setting non-UIA issuer permission bits on an UIA
BOOST_AUTO_TEST_CASE( uia_issuer_permissions_update_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask = ASSET_ISSUER_PERMISSION_MASK;
      uint16_t uiamask = UIA_ASSET_ISSUER_PERMISSION_MASK;

      uint16_t uiaflag = uiamask & ~disable_new_supply; // Allow creating new supply

      vector<operation> ops;

      asset_id_type samcoin_id = create_user_issued_asset( "SAMCOIN", sam_id(db), uiaflag ).id;

      // Testing asset_update_operation
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = samcoin_id;
      auop.new_options = samcoin_id(db).options;
      auop.new_options.issuer_permissions = old_bitmask & ~global_settle & ~disable_force_settle;

      trx.operations.clear();
      trx.operations.push_back( auop );

      // Able to update asset with non-UIA issuer permission bits
      PUSH_TX(db, trx, ~0);

      // Able to propose too
      propose( auop );

      // Issue some coin
      issue_uia( sam_id, asset( 1, samcoin_id ) );

      // Unable to unset the non-UIA "disable" issuer permission bits
      auto perms = samcoin_id(db).options.issuer_permissions;

      auop.new_options.issuer_permissions = perms & ~disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = perms & ~disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = perms & ~disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Still able to propose
      auop.new_options.issuer_permissions = new_bitmask;
      propose( auop );

      // But no longer able to update directly
      auop.new_options.issuer_permissions = uiamask | witness_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | committee_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unset the non-UIA bits in issuer permissions, should succeed
      auop.new_options.issuer_permissions = uiamask;
      trx.operations.clear();
      trx.operations.push_back( auop );

      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin_id(db).options.issuer_permissions, uiamask );

      // Burn all supply
      reserve_asset( sam_id, asset( 1, samcoin_id ) );

      BOOST_CHECK_EQUAL( samcoin_id(db).dynamic_asset_data_id(db).current_supply.value, 0 );

      // Still unable to set the non-UIA bits in issuer permissions
      auop.new_options.issuer_permissions = uiamask | witness_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | committee_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      generate_block();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests what kind of assets can have BSRM-related flags / issuer permissions / extensions
BOOST_AUTO_TEST_CASE( asset_permissions_flags_extensions_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      // Unable to create a PM with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_prediction_market( "TESTPM", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a MPA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_bitasset( "TESTBIT", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a UIA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_user_issued_asset( "TESTUIA", sam_id(db), disable_bsrm_update ), fc::exception );

      // create a PM with a zero market_fee_percent
      const asset_object& pm = create_prediction_market( "TESTPM", sam_id, 0, charge_market_fee );
      asset_id_type pm_id = pm.id;

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      // create a UIA with a zero market_fee_percent
      const asset_object& uia = create_user_issued_asset( "TESTUIA", sam_id(db), charge_market_fee );
      asset_id_type uia_id = uia.id;

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;

      // Unable to set disable_bsrm_update bit in flags for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in flags for MPA
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in flags for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in issuer_permissions for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // But able to propose
      propose( auop );

      // Unable to set disable_bsrm_update bit in issuer_permissions for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // But able to propose
      propose( auop );

      // Unable to create a UIA with disable_bsrm_update permission bit
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | disable_bsrm_update;

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Able to create UIA without disable_bsrm_update permission bit
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Unable to create a PM with disable_bsrm_update permission bit
      acop.symbol = "SAMPM";
      acop.precision = asset_id_type()(db).precision;
      acop.is_prediction_market = true;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle | disable_bsrm_update;
      acop.bitasset_opts = bitasset_options();

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Unable to create a PM with BSRM in extensions
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle;
      acop.bitasset_opts->extensions.value.black_swan_response_method = 0;

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Able to create PM with no disable_bsrm_update permission bit nor BSRM in extensions
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle;
      acop.bitasset_opts->extensions.value.black_swan_response_method.reset();
      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Unable to update PM to set BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = pm_id;
      aubop.new_options = pm_id(db).bitasset_data(db).options;
      aubop.new_options.extensions.value.black_swan_response_method = 1;

      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Able to propose
      propose( aubop );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether asset owner has permission to update bsrm
BOOST_AUTO_TEST_CASE( asset_owner_permissions_update_bsrm )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa_id(db).can_owner_update_bsrm() );

      BOOST_CHECK( !mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      using bsrm_type = bitasset_options::black_swan_response_type;
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::global_settlement );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;

      // disable owner's permission to update bsrm
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      // check that owner can not update bsrm
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;

      aubop.new_options.extensions.value.black_swan_response_method = 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      aubop.new_options.extensions.value.black_swan_response_method.reset();

      BOOST_CHECK( !mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      // enable owner's permission to update bsrm
      auop.new_options.issuer_permissions &= ~disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( mpa_id(db).can_owner_update_bsrm() );

      // check that owner can update bsrm
      aubop.new_options.extensions.value.black_swan_response_method = 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      BOOST_REQUIRE( mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      BOOST_CHECK_EQUAL( *mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method, 1u );
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::no_settlement );

      // check bsrm' valid range
      aubop.new_options.extensions.value.black_swan_response_method = 4;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      aubop.new_options.extensions.value.black_swan_response_method = 1;

      // Sam borrow some
      borrow( sam, asset(1000, mpa_id), asset(2000) );

      // disable owner's permission to update bsrm
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      // check that owner can not update bsrm
      aubop.new_options.extensions.value.black_swan_response_method = 0;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      aubop.new_options.extensions.value.black_swan_response_method.reset();
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      aubop.new_options.extensions.value.black_swan_response_method = 1;

      // able to update other params that still has permission E.G. force_settlement_delay_sec
      aubop.new_options.force_settlement_delay_sec += 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      BOOST_REQUIRE_EQUAL( mpa_id(db).bitasset_data(db).options.force_settlement_delay_sec,
                           aubop.new_options.force_settlement_delay_sec );

      BOOST_REQUIRE( mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      BOOST_CHECK_EQUAL( *mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method, 1u );

      // unable to enable the permission to update bsrm
      auop.new_options.issuer_permissions &= ~disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests margin calls when BSRM is no_settlement and call order is maker
BOOST_AUTO_TEST_CASE( no_settlement_maker_margin_call_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::no_settlement);

      // Create asset
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMMPA";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100; // 1%
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 1;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::no_settlement );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );

      // borrowers borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(1000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->id;

      const call_order_object* call2_ptr = borrow( borrower2, asset(1000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(10,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1250,mpa_id), asset(2000) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // borrower3 is unable to create debt position if its CR is below ICR which is calculated with median_feed
      // 1000 * (2000/1250) * 1.9 = 3040
      // 1000 * (22/10) * 1.9 = 4180
      BOOST_CHECK_THROW( borrow( borrower3, asset(1000, mpa_id), asset(4180) ), fc::exception );
      // borrower3 create debt position right above ICR
      const call_order_object* call3_ptr = borrow( borrower3, asset(1000, mpa_id), asset(4181) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // borrower is unable to adjust debt position if it's still undercollateralized
      // 1000 * (2000/1250) * 1.25 = 2000
      // 1000 * (22/10) * 1.25 = 2750
      BOOST_CHECK_THROW( borrow( borrower, asset(0, mpa_id), asset(749) ), fc::exception );
      // borrower adjust debt position to right at MSSR
      borrow( borrower, asset(0, mpa_id), asset(750) );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1250,mpa_id), asset(2100) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // Sam update MSSR and MCFR
      // note: borrower's position is undercollateralized again due to the mssr change
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;
      aubop.new_options.extensions.value.maximum_short_squeeze_ratio = 1300;
      aubop.new_options.extensions.value.margin_call_fee_ratio = 1;

      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK_EQUAL( mpa.bitasset_data(db).median_feed.maximum_short_squeeze_ratio, 1300u );
      BOOST_CHECK_EQUAL( mpa.bitasset_data(db).current_feed.maximum_short_squeeze_ratio, 1300u );
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1300,mpa_id), asset(2100) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // Transfer funds to sellers
      transfer( borrower, seller, asset(1000,mpa_id) );
      transfer( borrower2, seller, asset(1000,mpa_id) );
      transfer( borrower3, seller, asset(500,mpa_id) );
      transfer( borrower3, seller2, asset(500,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller2 sells some, due to MCFR, this order won't be filled in the beginning, but will be filled later
      const limit_order_object* sell_mid = create_sell_order( seller2, asset(100,mpa_id), asset(210) );
      BOOST_REQUIRE( sell_mid );
      limit_order_id_type sell_mid_id = sell_mid->id;
      BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );

      // seller2 sells more, this order won't be filled in the beginning either
      const limit_order_object* sell_high = create_sell_order( seller2, asset(100,mpa_id), asset(275) );
      BOOST_REQUIRE( sell_high );
      limit_order_id_type sell_high_id = sell_high->id;
      BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );

      // seller2 sells more, this order won't be filled
      const limit_order_object* sell_highest = create_sell_order( seller2, asset(100,mpa_id), asset(285) );
      BOOST_REQUIRE( sell_highest );
      limit_order_id_type sell_highest_id = sell_highest->id;
      BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 2500 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 ); // 500 - 100 - 100 - 100
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // seller sells some, this order will be filled
      const limit_order_object* sell_low = create_sell_order( seller, asset(111,mpa_id), asset(210) );
      BOOST_CHECK( !sell_low );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 2389 ); // 2500 - 111
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 232 ); // 111 * (210/100) * (1299/1300)
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price
                   == price( asset(11557,mpa_id), asset(18670) ) ); // 13:10 * (1000-111):(2100-111*210/100)
                                                                    // 13:10 * 889:1867
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 889 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1867 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller sells more
      sell_low = create_sell_order( seller, asset(1000,mpa_id), asset(100) );
      BOOST_CHECK( !sell_low );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 1389 ); // 2500 - 111 - 1000
      // 232 + round_up(889*(18670/11557)*(1299/1000)) + 111*(275/100)*(1299/1300)
      // 232 + 1866 + 305
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2403 );
      // now feed price is 13:10 * (1000-111):(2750-111*275/100)
      //                 = 13:10 * 889:2445 = 11557:24450
      // call order match price is 1300:1299 * 889:2445 = 0.363879089
      // sell_mid's price is 100/210 = 0.047619048

      // sell_mid got filled too
      BOOST_CHECK( !db.find( sell_mid_id ) );

      // sell_mid was selling 100 MPA for 210 CORE as maker, matched at its price
      // call pays round_down(210*1300/1299) = 210, fee = 0
      // now feed price is 13:10 * (889-100):(2445-210)
      //                 = 13:10 * 789:2235 = 10257:22350
      // call order match price is 1300:1299 * 789:2235 = 0.353291897
      // sell_high's price is 100/275 = 0.363636364

      // sell_high got filled too
      BOOST_CHECK( !db.find( sell_high_id ) );

      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
      // sell_mid was selling 100 MPA for 210 CORE as maker, matched at its price
      // sell_high was selling 100 MPA for 275 CORE as maker, matched at its price
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210 + 275
      // call pays round_down(275*1300/1299) = 275, fee = 0
      // now feed price is 13:10 * (789-100):(2235-275)
      //                 = 13:10 * 689:1960 = 8957:19600 (>10/22)
      // call order match price is 1300:1299 * 689:1960 = 0.351801229
      // sell_highest's price is 100/285 = 0.350877193

      // check
      BOOST_CHECK( mpa.bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price
                   == price( asset(8957,mpa_id), asset(19600) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 689 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 1960 );
      BOOST_CHECK( !db.find(call2_id) );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller sells more
      sell_low = create_sell_order( seller, asset(1000,mpa_id), asset(100) );
      BOOST_REQUIRE( sell_low );
      limit_order_id_type sell_low_id = sell_low->id;

      auto final_check = [&]
      {
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 311 );
         BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389 ); // 2500 - 111 - 1000 - 1000
         // 2403 + round_up(689*(19600/8957)*(1299/1000))
         // 2403 + 1959
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 4362 );

         BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
         BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // no change

         // check
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );

         BOOST_CHECK( !db.find(call_id) );
         BOOST_CHECK( !db.find(call2_id) );
         BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
         BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );
      };

      final_check();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      final_check();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests force settlements when BSRM is no_settlement and call order is maker
BOOST_AUTO_TEST_CASE( no_settlement_maker_force_settle_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::no_settlement);

      // Create asset
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMMPA";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100; // 1%
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 1;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::no_settlement );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );

      // borrowers borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(1000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->id;

      const call_order_object* call2_ptr = borrow( borrower2, asset(1000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // publish a new feed so that borrower's debt position is undercollateralized
      f.settlement_price = price( asset(10,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1250,mpa_id), asset(2000) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // borrower3 is unable to create debt position if its CR is below ICR which is calculated with median_feed
      // 1000 * (2000/1250) * 1.9 = 3040
      // 1000 * (22/10) * 1.9 = 4180
      BOOST_CHECK_THROW( borrow( borrower3, asset(1000, mpa_id), asset(4180) ), fc::exception );
      // borrower3 create debt position right above ICR
      const call_order_object* call3_ptr = borrow( borrower3, asset(1000, mpa_id), asset(4181) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // borrower is unable to adjust debt position if it's still undercollateralized
      // 1000 * (2000/1250) * 1.25 = 2000
      // 1000 * (22/10) * 1.25 = 2750
      BOOST_CHECK_THROW( borrow( borrower, asset(0, mpa_id), asset(749) ), fc::exception );
      // borrower adjust debt position to right at MSSR
      borrow( borrower, asset(0, mpa_id), asset(750) );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1250,mpa_id), asset(2100) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // Sam update MSSR and MCFR
      // note: borrower's position is undercollateralized again due to the mssr change
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;
      aubop.new_options.extensions.value.maximum_short_squeeze_ratio = 1300;
      aubop.new_options.extensions.value.margin_call_fee_ratio = 1;

      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      // check
      BOOST_CHECK_EQUAL( mpa.bitasset_data(db).median_feed.maximum_short_squeeze_ratio, 1300u );
      BOOST_CHECK_EQUAL( mpa.bitasset_data(db).current_feed.maximum_short_squeeze_ratio, 1300u );
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == price( asset(1300,mpa_id), asset(2100) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      // Transfer funds to sellers
      transfer( borrower, seller, asset(1000,mpa_id) );
      transfer( borrower2, seller, asset(1000,mpa_id) );
      transfer( borrower3, seller, asset(500,mpa_id) );
      transfer( borrower3, seller2, asset(500,mpa_id) );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller2 sells some, due to MCFR, this order won't be filled in the beginning, but will be filled later
      const limit_order_object* sell_mid = create_sell_order( seller2, asset(100,mpa_id), asset(210) );
      BOOST_REQUIRE( sell_mid );
      limit_order_id_type sell_mid_id = sell_mid->id;
      BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );

      // seller2 sells more, this order won't be filled in the beginning either
      const limit_order_object* sell_high = create_sell_order( seller2, asset(100,mpa_id), asset(275) );
      BOOST_REQUIRE( sell_high );
      limit_order_id_type sell_high_id = sell_high->id;
      BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );

      // seller2 sells more, this order won't be filled
      const limit_order_object* sell_highest = create_sell_order( seller2, asset(100,mpa_id), asset(285) );
      BOOST_REQUIRE( sell_highest );
      limit_order_id_type sell_highest_id = sell_highest->id;
      BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 2500 );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 ); // 500 - 100 - 100 - 100
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // seller settles some
      auto result = force_settle( seller, asset(111,mpa_id) );
      force_settlement_id_type settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_CHECK( !db.find(settle_id) );

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 2389 ); // 2500 - 111
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 232 ); // 111 * (210/100) * (1299/1300)
      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );

      // check
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price
                   == price( asset(11557,mpa_id), asset(18670) ) ); // 13:10 * (1000-111):(2100-111*210/100)
                                                                    // 13:10 * 889:1867
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 889 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1867 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller settles some more
      result = force_settle( seller, asset(1000,mpa_id) );
      settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
      BOOST_CHECK( !db.find(settle_id) );

      // call2 is filled by settle order
      BOOST_CHECK( !db.find(call2_id) );
      // now feed price is 13:10 * 1000:2750 = 26:55 (>10/22)
      // call order match price is 1300:1299 * 1000:2750 = 0.363916299
      // sell_mid's price is 100/210 = 0.047619048

      // then seller2's sell_mid got filled by call
      BOOST_CHECK( !db.find( sell_mid_id ) );

      // sell_mid was selling 100 MPA for 210 CORE as maker, matched at its price
      // call pays round_down(210*1300/1299) = 210, fee = 0
      // now feed price is 13:10 * (1000-100):(2750-210)
      //                 = 13:10 * 900:2540 = 11700:25400 (>10/22)
      // call order match price is 1300:1299 * 900:2540 = 0.32732629
      // sell_high's price is 100/275 = 0.363636364

      // then sell_high got filled by call
      BOOST_CHECK( !db.find( sell_high_id ) );

      BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
      // sell_mid was selling 100 MPA for 210 CORE as maker, matched at its price
      // sell_high was selling 100 MPA for 275 CORE as maker, matched at its price
      BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210 + 275
      // call pays round_down(275*1300/1299) = 275, fee = 0
      // now feed price is 13:10 * (1000-100-100):(2750-210-275)
      //                 = 13:10 * 800:2265 = 10400:22650 = 208:453 (>10/22)
      // call order match price is 1300:1299 * 800:2265 = 0.353472785
      // sell_highest's price is 100/285 = 0.350877193

      // then the settle order got filled by call
      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 1389 ); // 2500 - 111 - 1000
      // 232 + round_up(889*(18670/11557)*(1299/1000)) + 111*(22650/10400)*(1299/1000)
      // 232 + 1866 + 314
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2412 );
      // now feed price is 13:10 * (800-111):(2265-111*(22650/10400)*(13/10))
      //                 = 13:10 * 689:1951 = 8957:19510 (>10/22)
      // call order match price is 1300:1299 * 689:1951 = 0.353424094
      // sell_highest's price is 100/285 = 0.350877193

      // check
      BOOST_CHECK( mpa.bitasset_data(db).is_current_feed_price_capped() );
      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price
                   == price( asset(8957,mpa_id), asset(19510) ) );
      BOOST_CHECK( !mpa.bitasset_data(db).has_settlement() );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 689 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 1951 );
      BOOST_CHECK( !db.find(call2_id) );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // seller settles more
      result = force_settle( seller, asset(1000,mpa_id) );
      settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();

      auto final_check = [&]
      {
         BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 311 );

         BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), 389 ); // 2500 - 111 - 1000 - 1000
         // 2412 + round_up(689*(19510/8957)*(1299/1000))
         // 2412 + 1950
         BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 4362 );

         BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), 200 );
         BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // no change

         // check
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );

         BOOST_CHECK( !db.find(call_id) );
         BOOST_CHECK( !db.find(call2_id) );
         BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
         BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );
      };

      final_check();

      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      final_check();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests margin calls and force settlements when BSRM is no_settlement and call order is taker
BOOST_AUTO_TEST_CASE( no_settlement_taker_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // Several passes, with different limit orders and/or settle orders
   for( int i = 0; i <= 20; ++ i )
   {
      idump( (i) );

      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2)(borrower3)(seller)(seller2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );
      fund( borrower3, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::no_settlement);

      // Create asset
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMMPA";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100; // 1%
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 1;
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm_value;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11; // 1.1%

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::no_settlement );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      BOOST_CHECK( mpa.bitasset_data(db).median_feed.settlement_price == f.settlement_price );
      BOOST_CHECK( mpa.bitasset_data(db).current_feed.settlement_price == f.settlement_price );

      // borrowers borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(1000, mpa_id), asset(2750) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->id;

      const call_order_object* call2_ptr = borrow( borrower2, asset(1000, mpa_id), asset(2100) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->id;

      // 1000 * (22/10) * 1.9 = 4180
      const call_order_object* call3_ptr = borrow( borrower3, asset(1000, mpa_id), asset(4181) );
      BOOST_REQUIRE( call3_ptr );
      call_order_id_type call3_id = call3_ptr->id;

      // Transfer funds to sellers
      transfer( borrower, seller, asset(1000,mpa_id) );
      transfer( borrower2, seller, asset(1000,mpa_id) );
      transfer( borrower3, seller, asset(500,mpa_id) );
      transfer( borrower3, seller2, asset(500,mpa_id) );

      int64_t expected_seller_balance_mpa = 2500;
      int64_t expected_seller2_balance_mpa = 500;

      // seller2 sells some
      const limit_order_object* sell_highest = create_sell_order( seller2, asset(100,mpa_id), asset(285) );
      BOOST_REQUIRE( sell_highest );
      limit_order_id_type sell_highest_id = sell_highest->id;
      BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );
      expected_seller2_balance_mpa -= 100;

      // seller2 sells more
      const limit_order_object* sell_high = create_sell_order( seller2, asset(100,mpa_id), asset(275) );
      BOOST_REQUIRE( sell_high );
      limit_order_id_type sell_high_id = sell_high->id;
      BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
      expected_seller2_balance_mpa -= 100;

      // seller2 sells more, due to MCFR, this order won't be filled if no order is selling lower
      const limit_order_object* sell_mid = create_sell_order( seller2, asset(100,mpa_id), asset(210) );
      BOOST_REQUIRE( sell_mid );
      limit_order_id_type sell_mid_id = sell_mid->id;
      BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
      expected_seller2_balance_mpa -= 100;

      // seller sells
      const limit_order_object* sell_low = nullptr;
      limit_order_id_type sell_low_id;
      force_settlement_id_type settle_id;
      force_settlement_id_type settle2_id;
      if( 0 == i )
      {
         // Nothing to do here
      }
      else if( 1 == i )
      {
         sell_low = create_sell_order( seller, asset(111,mpa_id), asset(230) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 111 );
         expected_seller_balance_mpa -= 111;
      }
      else if( 2 == i )
      {
         sell_low = create_sell_order( seller, asset(111,mpa_id), asset(210) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 111 );
         expected_seller_balance_mpa -= 111;
      }
      else if( 3 == i )
      {
         sell_low = create_sell_order( seller, asset(900,mpa_id), asset(1870) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 900 );
         expected_seller_balance_mpa -= 900;
      }
      else if( 4 == i )
      {
         sell_low = create_sell_order( seller, asset(920,mpa_id), asset(1870) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 920 );
         expected_seller_balance_mpa -= 920;
      }
      else if( 5 == i )
      {
         sell_low = create_sell_order( seller, asset(1000,mpa_id), asset(1870) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 1000 );
         expected_seller_balance_mpa -= 1000;
      }
      else if( 6 == i )
      {
         sell_low = create_sell_order( seller, asset(1050,mpa_id), asset(1870) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 1050 );
         expected_seller_balance_mpa -= 1050;
      }
      else if( 7 == i )
      {
         sell_low = create_sell_order( seller, asset(1800,mpa_id), asset(1870*2) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 1800 );
         expected_seller_balance_mpa -= 1800;
      }
      else if( 8 == i )
      {
         sell_low = create_sell_order( seller, asset(2000,mpa_id), asset(1870) );
         BOOST_REQUIRE( sell_low );
         sell_low_id = sell_low->id;
         BOOST_CHECK_EQUAL( sell_low_id(db).for_sale.value, 2000 );
         expected_seller_balance_mpa -= 2000;
      }
      else if( 9 == i )
      {
         auto result = force_settle( seller, asset(111,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 111;
      }
      else if( 10 == i )
      {
         auto result = force_settle( seller, asset(990,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 990;
      }
      else if( 11 == i )
      {
         auto result = force_settle( seller, asset(995,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 995;
      }
      else if( 12 == i )
      {
         auto result = force_settle( seller, asset(1000,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 1000;
      }
      else if( 13 == i )
      {
         auto result = force_settle( seller, asset(1050,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 1050;
      }
      else if( 14 == i )
      {
         auto result = force_settle( seller, asset(1750,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 1750;
      }
      else if( 15 == i )
      {
         auto result = force_settle( seller, asset(1800,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 1800;
      }
      else if( 16 == i )
      {
         auto result = force_settle( seller, asset(2000,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         expected_seller_balance_mpa -= 2000;
      }
      else if( 17 == i )
      {
         auto result = force_settle( seller, asset(492,mpa_id) );
         settle_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle_id) );
         result = force_settle( seller, asset(503,mpa_id) );
         settle2_id = *result.get<extendable_operation_result>().value.new_objects->begin();
         BOOST_REQUIRE( db.find(settle2_id) );
         expected_seller_balance_mpa -= 995;
      }
      else
      {
         BOOST_TEST_MESSAGE( "No more test cases so far" );
         break;
      }

      BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), expected_seller_balance_mpa );
      BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );

      BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
      BOOST_CHECK_EQUAL( call2_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );
      BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
      BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

      // publish a new feed so that borrower's and borrower2's debt positions become undercollateralized
      f.settlement_price = price( asset(10,mpa_id), asset(22) );
      f.maximum_short_squeeze_ratio = 1300;
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check result
      auto check_result = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).has_settlement() );
         BOOST_CHECK_EQUAL( get_balance( seller_id, mpa_id ), expected_seller_balance_mpa );
         BOOST_CHECK_EQUAL( get_balance( seller2_id, mpa_id ), expected_seller2_balance_mpa );
         BOOST_CHECK_EQUAL( call3_id(db).debt.value, 1000 );
         BOOST_CHECK_EQUAL( call3_id(db).collateral.value, 4181 );

         //BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );

         if( 0 == i ) // no order is filled
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_mid price = 100:210 = 0.476190476
            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(13,mpa_id), asset(21) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 2100 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 0 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 1 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 111:230 = 0.482608696
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low receives 230
            // call2 pays round_down(230*1300/1289) = 231, margin call fee = 1
            // now feed price is 13:10 * (1000-111):(2100-231)
            //                 = 13:10 * 889:1869 = 11557:18690 = 0.61835206 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 889:1869 = 0.479714554
            // sell_mid price = 100:210 = 0.476190476
            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(11557,mpa_id), asset(18690) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 889 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1869 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 230 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 2 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 111:210 = 0.504545455
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low receives 210
            // call2 pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-111):(2100-211)
            //                 = 13:10 * 889:1889 = 11557:18890 = 0.611805188 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 889:1889 = 0.474635522
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid receives 210
            // call2 pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-111-100):(2100-211-211)
            //                 = 13:10 * 789:1678 = 10257:16780 = 0.611263409 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 789:1678 = 0.474215212
            // sell_high price = 100:275 = 0.363636364
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10257,mpa_id), asset(16780) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 789 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1678 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 210 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 210 );
         }
         else if( 3 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 900:1870 = 0.481283422
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low receives 1870
            // call2 pays round_down(1870*1300/1289) = 1885, margin call fee = 15
            // now feed price is 13:10 * (1000-900):(2100-1885)
            //                 = 13:10 * 100:215 = 130:215 = 0.604651163 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 100:215 = 0.469085464
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid receives 210
            // call2 pays round_up(210*1300/1289) = 212, margin call fee = 2
            // call2 is fully filled, freed collateral = 215 - 212 = 3
            BOOST_CHECK( !db.find( call2_id ) );
            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_high price = 100:275 = 0.363636364
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(130,mpa_id), asset(275) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 3 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 210 );
         }
         else if( 4 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 920:1870 = 0.49197861
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low receives 1870
            // call2 pays round_down(1870*1300/1289) = 1885, margin call fee = 15
            // now feed price is 13:10 * (1000-920):(2100-1885)
            //                 = 13:10 * 80:215 = 104:215 = 0.48372093 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 80:215 = 0.375268371
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is partially filled
            // sell_mid pays 80, receives 80 * 210/100 = 168
            // call2 pays round_up(80*(210/100)*(1300/1289)) = 170, margin call fee = 2
            // call2 is fully filled, freed collateral = 215-170 = 45
            BOOST_CHECK( !db.find( call2_id ) );
            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 20, receives 20 * 210/100 = 42
            // call pays round_down(20*(210/100)*(1300/1289)) = 42, margin call fee = 0
            // now feed price is 13:10 * (1000-20):(2750-42)
            //                 = 13:10 * 980:2708 = 1274:2708 = 0.470457903 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 980:2708 = 0.364978978
            // sell_high price = 100:275 = 0.363636364
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(1274,mpa_id), asset(2708) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 980 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2708 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 45 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 210 );
         }
         else if( 5 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 1000:1870 = 0.534759358
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low receives 1870
            // call2 pays round_up(1870*1300/1289) = 1886, margin call fee = 16
            // call2 is fully filled, freed collateral = 2100-1886 = 214
            BOOST_CHECK( !db.find( call2_id ) );
            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(1040,mpa_id), asset(2262) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 800 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2262 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 214 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 6 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 1050:1870 = 0.561497326
            // sell_low is partially filled
            // sell_low pays 1000, receives round_up(1000 * 1870/1050) = 1781
            // call2 pays round_up(1000*(1870/1050)*(1300/1289)) = 1797, margin call fee = 16
            // call2 is fully filled, freed collateral = 2100-1797 = 303
            BOOST_CHECK( !db.find( call2_id ) );

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_low price = 1050:1870 = 0.561497326
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low pays 50, receives round_down(50*1870/1050) = 89
            // call pays round_down(50*(1870/1050)*(1300/1289)) = 89, margin call fee = 0
            // now feed price is 13:10 * (1000-50):(2750-89)
            //                 = 13:10 * 950:2661 = 1235:2661 = 0.464111236 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 950:2661 = 0.360055265
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price from call is 13:10 * (1000-50-100):(2750-89-211)
            //                 = 13:10 * 850:2450 = 1105:2450 = 0.451020408 (< 10:22 = 0.454545455)
            // so feed price is 10:22
            // call match price is 1000:1289 * 10:22 = 0.352634177
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price from call is 13:10 * (1000-50-100-100):(2750-89-211-277)
            //                 = 13:10 * 750:2173 = 975:2173 = 0.448688449 (< 10:22 = 0.454545455)
            // so feed price is 10:22
            // call match price is 1000:1289 * 10:22 = 0.352634177
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10,mpa_id), asset(22) ) );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 750 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2173 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 303 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 7 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 0.480254165
            // sell_low price = 900:1870 = 0.481283422
            // sell_low is partially filled
            // sell_low pays 1000, receives round_up(1000 * 1870/900) = 2078
            // call2 pays round_up(1000*(1870/900)*(1300/1289)) = 2096, margin call fee = 18
            // call2 is fully filled, freed collateral = 2100-2096 = 4
            BOOST_CHECK( !db.find( call2_id ) );

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_low price = 900:1870 = 0.481283422
            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );
            // sell_low pays 800, receives round_down(800*1870/900) = 1662
            // call pays round_down(800*(1870/900)*(1300/1289)) = 1676, margin call fee = 14
            // now call's debt is 1000-800=200, collateral is 2750-1676=1074
            //     CR = 1074/200 / (22/10) = 2.44 > 1.85, out of margin call territory
            // so feed price is 10:22
            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10,mpa_id), asset(22) ) );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 200 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 1074 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 4 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870*2 ); // 2078+1662
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 8 == i )
         {
            // sell_low would match call2 and call

            // sell_low pays 1000, receives round_up(1000 * 1870/2000) = 935
            // call2 pays round_up(1000*(1870/2000)*(1300/1289)) = 943, margin call fee = 8
            // call2 is fully filled, freed collateral = 2100-943 = 1157
            BOOST_CHECK( !db.find( call2_id ) );

            // sell_low is fully filled
            BOOST_CHECK( !db.find( sell_low_id ) );

            // sell_low pays 1000, receives round_up(1000 * 1870/2000) = 935
            // call pays round_up(1000*(1870/2000)*(1300/1289)) = 943, margin call fee = 8
            // call is fully filled, freed collateral = 2750-943 = 1807
            BOOST_CHECK( !db.find( call_id ) );

            // feed price is 10:22

            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10,mpa_id), asset(22) ) );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 + 1157 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 + 1807 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 1870 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 9 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(111*27069/13000) = 231
            // call2 pays round_down(111*2100/1000) = 233, margin call fee = 2
            // now feed price is 13:10 * (1000-111):(2100-233)
            //                 = 13:10 * 889:1867 = 11557:18670 = 0.619014462 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 889:1867 = 0.480228442
            // sell_mid price = 100:210 = 0.476190476
            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(11557,mpa_id), asset(18670) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 889 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 1867 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 231 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 10 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(990*27069/13000) = 2061
            // call2 pays round_down(990*2100/1000) = 2079, margin call fee = 18
            // now feed price is 13:10 * (1000-990):(2100-2079)
            //                 = 13:10 * 10:21 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 10:21 = 0.480254165
            // sell_mid price = 100:210 = 0.476190476
            BOOST_REQUIRE( db.find( sell_mid_id ) );
            BOOST_REQUIRE( db.find( sell_high_id ) );
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_mid_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_high_id(db).for_sale.value, 100 );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(13,mpa_id), asset(21) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 1000 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2750 );
            BOOST_CHECK_EQUAL( call2_id(db).debt.value, 10 );
            BOOST_CHECK_EQUAL( call2_id(db).collateral.value, 21 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2061 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 0 );
         }
         else if( 11 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(995*27069/13000) = 2071
            // call2 pays round_down(995*2100/1000) = 2089, margin call fee = 18
            // now feed price is 13:10 * (1000-995):(2100-2089)
            //                 = 13:10 * 5:11 = 13:22 = 0.590909091 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 5:11 = 0.45842443
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is partially filled
            // sell_mid pays 5, receives round_up(5 * 21/10) = 11
            // call2 pays round_up(5*(21/10)*(1300/1289)) = 11, margin call fee = 0
            // call2 is fully filled, freed collateral = 11-11 = 0
            BOOST_CHECK( !db.find( call2_id ) );

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 95, receives round_down(95*210/100) = 199
            // call pays round_down(95*(210/100)*(1300/1289)) = 201, margin call fee = 2
            // now feed price from call is 13:10 * (1000-95):(2750-201)
            //                 = 13:10 * 905:2549 = 11765:25490 = 0.46155355 (> 10:22 = 0.454545455)
            // so feed price is 11765:25490
            // call match price is 1300:1289 * 905:2549 = 0.358071024
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price from call is 13:10 * (1000-95-100):(2750-201-277)
            //                 = 13:10 * 805:2272 = 10465:22720 = 0.460607394 (> 10:22 = 0.454545455)
            // so feed price is 10465:22720
            // call match price is 1300:1289 * 805:2272 = 0.357337001
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10465,mpa_id), asset(22720) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 805 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2272 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2071 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 11+199+275
         }
         else if( 12 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_up(1000*27069/13000) = 2083
            // call2 pays 2100, margin call fee = 17
            BOOST_CHECK( !db.find( call2_id ) );

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(1040,mpa_id), asset(2262) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 800 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2262 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2083 );
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 13 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // call2 is fully filled
            BOOST_CHECK( !db.find( call2_id ) );
            // settle order receives round_up(1000*27069/13000) = 2083
            // call2 pays 2100, margin call fee = 17

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call pays price = 1000:2750
            // call match price is 1300:1289 * 100:275 = 130000:354475 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 1040000:2915718 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193, does not match

            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(50*2915718/1040000) = 140
            // call pays round_down(50*2262/800) = 141, margin call fee = 1
            // now feed price is 13:10 * (800-50):(2262-141)
            //                 = 13:10 * 750:2121 = 975:2121 = 0.459688826 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 750:2121 = 0.35662438
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(975,mpa_id), asset(2121) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 750 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2121 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2223 ); // 2083 + 140
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 14 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // call2 is fully filled
            BOOST_CHECK( !db.find( call2_id ) );
            // settle order receives round_up(1000*27069/13000) = 2083
            // call2 pays 2100, margin call fee = 17

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call pays price = 1000:2750
            // call match price is 1300:1289 * 100:275 = 130000:354475 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 1040000:2915718 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193, does not match

            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(750*2915718/1040000) = 2102
            // call pays round_down(750*2262/800) = 2120, margin call fee = 18
            // now feed price is 13:10 * (800-750):(2262-2120)
            //                 = 13:10 * 50:142 = 65:142 = 0.457746479 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 50:142 = 0.355117517
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(65,mpa_id), asset(142) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 50 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 142 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 4185 ); // 2083 + 2102
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 15 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // call2 is fully filled
            BOOST_CHECK( !db.find( call2_id ) );
            // settle order receives round_up(1000*27069/13000) = 2083
            // call2 pays 2100, margin call fee = 17

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call pays price = 1000:2750
            // call match price is 1300:1289 * 100:275 = 130000:354475 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 1040000:2915718 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193, does not match

            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_up(800*2915718/1040000) = 2243
            // call pays 2262, margin call fee = 19
            // call is fully filled
            BOOST_CHECK( !db.find( call_id ) );
            // now feed price is 10:22, no margin call

            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10,mpa_id), asset(22) ) );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 4326 ); // 2083 + 2243
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 16 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // call2 is fully filled
            BOOST_CHECK( !db.find( call2_id ) );
            // settle order receives round_up(1000*27069/13000) = 2083
            // call2 pays 2100, margin call fee = 17

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call pays price = 1000:2750
            // call match price is 1300:1289 * 100:275 = 130000:354475 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 100, receives 210
            // call pays round_down(210*1300/1289) = 211, margin call fee = 1
            // now feed price is 13:10 * (1000-100):(2750-211)
            //                 = 13:10 * 900:2539 = 1170:2539 = 0.460811343 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 900:2539 = 0.357495223
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price is 13:10 * (1000-100-100):(2750-211-277)
            //                 = 13:10 * 800:2262 = 1040:2262 = 0.459770115 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 800:2262 = 1040000:2915718 = 0.356687444
            // sell_highest price = 100:285 = 0.350877193, does not match

            // call is fully filled
            BOOST_CHECK( !db.find( call_id ) );
            // settle order receives round_up(800*2915718/1040000) = 2243
            // call pays 2262, margin call fee = 19
            BOOST_CHECK_EQUAL( settle_id(db).balance.amount.value, 200 );

            // now feed price is 10:22, no margin call

            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10,mpa_id), asset(22) ) );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 4326 ); // 2083 + 2243
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 210+275
         }
         else if( 17 == i )
         {
            // now feed price is 13:10 * 1000:2100 = 13:21 = 0.619047619 (> 10:22 = 0.454545455)
            // call2 pays price = 1000:2100
            //       match price = 1000:2100 * 1300:1289 = 13000:27069 = 0.480254165
            // settle order is fully filled
            BOOST_CHECK( !db.find( settle_id ) );
            // settle order receives round_down(492*27069/13000) = 1024
            // call2 pays round_down(492*2100/1000) = 1033, margin call fee = 9
            // now feed price is 13:10 * (1000-492):(2100-1033)
            //                 = 13:10 * 508:1067 = 6604:10670 = 0.618931584 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 508:1067 = 660400:1375363 = 0.480164146
            // sell_mid price = 100:210 = 0.476190476 does not match
            // settle2 is fully filled
            BOOST_CHECK( !db.find( settle2_id ) );
            // settle2 receives round_down(503*(1375363/660400)) = 1047
            // call2 pays round_down(503*1067/508) = 1056, margin call fee = 9
            // now feed price is 13:10 * (508-503):(1067-1056)
            //                 = 13:10 * 5:11 = 13:22 = 0.590909091 (> 10:22 = 0.454545455)
            // call2 match price is 1300:1289 * 5:11 = 0.45842443
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is partially filled
            // sell_mid pays 5, receives round_up(5 * 21/10) = 11
            // call2 pays round_up(5*(21/10)*(1300/1289)) = 11, margin call fee = 0
            // call2 is fully filled, freed collateral = 11-11 = 0
            BOOST_CHECK( !db.find( call2_id ) );

            // now feed price is 13:10 * 1000:2750 = 130:275 = 0.472727273 (> 10:22 = 0.454545455)
            // call match price is 1300:1289 * 100:275 = 0.366739544
            // sell_mid price = 100:210 = 0.476190476
            // sell_mid is fully filled
            BOOST_CHECK( !db.find( sell_mid_id ) );
            // sell_mid pays 95, receives round_down(95*210/100) = 199
            // call pays round_down(95*(210/100)*(1300/1289)) = 201, margin call fee = 2
            // now feed price from call is 13:10 * (1000-95):(2750-201)
            //                 = 13:10 * 905:2549 = 11765:25490 = 0.46155355 (> 10:22 = 0.454545455)
            // so feed price is 11765:25490
            // call match price is 1300:1289 * 905:2549 = 0.358071024
            // sell_high price = 100:275 = 0.363636364
            // sell_high is fully filled
            BOOST_CHECK( !db.find( sell_high_id ) );
            // sell_high pays 100, receives 275
            // call pays round_down(275*1300/1289) = 277, margin call fee = 2
            // now feed price from call is 13:10 * (1000-95-100):(2750-201-277)
            //                 = 13:10 * 805:2272 = 10465:22720 = 0.460607394 (> 10:22 = 0.454545455)
            // so feed price is 10465:22720
            // call match price is 1300:1289 * 805:2272 = 0.357337001
            // sell_highest price = 100:285 = 0.350877193
            BOOST_REQUIRE( db.find( sell_highest_id ) );
            BOOST_CHECK_EQUAL( sell_highest_id(db).for_sale.value, 100 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(10465,mpa_id), asset(22720) ) );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );

            BOOST_CHECK_EQUAL( call_id(db).debt.value, 805 );
            BOOST_CHECK_EQUAL( call_id(db).collateral.value, 2272 );

            BOOST_CHECK_EQUAL( get_balance( borrower2_id, asset_id_type() ), init_amount - 2100 );
            BOOST_CHECK_EQUAL( get_balance( borrower_id, asset_id_type() ), init_amount - 2750 );

            BOOST_CHECK_EQUAL( get_balance( seller_id, asset_id_type() ), 2071 ); // 1024+1047
            BOOST_CHECK_EQUAL( get_balance( seller2_id, asset_id_type() ), 485 ); // 11+199+275
         }
         else
         {
            BOOST_FAIL( "to be fixed" );
         }
      };

      check_result();

      // generate a block
      BOOST_TEST_MESSAGE( "Generate a block" );
      generate_block();

      // check again
      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_CAPTURE_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
