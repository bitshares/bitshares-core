/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsip48_75_tests, database_fixture )

BOOST_AUTO_TEST_CASE( hardfork_protection_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_CORE_1270_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      uint16_t bitmask = ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK;
      uint16_t uiamask = DEFAULT_UIA_ASSET_ISSUER_PERMISSION;

      uint16_t bitflag = ~global_settle & ~committee_fed_asset; // high bits are set
      uint16_t uiaflag = ~(bitmask ^ uiamask); // high bits are set

      vector<operation> ops;

      // Testing asset_create_operation
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = uiaflag;
      acop.common_options.issuer_permissions = uiamask;

      trx.operations.clear();
      trx.operations.push_back( acop );

      {
         auto& op = trx.operations.front().get<asset_create_operation>();

         // Unable to set new permission bits
         op.common_options.issuer_permissions = ( uiamask | lock_max_supply );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.common_options.issuer_permissions = ( uiamask | disable_new_supply );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.bitasset_opts = bitasset_options();
         op.bitasset_opts->minimum_feeds = 3;
         op.common_options.flags = bitflag;

         op.common_options.issuer_permissions = ( bitmask | disable_mcr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.common_options.issuer_permissions = ( bitmask | disable_icr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.common_options.issuer_permissions = ( bitmask | disable_mssr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.common_options.issuer_permissions = bitmask;

         // Unable to set new extensions in bitasset options
         op.bitasset_opts->extensions.value.maintenance_collateral_ratio = 1500;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.bitasset_opts->extensions.value.maintenance_collateral_ratio = {};

         op.bitasset_opts->extensions.value.maximum_short_squeeze_ratio = 1500;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.bitasset_opts->extensions.value.maximum_short_squeeze_ratio = {};

         acop = op;
      }

      // Able to create asset without new data
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& samcoin = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type samcoin_id = samcoin.id;

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 100 );
      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 3 );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );
      ops.clear();
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
         op.new_options.flags &= ~witness_fed_asset;

         // Unable to set new permission bits
         op.new_options.issuer_permissions = ( bitmask | lock_max_supply );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.new_options.issuer_permissions = ( bitmask | disable_new_supply );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.new_options.issuer_permissions = ( bitmask | disable_mcr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.new_options.issuer_permissions = ( bitmask | disable_icr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.new_options.issuer_permissions = ( bitmask | disable_mssr_update );
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );

         op.new_options.issuer_permissions = bitmask;

         // Unable to set new extensions
         op.extensions.value.new_precision = 8;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.extensions.value.new_precision = {};

         op.extensions.value.skip_core_exchange_rate = true;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.extensions.value.skip_core_exchange_rate = {};

         auop = op;
      }

      // Able to update asset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 200 );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );
      ops.clear();
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
         op.new_options.extensions.value.maintenance_collateral_ratio = 1500;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.extensions.value.maintenance_collateral_ratio = {};

         op.new_options.extensions.value.maximum_short_squeeze_ratio = 1500;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.extensions.value.maximum_short_squeeze_ratio = {};

         aubop = op;
      }

      // Able to update bitasset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 1 );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );
      ops.clear();
      // Able to propose the good operation
      propose( aubop );

      // Testing asset_publish_feed_operation
      update_feed_producers( samcoin, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,samcoin_id), asset(1) );
      f.core_exchange_rate = price( asset(1,samcoin_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;

      asset_publish_feed_operation apfop;
      apfop.publisher = feeder_id;
      apfop.asset_id = samcoin_id;
      apfop.feed = f;

      trx.operations.clear();
      trx.operations.push_back( apfop );

      {
         auto& op = trx.operations.front().get<asset_publish_feed_operation>();

         // Unable to set new extensions
         op.extensions.value.initial_collateral_ratio = 1500;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.extensions.value.initial_collateral_ratio = {};

         apfop = op;
      }

      // Able to publish feed without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).current_feed.initial_collateral_ratio,
                         f.maintenance_collateral_ratio );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );
      ops.clear();
      // Able to propose the good operation
      propose( apfop );

      // Check what we have now
      idump( (samcoin) );
      idump( (samcoin.bitasset_data(db)) );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()

