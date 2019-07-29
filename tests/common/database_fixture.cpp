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
#include <boost/range/algorithm.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/es_objects/es_objects.hpp>

#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>
#include <graphene/chain/htlc_object.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include <iomanip>

#include "database_fixture.hpp"

using namespace graphene::chain::test;

uint32_t GRAPHENE_TESTING_GENESIS_TIMESTAMP = 1431700000;

namespace graphene { namespace chain {

using std::cout;
using std::cerr;

void clearable_block::clear()
{
   _calculated_merkle_root = checksum_type();
   _signee = fc::ecc::public_key();
   _block_id = block_id_type();
}

database_fixture::database_fixture(const fc::time_point_sec &initial_timestamp)
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
   init_account_pub_key = init_account_priv_key.get_public_key();

   boost::program_options::variables_map options;

   genesis_state.initial_timestamp = initial_timestamp;

   if(boost::unit_test::framework::current_test_case().p_name.value == "hf_935_test")
      genesis_state.initial_active_witnesses = 20;
   else
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
   genesis_state.initial_parameters.get_mutable_fees().zero_all_fees();
   genesis_state.initial_marketing_partner_account_name = "nathan";
   genesis_state.initial_charity_account_name = "nathan";

   genesis_state_type::initial_asset_type init_mpa1;
   init_mpa1.symbol = "INITMPA";
   init_mpa1.issuer_name = "committee-account";
   init_mpa1.description = "Initial MPA";
   init_mpa1.precision = 4;
   init_mpa1.initial_max_supply = GRAPHENE_INITIAL_MAX_SHARE_SUPPLY;
   init_mpa1.accumulated_fees = 0;
   init_mpa1.accumulated_fees_for_marketing_partner = 0;
   init_mpa1.accumulated_fees_for_charity = 0;
   // TODO add initial UIA's; add initial short positions; test non-zero accumulated_fees
   genesis_state.initial_assets.push_back( init_mpa1 );

   open_database();

   /**
    * Test specific settings
    */
   auto current_test_name = boost::unit_test::framework::current_test_case().p_name.value;
   auto current_test_suite_id = boost::unit_test::framework::current_test_case().p_parent_id;
   if (current_test_name == "get_account_history_operations")
   {
      options.insert(std::make_pair("max-ops-per-account", boost::program_options::variable_value((uint64_t)75, false)));
   }
   if (current_test_name == "api_limit_get_account_history_operations")
   {
    options.insert(std::make_pair("max-ops-per-account", boost::program_options::variable_value((uint64_t)125, false)));
    options.insert(std::make_pair("api-limit-get-account-history-operations", boost::program_options::variable_value((uint64_t)300, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   if(current_test_name =="api_limit_get_account_history")
   {
    options.insert(std::make_pair("max-ops-per-account", boost::program_options::variable_value((uint64_t)125, false)));
    options.insert(std::make_pair("api-limit-get-account-history", boost::program_options::variable_value((uint64_t)250, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   if(current_test_name =="api_limit_get_relative_account_history")
   {
    options.insert(std::make_pair("max-ops-per-account", boost::program_options::variable_value((uint64_t)125, false)));
    options.insert(std::make_pair("api-limit-get-relative-account-history", boost::program_options::variable_value((uint64_t)250, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   if(current_test_name =="api_limit_get_account_history_by_operations")
   {
    options.insert(std::make_pair("api-limit-get-account-history-by-operations", boost::program_options::variable_value((uint64_t)250, false)));
    options.insert(std::make_pair("api-limit-get-relative-account-history", boost::program_options::variable_value((uint64_t)250, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   if(current_test_name =="api_limit_get_asset_holders")
   {
    options.insert(std::make_pair("api-limit-get-asset-holders", boost::program_options::variable_value((uint64_t)250, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   if(current_test_name =="api_limit_get_key_references")
   {
    options.insert(std::make_pair("api-limit-get-key-references", boost::program_options::variable_value((uint64_t)200, false)));
    options.insert(std::make_pair("plugins", boost::program_options::variable_value(string("account_history"), false)));
   }
   // add account tracking for ahplugin for special test case with track-account enabled
   if( !options.count("track-account") && current_test_name == "track_account") {
      std::vector<std::string> track_account;
      std::string track = "\"1.2.17\"";
      track_account.push_back(track);
      options.insert(std::make_pair("track-account", boost::program_options::variable_value(track_account, false)));
      options.insert(std::make_pair("partial-operations", boost::program_options::variable_value(true, false)));
   }
   // account tracking 2 accounts
   if( !options.count("track-account") && current_test_name == "track_account2") {
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
   if(current_test_name == "elasticsearch_account_history" || current_test_name == "elasticsearch_suite") {
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
   else if( boost::unit_test::framework::get<boost::unit_test::test_suite>(current_test_suite_id).p_name.value != "performance_tests" )
   {
      auto ahplugin = app.register_plugin<graphene::account_history::account_history_plugin>();
      ahplugin->plugin_set_app(&app);
      ahplugin->plugin_initialize(options);
      ahplugin->plugin_startup();
      if (current_test_name == "api_limit_get_account_history_operations" || current_test_name == "api_limit_get_account_history"
      || current_test_name == "api_limit_get_relative_account_history"
      || current_test_name == "api_limit_get_account_history_by_operations" || current_test_name =="api_limit_get_asset_holders"
      || current_test_name =="api_limit_get_key_references")
      {
          app.initialize(graphene::utilities::temp_directory_path(), options);
          app.set_api_limit();
      }
   }

   if(current_test_name == "elasticsearch_objects" || current_test_name == "elasticsearch_suite") {
      auto esobjects_plugin = app.register_plugin<graphene::es_objects::es_objects_plugin>();
      esobjects_plugin->plugin_set_app(&app);

      options.insert(std::make_pair("es-objects-elasticsearch-url", boost::program_options::variable_value(string("http://localhost:9200/"), false)));
      options.insert(std::make_pair("es-objects-bulk-replay", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("es-objects-bulk-sync", boost::program_options::variable_value(uint32_t(2), false)));
      options.insert(std::make_pair("es-objects-proposals", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-accounts", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-assets", boost::program_options::variable_value(true, false)));
      options.insert(std::make_pair("es-objects-balances", boost::program_options::variable_value(true, false)));
 
      esobjects_plugin->plugin_initialize(options);
      esobjects_plugin->plugin_startup();
   }

   options.insert(std::make_pair("bucket-size", boost::program_options::variable_value(string("[15]"),false)));

   generate_block();

   set_expiration( db, trx );
   } catch ( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }

   return;
}

database_fixture::~database_fixture()
{
   try {
      // If we're unwinding due to an exception, don't do any more checks.
      // This way, boost test's last checkpoint tells us approximately where the error was.
      if( !std::uncaught_exception() )
      {
         verify_asset_supplies(db);
         BOOST_CHECK( db.get_node_properties().skip_flags == database::skip_nothing );
      }
      return;
   } catch (fc::exception& ex) {
      BOOST_FAIL( std::string("fc::exception in ~database_fixture: ") + ex.to_detail_string() );
   } catch (std::exception& e) {
      BOOST_FAIL( std::string("std::exception in ~database_fixture:") + e.what() );
   } catch (...) {
      BOOST_FAIL( "Uncaught exception in ~database_fixture" );
   }
}

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
   const auto& acct_balance_index = db.get_index_type<account_balance_index>().indices();
   map<asset_id_type,share_type> total_balances;
   map<asset_id_type,share_type> total_debts;
   share_type core_in_orders;
   share_type reported_core_in_orders;

   for( const account_balance_object& b : acct_balance_index )
      total_balances[b.asset_type] += b.balance;
   for( const account_statistics_object& a : statistics_index )
   {
      reported_core_in_orders += a.total_core_in_orders;
      total_balances[asset_id_type()] += a.pending_fees + a.pending_vested_fees;
   }
   for( const asset_object& asset_obj : db.get_index_type<asset_index>().indices() )
   {
      const auto& dasset_obj = asset_obj.dynamic_asset_data_id(db);
      total_balances[asset_obj.id] += dasset_obj.accumulated_fees;
      total_balances[asset_obj.id] += dasset_obj.accumulated_fees_for_marketing_partner;
      total_balances[asset_obj.id] += dasset_obj.accumulated_fees_for_charity;
      total_balances[asset_id_type()] += dasset_obj.fee_pool;
      total_balances[asset_obj.id] += dasset_obj.confidential_supply.value;
   }
   for( const vesting_balance_object& vbo : db.get_index_type< vesting_balance_index >().indices() )
      total_balances[ vbo.balance.asset_id ] += vbo.balance.amount;
   for( const balance_object& bo : db.get_index_type< balance_index >().indices() )
      total_balances[ bo.balance.asset_id ] += bo.balance.amount;

   total_balances[asset_id_type()] += db.get_dynamic_global_properties().witness_budget;

   for( const auto& item : total_debts )
   {
      BOOST_CHECK_EQUAL(item.first(db).dynamic_asset_data_id(db).current_supply.value, item.second.value);
   }

   // htlc
   const auto& htlc_idx = db.get_index_type< htlc_index >().indices().get< by_id >();
   for( auto itr = htlc_idx.begin(); itr != htlc_idx.end(); ++itr )
   {
      total_balances[itr->transfer.asset_id] += itr->transfer.amount;
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
   uint16_t referrer_percent /* = 100 */,
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

void database_fixture::change_fees(
   const flat_set< fee_parameters >& new_params,
   uint32_t new_scale /* = 0 */
   )
{
   const chain_parameters& current_chain_params = db.get_global_properties().parameters;
   const fee_schedule& current_fees = current_chain_params.get_current_fees();

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
   new_chain_params.get_mutable_fees() = new_fees;

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
   processed_transaction ptx = PUSH_TX(db, trx, ~0);
   auto& result = db.get<account_object>(ptx.operation_results[0].get<object_id_type>());
   trx.operations.clear();
   return result;
}

const account_object& database_fixture::create_account(
   const string& name,
   const account_object& registrar,
   const account_object& referrer,
   uint16_t referrer_percent /* = 100 (1%)*/,
   const public_key_type& key /*= public_key_type()*/
   )
{
   try
   {
      trx.operations.resize(1);
      trx.operations.back() = (make_account(name, registrar, referrer, referrer_percent, key));
      trx.validate();
      auto r = PUSH_TX(db, trx, ~0);
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
   uint16_t referrer_percent /* = 100 (1%)*/
   )
{
   try
   {
      trx.operations.clear();

      account_create_operation account_create_op;

      account_create_op.registrar = registrar_id;
      account_create_op.referrer = referrer_id;
      account_create_op.referrer_percent = referrer_percent;
      account_create_op.name = name;
      account_create_op.owner = authority(1234, public_key_type(key.get_public_key()), 1234);
      account_create_op.active = authority(5678, public_key_type(key.get_public_key()), 5678);
      account_create_op.options.memo_key = key.get_public_key();
      account_create_op.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
      trx.operations.push_back( account_create_op );

      trx.validate();

      processed_transaction ptx = PUSH_TX(db, trx, ~0);
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
   processed_transaction ptx = PUSH_TX(db, trx, ~0);
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
   processed_transaction ptx = PUSH_TX(db, trx, skip_flags );
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
   processed_transaction ptx = PUSH_TX(db, trx, ~0);
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
      PUSH_TX(db, trx, ~0);
      verify_asset_supplies(db);
      trx.operations.clear();
   } FC_CAPTURE_AND_RETHROW( (from.id)(to.id)(amount)(fee) )
}

void database_fixture::enable_fees()
{
   db.modify(global_property_id_type()(db), [](global_property_object& gpo)
   {
      gpo.parameters.get_mutable_fees() = fee_schedule::get_default();
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
      op.fee = db.get_global_properties().parameters.get_current_fees().calculate_fee(op);
      trx.operations = {op};
      PUSH_TX(db, trx, ~0);
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
      op.fee = db.get_global_properties().parameters.get_current_fees().calculate_fee(op);
      trx.operations = {op};
      PUSH_TX(db, trx, ~0);
      FC_ASSERT( op.account_to_upgrade(db).is_member(db.head_block_time()) );
      trx.clear();
      verify_asset_supplies(db);
   } FC_CAPTURE_AND_RETHROW((account))
}

string database_fixture::pretty( const asset& a )const
{
  std::stringstream ss;
  ss << a.amount.value << " ";
  ss << a.asset_id(db).symbol;
  return ss.str();
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
   auto pt = db.push_transaction( precomputable_transaction(tx), skip_flags );
   database_fixture::verify_asset_supplies(db);
   return pt;
} FC_CAPTURE_AND_RETHROW((tx)) }

} // graphene::chain::test

} } // graphene::chain
