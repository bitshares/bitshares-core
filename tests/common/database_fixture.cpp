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
#include <boost/test/unit_test.hpp>
#include <boost/program_options.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/es_objects/es_objects.hpp>

#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/smart_ref_impl.hpp>

#include <iomanip>

#include "database_fixture.hpp"

using namespace graphene::chain::test;

uint32_t GRAPHENE_TESTING_GENESIS_TIMESTAMP = 1431700000;

namespace graphene { namespace chain {

using std::cout;
using std::cerr;

database_fixture::database_fixture()
   : app(), db( *app.chain_database() )
{
   try {
   int argc = boost::unit_test::framework::master_test_suite().argc;
   char** argv = boost::unit_test::framework::master_test_suite().argv;
   for( int i=1; i<argc; i++ )
   {
      const std::string arg = argv[i];
      if( arg == "--record-assert-trip" )
         fc::enable_record_assert_trip = true;
      if( arg == "--show-test-names" )
         std::cout << "running test " << boost::unit_test::framework::current_test_case().p_name << std::endl;
   }
   auto mhplugin = app.register_plugin<graphene::market_history::market_history_plugin>();
   auto goplugin = app.register_plugin<graphene::grouped_orders::grouped_orders_plugin>();
   init_account_pub_key = init_account_priv_key.get_public_key();

   boost::program_options::variables_map options;

   genesis_state.initial_timestamp = time_point_sec( GRAPHENE_TESTING_GENESIS_TIMESTAMP );

   genesis_state.initial_active_witnesses = 10;
   for( unsigned int i = 0; i < genesis_state.initial_active_witnesses; ++i )
   {
      auto name = "init"+fc::to_string(i);
      genesis_state.initial_accounts.emplace_back(name,
                                                  init_account_priv_key.get_public_key(),
                                                  init_account_priv_key.get_public_key(),
                                                  true);
      genesis_state.initial_committee_candidates.push_back({name});
      genesis_state.initial_witness_candidates.push_back({name, init_account_priv_key.get_public_key()});
   }
   genesis_state.initial_parameters.current_fees->zero_all_fees();

   genesis_state_type::initial_asset_type init_mpa1;
   init_mpa1.symbol = "INITMPA";
   init_mpa1.issuer_name = "committee-account";
   init_mpa1.description = "Initial MPA";
   init_mpa1.precision = 4;
   init_mpa1.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   init_mpa1.accumulated_fees = 0;
   init_mpa1.is_bitasset = true;
   // TODO add initial UIA's; add initial short positions; test non-zero accumulated_fees
   genesis_state.initial_assets.push_back( init_mpa1 );

   open_database();

   // add account tracking for ahplugin for special test case with track-account enabled
   if( !options.count("track-account") && boost::unit_test::framework::current_test_case().p_name.value == "track_account") {
      std::vector<std::string> track_account;
      std::string track = "\"1.2.17\"";
      track_account.push_back(track);
      options.insert(std::make_pair("track-account", boost::program_options::variable_value(track_account, false)));
      options.insert(std::make_pair("partial-operations", boost::program_options::variable_value(true, false)));
   }
   // account tracking 2 accounts
   if( !options.count("track-account") && boost::unit_test::framework::current_test_case().p_name.value == "track_account2") {
      std::vector<std::string> track_account;
      std::string track = "\"1.2.0\"";
      track_account.push_back(track);
      track = "\"1.2.16\"";
      track_account.push_back(track);
      options.insert(std::make_pair("track-account", boost::program_options::variable_value(track_account, false)));
   }
   // standby votes tracking
   if( boost::unit_test::framework::current_test_case().p_name.value == "track_votes_witnesses_disabled" ||
       boost::unit_test::framework::current_test_case().p_name.value == "track_votes_committee_disabled") {
      app.chain_database()->enable_standby_votes_tracking( false );
   }

   auto test_name = boost::unit_test::framework::current_test_case().p_name.value;
   auto test_suite_id = boost::unit_test::framework::current_test_case().p_parent_id;
   if(test_name == "elasticsearch_account_history" || test_name == "elasticsearch_suite") {
      auto esplugin = app.register_plugin<graphene::elasticsearch::elasticsearch_plugin>();
      esplugin->plugin_set_app(&app);

      options.insert(std::make_pair("elasticsearch-node-url", boost::program_options::variable_value(string("http://localhost:9200/"), false)));
      options.insert(std::make_pair("elasticsearch-bulk-replay", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("elasticsearch-bulk-sync", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("elasticsearch-visitor", boost::program_options::variable_value(true, false)));
      //options.insert(std::make_pair("elasticsearch-basic-auth", boost::program_options::variable_value(string("elastic:changeme"), false)));

      esplugin->plugin_initialize(options);
      esplugin->plugin_startup();
   }
   else if( boost::unit_test::framework::get<boost::unit_test::test_suite>(test_suite_id).p_name.value != "performance_tests" )
   {
      auto ahplugin = app.register_plugin<graphene::account_history::account_history_plugin>();
      ahplugin->plugin_set_app(&app);
      ahplugin->plugin_initialize(options);
      ahplugin->plugin_startup();
   }

   if(test_name == "elasticsearch_objects" || test_name == "elasticsearch_suite") {
      auto esobjects_plugin = app.register_plugin<graphene::es_objects::es_objects_plugin>();
      esobjects_plugin->plugin_set_app(&app);

      options.insert(std::make_pair("es-objects-elasticsearch-url", boost::program_options::variable_value(string("http://localhost:9200/"), false)));
      options.insert(std::make_pair("es-objects-bulk-replay", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("es-objects-bulk-sync", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("es-objects-proposals", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-accounts", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-assets", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-balances", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-limit-orders", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-asset-bitasset", boost::program_options::variable_value(true, false)));

      esobjects_plugin->plugin_initialize(options);
      esobjects_plugin->plugin_startup();
   }

   options.insert(std::make_pair("bucket-size", boost::program_options::variable_value(string("[15]"),false)));
   mhplugin->plugin_set_app(&app);
   mhplugin->plugin_initialize(options);

   goplugin->plugin_set_app(&app);
   goplugin->plugin_initialize(options);

   mhplugin->plugin_startup();
   goplugin->plugin_startup();

   generate_block();

   asset_id_type mpa1_id(1);
   BOOST_REQUIRE( mpa1_id(db).is_market_issued() );
   BOOST_CHECK( mpa1_id(db).bitasset_data(db).asset_id == mpa1_id );

   set_expiration( db, trx );
   } catch ( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }

   return;
}

database_fixture::~database_fixture()
{ try {
   // If we're unwinding due to an exception, don't do any more checks.
   // This way, boost test's last checkpoint tells us approximately where the error was.
   if( !std::uncaught_exception() )
   {
      verify_asset_supplies(db);
      BOOST_CHECK( db.get_node_properties().skip_flags == database::skip_nothing );
   }
   return;
} FC_CAPTURE_AND_RETHROW() }

fc::ecc::private_key database_fixture::generate_private_key(string seed)
{
   static const fc::ecc::private_key committee = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")));
   if( seed == "null_key" )
      return committee;
   return fc::ecc::private_key::regenerate(fc::sha256::hash(seed));
}

string database_fixture::generate_anon_acct_name()
{
   // names of the form "anon-acct-x123" ; the "x" is necessary
   //    to workaround issue #46
   return "anon-acct-x" + std::to_string( anon_acct_count++ );
}

void database_fixture::verify_asset_supplies( const database& db )
{
   //wlog("*** Begin asset supply verification ***");
   const asset_dynamic_data_object& core_asset_data = db.get_core_asset().dynamic_asset_data_id(db);
   BOOST_CHECK(core_asset_data.fee_pool == 0);

   const auto& statistics_index = db.get_index_type<account_stats_index>().indices();
   const auto& balance_index = db.get_index_type<account_balance_index>().indices();
   const auto& settle_index = db.get_index_type<force_settlement_index>().indices();
   const auto& bids = db.get_index_type<collateral_bid_index>().indices();
   map<asset_id_type,share_type> total_balances;
   map<asset_id_type,share_type> total_debts;
   share_type core_in_orders;
   share_type reported_core_in_orders;

   for( const account_balance_object& b : balance_index )
      total_balances[b.asset_type] += b.balance;
   for( const force_settlement_object& s : settle_index )
      total_balances[s.balance.asset_id] += s.balance.amount;
   for( const collateral_bid_object& b : bids )
      total_balances[b.inv_swan_price.base.asset_id] += b.inv_swan_price.base.amount;
   for( const account_statistics_object& a : statistics_index )
   {
      reported_core_in_orders += a.total_core_in_orders;
      total_balances[asset_id_type()] += a.pending_fees + a.pending_vested_fees;
   }
   for( const limit_order_object& o : db.get_index_type<limit_order_index>().indices() )
   {
      asset for_sale = o.amount_for_sale();
      if( for_sale.asset_id == asset_id_type() ) core_in_orders += for_sale.amount;
      total_balances[for_sale.asset_id] += for_sale.amount;
      total_balances[asset_id_type()] += o.deferred_fee;
      total_balances[o.deferred_paid_fee.asset_id] += o.deferred_paid_fee.amount;
   }
   for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
   {
      asset col = o.get_collateral();
      if( col.asset_id == asset_id_type() ) core_in_orders += col.amount;
      total_balances[col.asset_id] += col.amount;
      total_debts[o.get_debt().asset_id] += o.get_debt().amount;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      const auto& dasset_obj = asset_obj.dynamic_asset_data_id(db);
      total_balances[asset_obj.id] += dasset_obj.accumulated_fees;
      total_balances[asset_id_type()] += dasset_obj.fee_pool;
      if( asset_obj.is_market_issued() )
      {
         const auto& bad = asset_obj.bitasset_data(db);
         total_balances[bad.options.short_backing_asset] += bad.settlement_fund;
      }
      total_balances[asset_obj.id] += dasset_obj.confidential_supply.value;
   }
   for( const vesting_balance_object& vbo : db.get_index_type< vesting_balance_index >().indices() )
      total_balances[ vbo.balance.asset_id ] += vbo.balance.amount;
   for( const fba_accumulator_object& fba : db.get_index_type< simple_index< fba_accumulator_object > >() )
      total_balances[ asset_id_type() ] += fba.accumulated_fba_fees;

   total_balances[asset_id_type()] += db.get_dynamic_global_properties().witness_budget;

   for( const auto& item : total_debts )
   {
      BOOST_CHECK_EQUAL(item.first(db).dynamic_asset_data_id(db).current_supply.value, item.second.value);
   }

   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      BOOST_CHECK_EQUAL(total_balances[asset_obj.id].value, asset_obj.dynamic_asset_data_id(db).current_supply.value);
   }

   BOOST_CHECK_EQUAL( core_in_orders.value , reported_core_in_orders.value );
//   wlog("***  End  asset supply verification ***");
}

void database_fixture::open_database()
{
   if( !data_dir ) {
      data_dir = fc::temp_directory( graphene::utilities::temp_directory_path() );
      db.open(data_dir->path(), [this]{return genesis_state;}, "test");
   }
}

signed_block database_fixture::generate_block(uint32_t skip, const fc::ecc::private_key& key, int miss_blocks)
{
   skip |= database::skip_undo_history_check;
   // skip == ~0 will skip checks specified in database::validation_steps
   auto block = db.generate_block(db.get_slot_time(miss_blocks + 1),
                            db.get_scheduled_witness(miss_blocks + 1),
                            key, skip);
   db.clear_pending();
   return block;
}

void database_fixture::generate_blocks( uint32_t block_count )
{
   for( uint32_t i = 0; i < block_count; ++i )
      generate_block();
}

uint32_t database_fixture::generate_blocks(fc::time_point_sec timestamp, bool miss_intermediate_blocks, uint32_t skip)
{
   if( miss_intermediate_blocks )
   {
      generate_block(skip);
      auto slots_to_miss = db.get_slot_at_time(timestamp);
      if( slots_to_miss <= 1 )
         return 1;
      --slots_to_miss;
      generate_block(skip, init_account_priv_key, slots_to_miss);
      return 2;
   }
   uint32_t blocks = 0;
   while( db.head_block_time() < timestamp )
   {
      generate_block(skip);
      ++blocks;
   }
   return blocks;
}

account_create_operation database_fixture::make_account(
   const std::string& name /* = "nathan" */,
   public_key_type key /* = key_id_type() */
   )
{ try {
   account_create_operation create_account;
   create_account.registrar = account_id_type();

   create_account.name = name;
   create_account.owner = authority(123, key, 123);
   create_account.active = authority(321, key, 321);
   create_account.options.memo_key = key;
   create_account.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;

   auto& active_committee_members = db.get_global_properties().active_committee_members;
   if( active_committee_members.size() > 0 )
   {
      set<vote_id_type> votes;
      votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
      votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
      votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
      votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
      votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
      create_account.options.votes = flat_set<vote_id_type>(votes.begin(), votes.end());
   }
   create_account.options.num_committee = create_account.options.votes.size();

   create_account.fee = db.current_fee_schedule().calculate_fee( create_account );
   return create_account;
} FC_CAPTURE_AND_RETHROW() }

account_create_operation database_fixture::make_account(
   const std::string& name,
   const account_object& registrar,
   const account_object& referrer,
   uint8_t referrer_percent /* = 100 */,
   public_key_type key /* = public_key_type() */
   )
{
   try
   {
      account_create_operation          create_account;

      create_account.registrar          = registrar.id;
      create_account.referrer           = referrer.id;
      create_account.referrer_percent   = referrer_percent;

      create_account.name = name;
      create_account.owner = authority(123, key, 123);
      create_account.active = authority(321, key, 321);
      create_account.options.memo_key = key;
      create_account.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;

      const vector<committee_member_id_type>& active_committee_members = db.get_global_properties().active_committee_members;
      if( active_committee_members.size() > 0 )
      {
         set<vote_id_type> votes;
         votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
         votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
         votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
         votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
         votes.insert(active_committee_members[rand() % active_committee_members.size()](db).vote_id);
         create_account.options.votes = flat_set<vote_id_type>(votes.begin(), votes.end());
      }
      create_account.options.num_committee = create_account.options.votes.size();

      create_account.fee = db.current_fee_schedule().calculate_fee( create_account );
      return create_account;
   }
   FC_CAPTURE_AND_RETHROW((name)(referrer_percent))
}

const asset_object& database_fixture::get_asset( const string& symbol )const
{
   const auto& idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
   const auto itr = idx.find(symbol);
   assert( itr != idx.end() );
   return *itr;
}

const account_object& database_fixture::get_account( const string& name )const
{
   const auto& idx = db.get_index_type<account_index>().indices().get<by_name>();
   const auto itr = idx.find(name);
   assert( itr != idx.end() );
   return *itr;
}

const asset_object& database_fixture::create_bitasset(
   const string& name,
   account_id_type issuer /* = GRAPHENE_WITNESS_ACCOUNT */,
   uint16_t market_fee_percent /* = 100 */ /* 1% */,
   uint16_t flags /* = charge_market_fee */,
   uint16_t precision /* = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS */,
   asset_id_type backing_asset /* = CORE */
   )
{ try {
   asset_create_operation creator;
   creator.issuer = issuer;
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   creator.precision = precision;
   creator.common_options.market_fee_percent = market_fee_percent;
   if( issuer == GRAPHENE_WITNESS_ACCOUNT )
      flags |= witness_fed_asset;
   creator.common_options.issuer_permissions = flags;
   creator.common_options.flags = flags & ~global_settle;
   creator.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
   creator.bitasset_opts = bitasset_options();
   creator.bitasset_opts->short_backing_asset = backing_asset;
   trx.operations.push_back(std::move(creator));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
} FC_CAPTURE_AND_RETHROW( (name)(flags) ) }

const asset_object& database_fixture::create_prediction_market(
   const string& name,
   account_id_type issuer /* = GRAPHENE_WITNESS_ACCOUNT */,
   uint16_t market_fee_percent /* = 100 */ /* 1% */,
   uint16_t flags /* = charge_market_fee */,
   uint16_t precision /* = 2, which seems arbitrary, but historically chosen */,
   asset_id_type backing_asset /* = CORE */
   )
{ try {
   asset_create_operation creator;
   creator.issuer = issuer;
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   creator.precision = precision;
   creator.common_options.market_fee_percent = market_fee_percent;
   creator.common_options.issuer_permissions = flags | global_settle;
   creator.common_options.flags = flags & ~global_settle;
   if( issuer == GRAPHENE_WITNESS_ACCOUNT )
      creator.common_options.flags |= witness_fed_asset;
   creator.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
   creator.bitasset_opts = bitasset_options();
   creator.bitasset_opts->short_backing_asset = backing_asset;
   creator.is_prediction_market = true;
   trx.operations.push_back(std::move(creator));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
} FC_CAPTURE_AND_RETHROW( (name)(flags) ) }


const asset_object& database_fixture::create_user_issued_asset( const string& name )
{
   asset_create_operation creator;
   creator.issuer = account_id_type();
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = 0;
   creator.precision = 2;
   creator.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
   creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   creator.common_options.flags = charge_market_fee;
   creator.common_options.issuer_permissions = charge_market_fee;
   trx.operations.push_back(std::move(creator));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
}

const asset_object& database_fixture::create_user_issued_asset( const string& name, const account_object& issuer, uint16_t flags,
                                                                const price& core_exchange_rate, uint16_t precision)
{
   asset_create_operation creator;
   creator.issuer = issuer.id;
   creator.fee = asset();
   creator.symbol = name;
   creator.common_options.max_supply = 0;
   creator.precision = precision;
   creator.common_options.core_exchange_rate = core_exchange_rate;
   creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   creator.common_options.flags = flags;
   creator.common_options.issuer_permissions = flags;
   trx.operations.clear();
   trx.operations.push_back(std::move(creator));
   set_expiration( db, trx );
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
}

void database_fixture::issue_uia( const account_object& recipient, asset amount )
{
   BOOST_TEST_MESSAGE( "Issuing UIA" );
   asset_issue_operation op;
   op.issuer = amount.asset_id(db).issuer;
   op.asset_to_issue = amount;
   op.issue_to_account = recipient.id;
   trx.operations.push_back(op);
   db.push_transaction( trx, ~0 );
   trx.operations.clear();
}

void database_fixture::issue_uia( account_id_type recipient_id, asset amount )
{
   issue_uia( recipient_id(db), amount );
}

void database_fixture::change_fees(
   const flat_set< fee_parameters >& new_params,
   uint32_t new_scale /* = 0 */
   )
{
   const chain_parameters& current_chain_params = db.get_global_properties().parameters;
   const fee_schedule& current_fees = *(current_chain_params.current_fees);

   flat_map< int, fee_parameters > fee_map;
   fee_map.reserve( current_fees.parameters.size() );
   for( const fee_parameters& op_fee : current_fees.parameters )
      fee_map[ op_fee.which() ] = op_fee;
   for( const fee_parameters& new_fee : new_params )
      fee_map[ new_fee.which() ] = new_fee;

   fee_schedule_type new_fees;

   for( const std::pair< int, fee_parameters >& item : fee_map )
      new_fees.parameters.insert( item.second );
   if( new_scale != 0 )
      new_fees.scale = new_scale;

   chain_parameters new_chain_params = current_chain_params;
   new_chain_params.current_fees = new_fees;

   db.modify(db.get_global_properties(), [&](global_property_object& p) {
      p.parameters = new_chain_params;
   });
}

const account_object& database_fixture::create_account(
   const string& name,
   const public_key_type& key /* = public_key_type() */
   )
{
   trx.operations.push_back(make_account(name, key));
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   auto& result = db.get<account_object>(ptx.operation_results[0].get<object_id_type>());
   trx.operations.clear();
   return result;
}

const account_object& database_fixture::create_account(
   const string& name,
   const account_object& registrar,
   const account_object& referrer,
   uint8_t referrer_percent /* = 100 */,
   const public_key_type& key /*= public_key_type()*/
   )
{
   try
   {
      trx.operations.resize(1);
      trx.operations.back() = (make_account(name, registrar, referrer, referrer_percent, key));
      trx.validate();
      auto r = db.push_transaction(trx, ~0);
      const auto& result = db.get<account_object>(r.operation_results[0].get<object_id_type>());
      trx.operations.clear();
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (name)(registrar)(referrer) )
}

const account_object& database_fixture::create_account(
   const string& name,
   const private_key_type& key,
   const account_id_type& registrar_id /* = account_id_type() */,
   const account_id_type& referrer_id /* = account_id_type() */,
   uint8_t referrer_percent /* = 100 */
   )
{
   try
   {
      trx.operations.clear();

      account_create_operation account_create_op;

      account_create_op.registrar = registrar_id;
      account_create_op.name = name;
      account_create_op.owner = authority(1234, public_key_type(key.get_public_key()), 1234);
      account_create_op.active = authority(5678, public_key_type(key.get_public_key()), 5678);
      account_create_op.options.memo_key = key.get_public_key();
      account_create_op.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
      trx.operations.push_back( account_create_op );

      trx.validate();

      processed_transaction ptx = db.push_transaction(trx, ~0);
      const account_object& result = db.get<account_object>(ptx.operation_results[0].get<object_id_type>());
      trx.operations.clear();
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (name)(registrar_id)(referrer_id) )
}

const committee_member_object& database_fixture::create_committee_member( const account_object& owner )
{
   committee_member_create_operation op;
   op.committee_member_account = owner.id;
   trx.operations.push_back(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.operations.clear();
   return db.get<committee_member_object>(ptx.operation_results[0].get<object_id_type>());
}

const witness_object&database_fixture::create_witness(account_id_type owner,
                                                        const fc::ecc::private_key& signing_private_key,
                                                        uint32_t skip_flags )
{
   return create_witness(owner(db), signing_private_key, skip_flags );
}

const witness_object& database_fixture::create_witness( const account_object& owner,
                                                        const fc::ecc::private_key& signing_private_key,
                                                        uint32_t skip_flags )
{ try {
   witness_create_operation op;
   op.witness_account = owner.id;
   op.block_signing_key = signing_private_key.get_public_key();
   trx.operations.push_back(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, skip_flags );
   trx.clear();
   return db.get<witness_object>(ptx.operation_results[0].get<object_id_type>());
} FC_CAPTURE_AND_RETHROW() }

const worker_object& database_fixture::create_worker( const account_id_type owner, const share_type daily_pay, const fc::microseconds& duration )
{ try {
   worker_create_operation op;
   op.owner = owner;
   op.daily_pay = daily_pay;
   op.initializer = burn_worker_initializer();
   op.work_begin_date = db.head_block_time();
   op.work_end_date = op.work_begin_date + duration;
   trx.operations.push_back(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   trx.clear();
   return db.get<worker_object>(ptx.operation_results[0].get<object_id_type>());
} FC_CAPTURE_AND_RETHROW() }

uint64_t database_fixture::fund(
   const account_object& account,
   const asset& amount /* = asset(500000) */
   )
{
   transfer(account_id_type()(db), account, amount);
   return get_balance(account, amount.asset_id(db));
}

void database_fixture::sign(signed_transaction& trx, const fc::ecc::private_key& key)
{
   trx.sign( key, db.get_chain_id() );
}

digest_type database_fixture::digest( const transaction& tx )
{
   return tx.digest();
}

const limit_order_object*database_fixture::create_sell_order(account_id_type user, const asset& amount, const asset& recv,
                                                const time_point_sec order_expiration,
                                                const price& fee_core_exchange_rate )
{
   auto r =  create_sell_order( user(db), amount, recv, order_expiration, fee_core_exchange_rate );
   verify_asset_supplies(db);
   return r;
}

const limit_order_object* database_fixture::create_sell_order( const account_object& user, const asset& amount, const asset& recv,
                                                const time_point_sec order_expiration,
                                                const price& fee_core_exchange_rate )
{
   limit_order_create_operation buy_order;
   buy_order.seller = user.id;
   buy_order.amount_to_sell = amount;
   buy_order.min_to_receive = recv;
   buy_order.expiration = order_expiration;
   trx.operations.push_back(buy_order);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op, fee_core_exchange_rate);
   trx.validate();
   auto processed = db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
   return db.find<limit_order_object>( processed.operation_results[0].get<object_id_type>() );
}

asset database_fixture::cancel_limit_order( const limit_order_object& order )
{
  limit_order_cancel_operation cancel_order;
  cancel_order.fee_paying_account = order.seller;
  cancel_order.order = order.id;
  trx.operations.push_back(cancel_order);
  for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
  trx.validate();
  auto processed = db.push_transaction(trx, ~0);
  trx.operations.clear();
   verify_asset_supplies(db);
  return processed.operation_results[0].get<asset>();
}

void database_fixture::transfer(
   account_id_type from,
   account_id_type to,
   const asset& amount,
   const asset& fee /* = asset() */
   )
{
   transfer(from(db), to(db), amount, fee);
}

void database_fixture::transfer(
   const account_object& from,
   const account_object& to,
   const asset& amount,
   const asset& fee /* = asset() */ )
{
   try
   {
      set_expiration( db, trx );
      transfer_operation trans;
      trans.from = from.id;
      trans.to   = to.id;
      trans.amount = amount;
      trx.operations.push_back(trans);

      if( fee == asset() )
      {
         for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
      }
      trx.validate();
      db.push_transaction(trx, ~0);
      verify_asset_supplies(db);
      trx.operations.clear();
   } FC_CAPTURE_AND_RETHROW( (from.id)(to.id)(amount)(fee) )
}

void database_fixture::update_feed_producers( const asset_object& mia, flat_set<account_id_type> producers )
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   asset_update_feed_producers_operation op;
   op.asset_to_update = mia.id;
   op.issuer = mia.issuer;
   op.new_feed_producers = std::move(producers);
   trx.operations = {std::move(op)};

   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
} FC_CAPTURE_AND_RETHROW( (mia)(producers) ) }

void database_fixture::publish_feed( const asset_object& mia, const account_object& by, const price_feed& f )
{
   set_expiration( db, trx );
   trx.operations.clear();

   asset_publish_feed_operation op;
   op.publisher = by.id;
   op.asset_id = mia.id;
   op.feed = f;
   if( op.feed.core_exchange_rate.is_null() )
      op.feed.core_exchange_rate = op.feed.settlement_price;
   trx.operations.emplace_back( std::move(op) );

   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
}

/***
 * @brief helper method to add a price feed
 *
 * Adds a price feed for asset2, pushes the transaction, and generates the block
 *
 * @param fixture the database_fixture
 * @param publisher who is publishing the feed
 * @param asset1 the base asset
 * @param amount1 the amount of the base asset
 * @param asset2 the quote asset
 * @param amount2 the amount of the quote asset
 * @param core_id id of core (helps with core_exchange_rate)
 */
void database_fixture::publish_feed(const account_id_type& publisher,
      const asset_id_type& asset1, int64_t amount1,
      const asset_id_type& asset2, int64_t amount2,
      const asset_id_type& core_id)
{
   const asset_object& a1 = asset1(db);
   const asset_object& a2 = asset2(db);
   const asset_object& core = core_id(db);
   asset_publish_feed_operation op;
   op.publisher = publisher;
   op.asset_id = asset2;
   op.feed.settlement_price = ~price(a1.amount(amount1),a2.amount(amount2));
   op.feed.core_exchange_rate = ~price(core.amount(amount1), a2.amount(amount2));
   trx.operations.push_back(std::move(op));
   PUSH_TX( db, trx, ~0);
   generate_block();
   trx.clear();
}

void database_fixture::force_global_settle( const asset_object& what, const price& p )
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   asset_global_settle_operation sop;
   sop.issuer = what.issuer;
   sop.asset_to_settle = what.id;
   sop.settle_price = p;
   trx.operations.push_back(sop);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
} FC_CAPTURE_AND_RETHROW( (what)(p) ) }

operation_result database_fixture::force_settle( const account_object& who, asset what )
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   asset_settle_operation sop;
   sop.account = who.id;
   sop.amount = what;
   trx.operations.push_back(sop);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   processed_transaction ptx = db.push_transaction(trx, ~0);
   const operation_result& op_result = ptx.operation_results.front();
   trx.operations.clear();
   verify_asset_supplies(db);
   return op_result;
} FC_CAPTURE_AND_RETHROW( (who)(what) ) }

const call_order_object* database_fixture::borrow( const account_object& who, asset what, asset collateral,
                                                   optional<uint16_t> target_cr )
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   call_order_update_operation update;
   update.funding_account = who.id;
   update.delta_collateral = collateral;
   update.delta_debt = what;
   update.extensions.value.target_collateral_ratio = target_cr;
   trx.operations.push_back(update);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);

   auto& call_idx = db.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(who.id, what.asset_id) );
   const call_order_object* call_obj = nullptr;

   if( itr != call_idx.end() )
      call_obj = &*itr;
   return call_obj;
} FC_CAPTURE_AND_RETHROW( (who.name)(what)(collateral)(target_cr) ) }

void database_fixture::cover(const account_object& who, asset what, asset collateral, optional<uint16_t> target_cr)
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   call_order_update_operation update;
   update.funding_account = who.id;
   update.delta_collateral = -collateral;
   update.delta_debt = -what;
   update.extensions.value.target_collateral_ratio = target_cr;
   trx.operations.push_back(update);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
} FC_CAPTURE_AND_RETHROW( (who.name)(what)(collateral)(target_cr) ) }

void database_fixture::bid_collateral(const account_object& who, const asset& to_bid, const asset& to_cover)
{ try {
   set_expiration( db, trx );
   trx.operations.clear();
   bid_collateral_operation bid;
   bid.bidder = who.id;
   bid.additional_collateral = to_bid;
   bid.debt_covered = to_cover;
   trx.operations.push_back(bid);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
} FC_CAPTURE_AND_RETHROW( (who.name)(to_bid)(to_cover) ) }

void database_fixture::fund_fee_pool( const account_object& from, const asset_object& asset_to_fund, const share_type amount )
{
   asset_fund_fee_pool_operation fund;
   fund.from_account = from.id;
   fund.asset_id = asset_to_fund.id;
   fund.amount = amount;
   trx.operations.push_back( fund );

   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   set_expiration( db, trx );
   db.push_transaction(trx, ~0);
   trx.operations.clear();
   verify_asset_supplies(db);
}

void database_fixture::enable_fees()
{
   db.modify(global_property_id_type()(db), [](global_property_object& gpo)
   {
      gpo.parameters.current_fees = fee_schedule::get_default();
   });
}

void database_fixture::upgrade_to_lifetime_member(account_id_type account)
{
   upgrade_to_lifetime_member(account(db));
}

void database_fixture::upgrade_to_lifetime_member( const account_object& account )
{
   try
   {
      account_upgrade_operation op;
      op.account_to_upgrade = account.get_id();
      op.upgrade_to_lifetime_member = true;
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations = {op};
      db.push_transaction(trx, ~0);
      FC_ASSERT( op.account_to_upgrade(db).is_lifetime_member() );
      trx.clear();
      verify_asset_supplies(db);
   }
   FC_CAPTURE_AND_RETHROW((account))
}

void database_fixture::upgrade_to_annual_member(account_id_type account)
{
   upgrade_to_annual_member(account(db));
}

void database_fixture::upgrade_to_annual_member(const account_object& account)
{
   try {
      account_upgrade_operation op;
      op.account_to_upgrade = account.get_id();
      op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
      trx.operations = {op};
      db.push_transaction(trx, ~0);
      FC_ASSERT( op.account_to_upgrade(db).is_member(db.head_block_time()) );
      trx.clear();
      verify_asset_supplies(db);
   } FC_CAPTURE_AND_RETHROW((account))
}

void database_fixture::print_market( const string& syma, const string& symb )const
{
   const auto& limit_idx = db.get_index_type<limit_order_index>();
   const auto& price_idx = limit_idx.indices().get<by_price>();

   cerr << std::fixed;
   cerr.precision(5);
   cerr << std::setw(10) << std::left  << "NAME"      << " ";
   cerr << std::setw(16) << std::right << "FOR SALE"  << " ";
   cerr << std::setw(16) << std::right << "FOR WHAT"  << " ";
   cerr << std::setw(10) << std::right << "PRICE (S/W)"   << " ";
   cerr << std::setw(10) << std::right << "1/PRICE (W/S)" << "\n";
   cerr << string(70, '=') << std::endl;
   auto cur = price_idx.begin();
   while( cur != price_idx.end() )
   {
      cerr << std::setw( 10 ) << std::left   << cur->seller(db).name << " ";
      cerr << std::setw( 10 ) << std::right  << cur->for_sale.value << " ";
      cerr << std::setw( 5 )  << std::left   << cur->amount_for_sale().asset_id(db).symbol << " ";
      cerr << std::setw( 10 ) << std::right  << cur->amount_to_receive().amount.value << " ";
      cerr << std::setw( 5 )  << std::left   << cur->amount_to_receive().asset_id(db).symbol << " ";
      cerr << std::setw( 10 ) << std::right  << cur->sell_price.to_real() << " ";
      cerr << std::setw( 10 ) << std::right  << (~cur->sell_price).to_real() << " ";
      cerr << "\n";
      ++cur;
   }
}

string database_fixture::pretty( const asset& a )const
{
  std::stringstream ss;
  ss << a.amount.value << " ";
  ss << a.asset_id(db).symbol;
  return ss.str();
}

void database_fixture::print_limit_order( const limit_order_object& cur )const
{
  std::cout << std::setw(10) << cur.seller(db).name << " ";
  std::cout << std::setw(10) << "LIMIT" << " ";
  std::cout << std::setw(16) << pretty( cur.amount_for_sale() ) << " ";
  std::cout << std::setw(16) << pretty( cur.amount_to_receive() ) << " ";
  std::cout << std::setw(16) << cur.sell_price.to_real() << " ";
}

void database_fixture::print_call_orders()const
{
  cout << std::fixed;
  cout.precision(5);
  cout << std::setw(10) << std::left  << "NAME"      << " ";
  cout << std::setw(10) << std::right << "TYPE"      << " ";
  cout << std::setw(16) << std::right << "DEBT"  << " ";
  cout << std::setw(16) << std::right << "COLLAT"  << " ";
  cout << std::setw(16) << std::right << "CALL PRICE(D/C)"     << " ";
  cout << std::setw(16) << std::right << "~CALL PRICE(C/D)"     << " ";
  cout << std::setw(16) << std::right << "SWAN(D/C)"     << " ";
  cout << std::setw(16) << std::right << "SWAN(C/D)"     << "\n";
  cout << string(70, '=');

  for( const call_order_object& o : db.get_index_type<call_order_index>().indices() )
  {
     std::cout << "\n";
     cout << std::setw( 10 ) << std::left   << o.borrower(db).name << " ";
     cout << std::setw( 16 ) << std::right  << pretty( o.get_debt() ) << " ";
     cout << std::setw( 16 ) << std::right  << pretty( o.get_collateral() ) << " ";
     cout << std::setw( 16 ) << std::right  << o.call_price.to_real() << " ";
     cout << std::setw( 16 ) << std::right  << (~o.call_price).to_real() << " ";
     cout << std::setw( 16 ) << std::right  << (o.get_debt()/o.get_collateral()).to_real() << " ";
     cout << std::setw( 16 ) << std::right  << (~(o.get_debt()/o.get_collateral())).to_real() << " ";
  }
     std::cout << "\n";
}

void database_fixture::print_joint_market( const string& syma, const string& symb )const
{
  cout << std::fixed;
  cout.precision(5);

  cout << std::setw(10) << std::left  << "NAME"      << " ";
  cout << std::setw(10) << std::right << "TYPE"      << " ";
  cout << std::setw(16) << std::right << "FOR SALE"  << " ";
  cout << std::setw(16) << std::right << "FOR WHAT"  << " ";
  cout << std::setw(16) << std::right << "PRICE (S/W)" << "\n";
  cout << string(70, '=');

  const auto& limit_idx = db.get_index_type<limit_order_index>();
  const auto& limit_price_idx = limit_idx.indices().get<by_price>();

  auto limit_itr = limit_price_idx.begin();
  while( limit_itr != limit_price_idx.end() )
  {
     std::cout << std::endl;
     print_limit_order( *limit_itr );
     ++limit_itr;
  }
}

int64_t database_fixture::get_balance( account_id_type account, asset_id_type a )const
{
  return db.get_balance(account, a).amount.value;
}

int64_t database_fixture::get_balance( const account_object& account, const asset_object& a )const
{
  return db.get_balance(account.get_id(), a.get_id()).amount.value;
}

vector< operation_history_object > database_fixture::get_operation_history( account_id_type account_id )const
{
   vector< operation_history_object > result;
   const auto& stats = account_id(db).statistics(db);
   if(stats.most_recent_op == account_transaction_history_id_type())
      return result;

   const account_transaction_history_object* node = &stats.most_recent_op(db);
   while( true )
   {
      result.push_back( node->operation_id(db) );
      if(node->next == account_transaction_history_id_type())
         break;
      node = db.find(node->next);
   }
   return result;
}

vector< graphene::market_history::order_history_object > database_fixture::get_market_order_history( asset_id_type a, asset_id_type b )const
{
   const auto& history_idx = db.get_index_type<graphene::market_history::history_index>().indices().get<graphene::market_history::by_key>();
   graphene::market_history::history_key hkey;
   if( a > b ) std::swap(a,b);
   hkey.base = a;
   hkey.quote = b;
   hkey.sequence = std::numeric_limits<int64_t>::min();
   auto itr = history_idx.lower_bound( hkey );
   vector<graphene::market_history::order_history_object> result;
   while( itr != history_idx.end())
   {
       result.push_back( *itr );
       ++itr;
   }
   return result;
}

namespace test {

void set_expiration( const database& db, transaction& tx )
{
   const chain_parameters& params = db.get_global_properties().parameters;
   tx.set_reference_block(db.head_block_id());
   tx.set_expiration( db.head_block_time() + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 3 ) );
   return;
}

bool _push_block( database& db, const signed_block& b, uint32_t skip_flags /* = 0 */ )
{
   return db.push_block( b, skip_flags);
}

processed_transaction _push_transaction( database& db, const signed_transaction& tx, uint32_t skip_flags /* = 0 */ )
{ try {
   auto pt = db.push_transaction( tx, skip_flags );
   database_fixture::verify_asset_supplies(db);
   return pt;
} FC_CAPTURE_AND_RETHROW((tx)) }

} // graphene::chain::test

} } // graphene::chain
