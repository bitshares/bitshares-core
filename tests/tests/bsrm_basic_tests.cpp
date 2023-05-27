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
BOOST_AUTO_TEST_CASE( bsrm_hardfork_protection_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Note: tests hf core-2281 too, assumes hf core-2281 and core-2267 occur at the same time
      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update & ~disable_collateral_bidding;
      uint16_t new_bitmask1 = ASSET_ISSUER_PERMISSION_MASK;
      uint16_t new_bitmask2 = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask3 = ASSET_ISSUER_PERMISSION_MASK & ~disable_collateral_bidding;

      uint16_t old_bitflag = VALID_FLAGS_MASK & ~committee_fed_asset & ~disable_collateral_bidding;

      vector<operation> ops;

      // Testing asset_create_operation
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = old_bitflag;
      acop.common_options.issuer_permissions = old_bitmask;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 3;

      trx.operations.clear();
      trx.operations.push_back( acop );

      {
         auto& op = trx.operations.front().get<asset_create_operation>();

         // Unable to set new permission bit
         op.common_options.issuer_permissions = new_bitmask1;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.common_options.issuer_permissions = new_bitmask2;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.common_options.issuer_permissions = new_bitmask3;
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
      asset_id_type samcoin_id = samcoin.get_id();

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
         op.new_options.issuer_permissions = new_bitmask1;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.issuer_permissions = new_bitmask2;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.issuer_permissions = new_bitmask3;
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
      // Note: tests hf core-2281 too, assumes hf core-2281 and core-2267 occur at the same time
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

      // Note: tests hf core-2281 too, assumes hf core-2281 and core-2267 occur at the same time
      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update & ~disable_collateral_bidding;
      uint16_t new_bitmask1 = ASSET_ISSUER_PERMISSION_MASK;
      uint16_t new_bitmask2 = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask3 = ASSET_ISSUER_PERMISSION_MASK & ~disable_collateral_bidding;
      uint16_t uiamask = UIA_ASSET_ISSUER_PERMISSION_MASK;

      uint16_t uiaflag = uiamask & ~disable_new_supply; // Allow creating new supply

      vector<operation> ops;

      asset_id_type samcoin_id = create_user_issued_asset( "SAMCOIN", sam_id(db), uiaflag ).get_id();

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
      // Note: tests hf core-2281 too, assumes hf core-2281 and core-2267 occur at the same time
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Still able to propose
      auop.new_options.issuer_permissions = new_bitmask1;
      propose( auop );
      auop.new_options.issuer_permissions = new_bitmask2;
      propose( auop );
      auop.new_options.issuer_permissions = new_bitmask3;
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

      auop.new_options.issuer_permissions = uiamask | disable_collateral_bidding;
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

      auop.new_options.issuer_permissions = uiamask | disable_collateral_bidding;
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
BOOST_AUTO_TEST_CASE( bsrm_asset_permissions_flags_extensions_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      // Unable to create a PM with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_prediction_market( "TESTPM", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a MPA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_bitasset( "TESTBIT", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a UIA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_user_issued_asset( "TESTUIA", sam_id(db), disable_bsrm_update ), fc::exception );

      // create a PM with a zero market_fee_percent
      const asset_object& pm = create_prediction_market( "TESTPM", sam_id, 0, charge_market_fee );
      asset_id_type pm_id = pm.get_id();

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.get_id();

      // create a UIA with a zero market_fee_percent
      const asset_object& uia = create_user_issued_asset( "TESTUIA", sam_id(db), charge_market_fee );
      asset_id_type uia_id = uia.get_id();

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
BOOST_AUTO_TEST_CASE( bsrm_asset_owner_permissions_update_bsrm )
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
      asset_id_type mpa_id = mpa.get_id();

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

/// Tests closing debt position when there is no sufficient price feeds
BOOST_AUTO_TEST_CASE( close_debt_position_when_no_feed )
{
   try {

      // Advance to a time before core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );

      // Create asset
      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.get_id();

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // update price feed publisher list so that there is no valid feed
      update_feed_producers( mpa_id, { sam_id } );

      // no sufficient price feeds
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

      // Unable to close debt position
      BOOST_CHECK_THROW( cover( borrower, asset(100000, mpa_id), asset(2000) ), fc::exception );
      BOOST_CHECK( db.find( call_id ) );

      // Go beyond the hard fork time
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Still no sufficient price feeds
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );

      // The debt position is there
      BOOST_CHECK( db.find( call_id ) );

      // Able to close debt position
      cover( borrower_id(db), asset(100000, mpa_id), asset(2000) );
      BOOST_CHECK( !db.find( call_id ) );

      ilog( "Generate a block" );
      generate_block();

      // final check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price.is_null() );
      BOOST_CHECK( !db.find( call_id ) );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether it is able to update BSRM after GS
BOOST_AUTO_TEST_CASE( update_bsrm_after_gs )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );

      // Create asset
      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.get_id();

      using bsrm_type = bitasset_options::black_swan_response_type;

      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == bsrm_type::global_settlement );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();

      // publish a new feed so that borrower's debt position is undercollateralized
      ilog( "Publish a new feed to trigger GS" );
      f.settlement_price = price( asset(1000,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == bsrm_type::global_settlement );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
      BOOST_CHECK( !db.find( call_id ) );

      // Sam tries to update BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;

      for( uint16_t i = 1; i <= 3; ++i )
      {
         idump( (i) );
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      }

      // recheck
      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == bsrm_type::global_settlement );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // publish a new feed to revive the MPA
      ilog( "Publish a new feed to revive MPA" );
      f.settlement_price = price( asset(1000,mpa_id), asset(3) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check - revived
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // Sam tries to update BSRM
      for( uint8_t i = 1; i <= 3; ++i )
      {
         idump( (i) );
         aubop.new_options = mpa_id(db).bitasset_data(db).options;
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         PUSH_TX(db, trx, ~0);
         BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(i) );

         if( i != 3 )
         {
            aubop.new_options.extensions.value.black_swan_response_method = 0;
            trx.operations.clear();
            trx.operations.push_back( aubop );
            PUSH_TX(db, trx, ~0);
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                            == bsrm_type::global_settlement );
         }
      }

      ilog( "Generate a block" );
      generate_block();

      // final check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(3) );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether it is able to update BSRM after individual settlement to fund
BOOST_AUTO_TEST_CASE( update_bsrm_after_individual_settlement_to_fund )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_fund);

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
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_fund );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(8000) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // publish a new feed so that borrower's debt position is undercollateralized
      ilog( "Publish a new feed to trigger settlement" );
      f.settlement_price = price( asset(1000,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK( db.find( call2_id ) );

      // Sam tries to update BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;

      for( uint16_t i = 0; i <= 3; ++i )
      {
         if( static_cast<bsrm_type>(i) == bsrm_type::individual_settlement_to_fund )
            continue;
         idump( (i) );
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      }

      // recheck
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_fund );
      BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // Settle debt
      ilog( "Settle" );
      force_settle( borrower2, asset(100000,mpa_id) );

      // recheck
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // Sam tries to update BSRM
      for( uint8_t i = 0; i <= 3; ++i )
      {
         if( static_cast<bsrm_type>(i) == bsrm_type::individual_settlement_to_fund )
            continue;
         idump( (i) );
         aubop.new_options = mpa_id(db).bitasset_data(db).options;
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         PUSH_TX(db, trx, ~0);
         BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(i) );
         if( i != 3 )
         {
            aubop.new_options.extensions.value.black_swan_response_method = bsrm_value;
            trx.operations.clear();
            trx.operations.push_back( aubop );
            PUSH_TX(db, trx, ~0);
            BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_fund );
         }
      }

      ilog( "Generate a block" );
      generate_block();

      // final check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(3) );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether it is able to update BSRM after individual settlement to order
BOOST_AUTO_TEST_CASE( update_bsrm_after_individual_settlement_to_order )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder)(borrower)(borrower2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );

      using bsrm_type = bitasset_options::black_swan_response_type;
      uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::individual_settlement_to_order);

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
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_order );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(8000) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // publish a new feed so that borrower's debt position is undercollateralized
      ilog( "Publish a new feed to trigger settlement" );
      f.settlement_price = price( asset(1000,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( db.find_settled_debt_order(mpa_id) );
      BOOST_CHECK( !db.find( call_id ) );
      BOOST_CHECK( db.find( call2_id ) );

      // Sam tries to update BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;

      for( uint16_t i = 0; i <= 3; ++i )
      {
         if( static_cast<bsrm_type>(i) == bsrm_type::individual_settlement_to_order )
            continue;
         idump( (i) );
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      }

      // recheck
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::individual_settlement_to_order );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( db.find_settled_debt_order(mpa_id) );

      // Fill the individual settlement order
      ilog( "Buy into the individual settlement order" );
      const limit_order_object* sell_ptr = create_sell_order( borrower2, asset(100000,mpa_id), asset(1) );
      BOOST_CHECK( !sell_ptr );

      // recheck
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // Sam tries to update BSRM
      for( uint8_t i = 0; i <= 3; ++i )
      {
         if( static_cast<bsrm_type>(i) == bsrm_type::individual_settlement_to_order )
            continue;
         idump( (i) );
         aubop.new_options = mpa_id(db).bitasset_data(db).options;
         aubop.new_options.extensions.value.black_swan_response_method = i;
         trx.operations.clear();
         trx.operations.push_back( aubop );
         PUSH_TX(db, trx, ~0);
         BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(i) );
         if( i != 2 )
         {
            aubop.new_options.extensions.value.black_swan_response_method = bsrm_value;
            trx.operations.clear();
            trx.operations.push_back( aubop );
            PUSH_TX(db, trx, ~0);
            BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_order );
         }
      }

      ilog( "Generate a block" );
      generate_block();

      // final check
      BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(2) );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests scenarios:
///   updating BSRM from no_settlement to others when the least collateralized short is actually undercollateralized
BOOST_AUTO_TEST_CASE( undercollateralized_and_update_bsrm_from_no_settlement )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   using bsrm_type = bitasset_options::black_swan_response_type;
   uint8_t bsrm_value = static_cast<uint8_t>(bsrm_type::no_settlement);

   // Several passes, update BSRM from no_settlement to different values
   for( uint8_t i = 0; i <= 3; ++i )
   {
      if( i == bsrm_value )
         continue;
      idump( (i) );

      ACTORS((sam)(feeder)(borrower)(borrower2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );

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
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method()
                   == bsrm_type::no_settlement );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(8000) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // publish a new feed so that borrower's debt position is undercollateralized
      ilog( "Publish a new feed so that the least collateralized short is undercollateralized" );
      f.settlement_price = price( asset(1000,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
      BOOST_CHECK( db.find( call_id ) );
      BOOST_CHECK( db.find( call2_id ) );

      // Sam tries to update BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;
      aubop.new_options.extensions.value.black_swan_response_method = i;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      // check
      const auto& check_result = [&]
      {
         switch( static_cast<bsrm_type>(i) )
         {
         case bsrm_type::global_settlement:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::global_settlement );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( !db.find( call2_id ) );
            break;
         case bsrm_type::individual_settlement_to_fund:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_fund );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( db.find( call2_id ) );
            break;
         case bsrm_type::individual_settlement_to_order:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_order );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( db.find( call2_id ) );
            break;
         default:
            BOOST_FAIL( "This should not happen" );
            break;
         }
      };

      check_result();

      ilog( "Generate a block" );
      generate_block();

      check_result();

      // reset
      db.pop_block();

   } // for i

} FC_CAPTURE_AND_RETHROW() }

/// Tests scenarios:
///   manually trigger global settlement via asset_global_settle_operation on each BSRM type
BOOST_AUTO_TEST_CASE( manual_gs_test )
{ try {

   // Advance to core-2467 hard fork
   auto mi = db.get_global_properties().parameters.maintenance_interval;
   generate_blocks(HARDFORK_CORE_2467_TIME - mi);
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   set_expiration( db, trx );

   using bsrm_type = bitasset_options::black_swan_response_type;

   // Several passes, for each BSRM type, before and after core-2591 hf
   for( uint8_t i = 0; i < 8; ++i )
   {
      uint8_t bsrm = i % 4;

      idump( (i)(bsrm) );

      if( 4 == i )
      {
         // Advance to core-2591 hard fork
         generate_blocks(HARDFORK_CORE_2591_TIME);
         generate_block();
      }

      set_expiration( db, trx );
      ACTORS((sam)(feeder)(borrower)(borrower2));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( borrower, asset(init_amount) );
      fund( borrower2, asset(init_amount) );

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
      acop.bitasset_opts->extensions.value.black_swan_response_method = bsrm;
      acop.bitasset_opts->extensions.value.margin_call_fee_ratio = 11;

      trx.operations.clear();
      trx.operations.push_back( acop );
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& mpa = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type mpa_id = mpa.get_id();

      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == static_cast<bsrm_type>(bsrm) );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
      BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
      BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(100,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(100,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // borrow some
      const call_order_object* call_ptr = borrow( borrower, asset(100000, mpa_id), asset(2000) );
      BOOST_REQUIRE( call_ptr );
      call_order_id_type call_id = call_ptr->get_id();
      const call_order_object* call2_ptr = borrow( borrower2, asset(100000, mpa_id), asset(8000) );
      BOOST_REQUIRE( call2_ptr );
      call_order_id_type call2_id = call2_ptr->get_id();

      // publish a new feed so that borrower's debt position is undercollateralized
      ilog( "Publish a new feed so that the least collateralized short is undercollateralized" );
      f.settlement_price = price( asset(1000,mpa_id), asset(22) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // check
      const auto& check_result = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );
         switch( static_cast<bsrm_type>(bsrm) )
         {
         case bsrm_type::global_settlement:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::global_settlement );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( !db.find( call2_id ) );

            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
            // can not globally settle again
            BOOST_CHECK_THROW( force_global_settle( mpa_id(db), f.settlement_price ), fc::exception );
            break;
         case bsrm_type::no_settlement:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::no_settlement );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( db.find( call_id ) );
            BOOST_CHECK( db.find( call2_id ) );

            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(1250,mpa_id), asset(20) ) );
            // can not globally settle at real price since the least collateralized short's CR is too low
            BOOST_CHECK_THROW( force_global_settle( mpa_id(db), f.settlement_price ), fc::exception );
            break;
         case bsrm_type::individual_settlement_to_fund:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_fund );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( db.find( call2_id ) );

            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );
            // MSSR = 1250, MCFR = 11, fee = round_down(2000 * 11 / 1250) = 17, fund = 2000 - 17 = 1983
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );

            BOOST_CHECK( mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
            // current feed = 100000:1983 * (1250-11):1000
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price
                         == price( asset(123900,mpa_id), asset(1983) ) );
            break;
         case bsrm_type::individual_settlement_to_order:
            BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                         == bsrm_type::individual_settlement_to_order );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_globally_settled() );
            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
            BOOST_CHECK( db.find_settled_debt_order(mpa_id) );
            BOOST_CHECK( !db.find( call_id ) );
            BOOST_CHECK( db.find( call2_id ) );

            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 100000 );
            // MSSR = 1250, MCFR = 11, fee = round_down(2000 * 11 / 1250) = 17, fund = 2000 - 17 = 1983
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 1983 );

            BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
            BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );

            BOOST_CHECK_EQUAL( db.find_settled_debt_order(mpa_id)->for_sale.value, 1983 );
            BOOST_CHECK_EQUAL( db.find_settled_debt_order(mpa_id)->amount_to_receive().amount.value, 100000 );
            break;
         default:
            BOOST_FAIL( "This should not happen" );
            break;
         }
      };

      check_result();

      ilog( "Generate a block" );
      generate_block();

      check_result();

      // publish a new feed (collateral price rises)
      f.settlement_price = price( asset(1000,mpa_id), asset(15) );
      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // globally settle
      if( bsrm_type::no_settlement == static_cast<bsrm_type>(bsrm) )
         force_global_settle( mpa_id(db), price( asset(1000,mpa_id), asset(18) ) );
      else if( bsrm_type::individual_settlement_to_fund == static_cast<bsrm_type>(bsrm)
               || bsrm_type::individual_settlement_to_order == static_cast<bsrm_type>(bsrm) )
         force_global_settle( mpa_id(db), price( asset(1000,mpa_id), asset(22) ) );

      // check
      const auto& check_result2 = [&]
      {
         BOOST_CHECK( mpa_id(db).bitasset_data(db).get_black_swan_response_method()
                      == bsrm_type::global_settlement );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).is_globally_settled() );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_individually_settled_to_fund() );
         BOOST_CHECK( !db.find_settled_debt_order(mpa_id) );
         BOOST_CHECK( !db.find( call_id ) );
         BOOST_CHECK( !db.find( call2_id ) );
         BOOST_CHECK( !mpa_id(db).bitasset_data(db).is_current_feed_price_capped() );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).current_feed.settlement_price == f.settlement_price );
         BOOST_CHECK( mpa_id(db).bitasset_data(db).median_feed.settlement_price == f.settlement_price );

         switch( static_cast<bsrm_type>(bsrm) )
         {
         case bsrm_type::global_settlement:
            break;
         case bsrm_type::no_settlement:
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).settlement_fund.value, 3600 ); // 1800 * 2
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            break;
         case bsrm_type::individual_settlement_to_fund:
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).settlement_fund.value, 4183 ); // 1983 + 2200
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            break;
         case bsrm_type::individual_settlement_to_order:
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).settlement_fund.value, 4183 ); // 1983 + 2200
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_debt.value, 0 );
            BOOST_CHECK_EQUAL( mpa_id(db).bitasset_data(db).individual_settlement_fund.value, 0 );
            break;
         default:
            BOOST_FAIL( "This should not happen" );
            break;
         }
      };

      check_result2();

      ilog( "Generate a block" );
      generate_block();

      check_result2();

      // reset
      db.pop_block();
      db.pop_block();

   } // for i

} FC_CAPTURE_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
