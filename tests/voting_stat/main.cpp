/*
 * Copyright (c) 2019 Blockchain Projects BV.
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
#include <boost/program_options.hpp>
#include <boost/range.hpp>
#include <boost/tuple/tuple.hpp>

#include <fc/optional.hpp>

#include <graphene/voting_stat/voting_stat_plugin.hpp>
#include <graphene/es_objects/es_objects.hpp>
#include <graphene/utilities/elasticsearch.hpp>

#include <graphene/app/api.hpp>

#include <graphene/chain/protocol/account.hpp>

#include <graphene/chain/voting_statistics_object.hpp>
#include <graphene/chain/voteable_statistics_object.hpp>

#include "../common/database_fixture.hpp"

#define BOOST_TEST_MODULE Voting Statistics Plugin Test
#include <boost/test/included/unit_test.hpp>

using namespace fc;
using namespace graphene::app;
using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::voting_stat;
using namespace graphene::es_objects;
using namespace graphene::utilities;
namespace bpo = boost::program_options;

struct voting_stat_fixture : public database_fixture
{
    vote_id_type default_vote_id;
    CURL *_curl;
    ES _es;

    voting_stat_fixture()
    {
        try
        {
            _curl = curl_easy_init();
            _es.curl = _curl;
            _es.elasticsearch_url = "http://localhost:9200/";
            _es.index_prefix = "objects-";

            app.register_plugin<voting_stat_plugin>( true );
            app.register_plugin<es_objects_plugin>( true );
        }
        catch(fc::exception &e)
        {
            edump((e.to_detail_string() ));
        }

    };

    void make_next_maintenance_interval()
    {
        generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
        generate_block();
    }

    void set_account_options( account_id_type id, optional<account_id_type> proxy = optional<account_id_type>() )
    {
        witness_id_type wit_id = *db.get_global_properties().active_witnesses.begin();
        default_vote_id = wit_id(db).vote_id;

        account_options opt;
        opt.votes = { default_vote_id };
        opt.num_witness = opt.votes.size();
        if( proxy )
            opt.voting_account = *proxy;

        account_update_operation op;
        op.account = id;
        op.new_options = opt;

        signed_transaction trx;
        trx.operations.push_back( op );
        set_expiration( db, trx );
        PUSH_TX( db, trx, ~0 );
    }

    const voting_statistics_object& get_voting_statistics_object( account_id_type id )
    {
        const auto& idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();
        auto last_block = idx.rbegin()->block_number;
        auto range = idx.equal_range( boost::make_tuple( last_block, id ) );
        if( range.first == range.second )
            BOOST_FAIL( "object could not be found, which should never happen" );
        return *range.first;
    }
};

BOOST_FIXTURE_TEST_SUITE( voting_stat_tests, voting_stat_fixture )

BOOST_AUTO_TEST_CASE( test_voting_statistics_object_tracking_without_proxy )
{ try {

    bpo::options_description cli;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "true"
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );


    ACTOR( alice );
    set_account_options( alice_id );


    const auto& alice_acc = alice_id(db);
    BOOST_CHECK( *alice_acc.options.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_acc.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    const auto& alice_stat1 = get_voting_statistics_object( alice_id );

    BOOST_CHECK( alice_stat1.proxy == GRAPHENE_PROXY_TO_SELF_ACCOUNT );
    BOOST_CHECK( !alice_stat1.has_proxy() );
    BOOST_CHECK( alice_stat1.proxy_for.empty() );
    BOOST_CHECK( alice_stat1.stake == 1 );
    BOOST_CHECK( *alice_stat1.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_stat1.get_total_voting_stake() == 1 );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    const auto& alice_stat2 = get_voting_statistics_object( alice_id );

    BOOST_CHECK( alice_stat2.proxy == GRAPHENE_PROXY_TO_SELF_ACCOUNT );
    BOOST_CHECK( !alice_stat2.has_proxy() );
    BOOST_CHECK( alice_stat2.proxy_for.empty() );
    BOOST_CHECK( alice_stat2.stake == 2 );
    BOOST_CHECK( *alice_stat2.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_stat2.get_total_voting_stake() == 2 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( test_voting_statistics_with_proxy_delete_after_interval )
{ try {

    bpo::options_description cli_vs;
    bpo::options_description cli_es;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli_vs, cfg );

    auto es_objects = app.get_plugin<es_objects_plugin>("es_objects");
    es_objects->plugin_set_program_options( cli_es, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "false",
        "--voting-stat-track-witness-votes",    "false",
        "--voting-stat-track-committee-votes",  "false",
        "--voting-stat-track-worker-votes",     "false",

        "--es-objects-bulk-replay",             "1",
        "--es-objects-proposals",               "false",
        "--es-objects-accounts",                "false",
        "--es-objects-assets",                  "false",
        "--es-objects-balances",                "false",
        "--es-objects-limit-orders",            "false",
        "--es-objects-asset-bitasset",          "false",
        "--es-objects-keep-only-current",       "true",
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );

    auto objects_deleted = graphene::utilities::deleteAll(_es);
    if( !objects_deleted )
        BOOST_FAIL( "elastic search DB could not be deleted" );


    ACTORS( (alice)(bob)(charlie) );
    transfer( committee_account, alice_id, asset(1) );
    transfer( committee_account, bob_id, asset(2) );
    transfer( committee_account, charlie_id, asset(3) );

    // proxy chain: alice => bob => charlie
    set_account_options( alice_id, bob_id );
    set_account_options( bob_id, charlie_id );
    set_account_options( charlie_id );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( alice_stat.has_proxy() );
        BOOST_CHECK( alice_stat.proxy == bob_id );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == 0 );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy == charlie_id );
        auto alice_proxied = *bob_stat.proxy_for.begin();
        BOOST_CHECK( alice_proxied.first == alice_id && alice_proxied.second == 1 );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 1 );

        BOOST_CHECK( !charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == (2 + 3) );
    }

    // proxy: alice => alice; bob => charlie;
    set_account_options( alice_id, GRAPHENE_PROXY_TO_SELF_ACCOUNT );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( !alice_stat.has_proxy() );
        BOOST_CHECK( alice_stat.proxy_for.empty() );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == 1 );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy_for.empty() );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );

        BOOST_CHECK( !charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == (2 + 3) );
    }

    // proxy: alice => alice; bob => charlie; charlie => alice; stake increase
    set_account_options( charlie_id, alice_id );
    transfer( committee_account, alice_id, asset(10) );
    transfer( committee_account, bob_id, asset(20) );
    transfer( committee_account, charlie_id, asset(30) );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( !alice_stat.has_proxy() );
        auto charlie_proxied = *alice_stat.proxy_for.begin();
        BOOST_CHECK( charlie_proxied.first == charlie_id && charlie_proxied.second == 33 );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == (11 + 33) );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy_for.empty() );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );
        BOOST_CHECK( bob_stat.stake == 22 );

        BOOST_CHECK( charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 22 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == 22 );
    }

    // only stake increase
    transfer( committee_account, alice_id, asset(100) );
    transfer( committee_account, bob_id, asset(200) );
    transfer( committee_account, charlie_id, asset(300) );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( alice_stat.stake == 111 );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == (111 + 333) );
        BOOST_CHECK( bob_stat.stake == 222 );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );
        BOOST_CHECK( charlie_stat.stake == 333 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == 222 );
    }

    fc::usleep(fc::milliseconds(2000));
    string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
    _es.endpoint = _es.index_prefix + "*/data/_count";
    _es.query = query;
    auto res = graphene::utilities::simpleQuery(_es);
    variant j = fc::json::from_string(res);
    BOOST_CHECK( j["count"].as_int64() == 12 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( test_voting_statistics_with_proxy_keep_after_interval )
{ try {

    // this test is the same as above just that objects are not deleted with each interval

    bpo::options_description cli_vs;
    bpo::options_description cli_es;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli_vs, cfg );

    auto es_objects = app.get_plugin<es_objects_plugin>("es_objects");
    es_objects->plugin_set_program_options( cli_es, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "true",
        "--voting-stat-track-witness-votes",    "false",
        "--voting-stat-track-committee-votes",  "false",
        "--voting-stat-track-worker-votes",     "false",

        "--es-objects-bulk-replay",             "1",
        "--es-objects-proposals",               "false",
        "--es-objects-accounts",                "false",
        "--es-objects-assets",                  "false",
        "--es-objects-balances",                "false",
        "--es-objects-limit-orders",            "false",
        "--es-objects-asset-bitasset",          "false",
        "--es-objects-keep-only-current",       "true",
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );

    auto objects_deleted = graphene::utilities::deleteAll(_es);
    if( !objects_deleted )
        BOOST_FAIL( "elastic search DB could not be deleted" );


    ACTORS( (alice)(bob)(charlie) );
    transfer( committee_account, alice_id, asset(1) );
    transfer( committee_account, bob_id, asset(2) );
    transfer( committee_account, charlie_id, asset(3) );

    // proxy chain: alice => bob => charlie
    set_account_options( alice_id, bob_id );
    set_account_options( bob_id, charlie_id );
    set_account_options( charlie_id );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( alice_stat.has_proxy() );
        BOOST_CHECK( alice_stat.proxy == bob_id );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == 0 );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy == charlie_id );
        auto alice_proxied = *bob_stat.proxy_for.begin();
        BOOST_CHECK( alice_proxied.first == alice_id && alice_proxied.second == 1 );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 1 );

        BOOST_CHECK( !charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == (2 + 3) );
    }

    // proxy: alice => alice; bob => charlie;
    set_account_options( alice_id, GRAPHENE_PROXY_TO_SELF_ACCOUNT );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( !alice_stat.has_proxy() );
        BOOST_CHECK( alice_stat.proxy_for.empty() );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == 1 );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy_for.empty() );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );

        BOOST_CHECK( !charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == (2 + 3) );
    }

    // proxy: alice => alice; bob => charlie; charlie => alice; stake increase
    set_account_options( charlie_id, alice_id );
    transfer( committee_account, alice_id, asset(10) );
    transfer( committee_account, bob_id, asset(20) );
    transfer( committee_account, charlie_id, asset(30) );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( !alice_stat.has_proxy() );
        auto charlie_proxied = *alice_stat.proxy_for.begin();
        BOOST_CHECK( charlie_proxied.first == charlie_id && charlie_proxied.second == 33 );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == (11 + 33) );

        BOOST_CHECK( bob_stat.has_proxy() );
        BOOST_CHECK( bob_stat.proxy_for.empty() );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );
        BOOST_CHECK( bob_stat.stake == 22 );

        BOOST_CHECK( charlie_stat.has_proxy() );
        auto bob_proxied = *charlie_stat.proxy_for.begin();
        BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 22 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == 22 );
    }

    // only stake increase
    transfer( committee_account, alice_id, asset(100) );
    transfer( committee_account, bob_id, asset(200) );
    transfer( committee_account, charlie_id, asset(300) );

    make_next_maintenance_interval();
    {
        const auto& alice_stat   = get_voting_statistics_object( alice_id );
        const auto& bob_stat     = get_voting_statistics_object( bob_id );
        const auto& charlie_stat = get_voting_statistics_object( charlie_id );

        BOOST_CHECK( alice_stat.stake == 111 );
        BOOST_CHECK( alice_stat.get_total_voting_stake() == (111 + 333) );
        BOOST_CHECK( bob_stat.stake == 222 );
        BOOST_CHECK( bob_stat.get_total_voting_stake() == 0 );
        BOOST_CHECK( charlie_stat.stake == 333 );
        BOOST_CHECK( charlie_stat.get_total_voting_stake() == 222 );
    }

    fc::usleep(fc::milliseconds(2000));
    string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
    _es.endpoint = _es.index_prefix + "*/data/_count";
    _es.query = query;
    auto res = graphene::utilities::simpleQuery(_es);
    variant j = fc::json::from_string(res);
    BOOST_CHECK( j["count"].as_int64() == 12 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( test_voteable_objects_tracking_with_es )
{ try {

    bpo::options_description cli_vs;
    bpo::options_description cli_es;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli_vs, cfg );

    auto es_objects = app.get_plugin<es_objects_plugin>("es_objects");
    es_objects->plugin_set_program_options( cli_es, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "false",
        "--voting-stat-track-worker-votes",     "true",
        "--voting-stat-track-witness-votes",    "true",
        "--voting-stat-track-committee-votes",  "true",

        "--es-objects-voting-statistics",       "true",
        "--es-objects-voteable-statistics",     "true",
        "--es-objects-statistics-delete-allowed", "false",

        "--es-objects-bulk-replay",             "1",
        "--es-objects-proposals",               "false",
        "--es-objects-accounts",                "false",
        "--es-objects-assets",                  "false",
        "--es-objects-balances",                "false",
        "--es-objects-limit-orders",            "false",
        "--es-objects-asset-bitasset",          "false",
        "--es-objects-keep-only-current",       "true",
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );

    auto objects_deleted = graphene::utilities::deleteAll(_es);
    if( !objects_deleted )
        BOOST_FAIL( "elastic search DB could not be deleted" );


    ACTOR( alice );
    uint64_t alice_stake = 100;
    upgrade_to_lifetime_member( alice_id );
    transfer( committee_account, alice_id, asset(alice_stake) );
    set_account_options( alice_id );

    create_worker( alice_id );
    create_worker( alice_id );


    const auto& voteable_idx = db.get_index_type<voteable_statistics_index>().indices().get<by_block_number>();
    const auto& witness_idx = db.get_index_type<witness_index>().indices().get<by_id>();
    const auto& committee_idx = db.get_index_type<committee_member_index>().indices().get<by_id>();
    const auto& worker_idx = db.get_index_type<worker_index>().indices().get<by_id>();

    uint64_t num_witnesses = witness_idx.size();
    uint64_t num_committee_members = committee_idx.size();
    uint64_t num_workers = worker_idx.size();

    uint64_t expected_voteables = num_witnesses + num_committee_members + num_workers;

    make_next_maintenance_interval();
    BOOST_CHECK( voteable_idx.size() == expected_voteables );
    auto last_block = voteable_idx.rbegin()->block_number;
    auto default_voteable_votes = voteable_idx.equal_range( boost::make_tuple( last_block, default_vote_id) )
        .first->get_votes();
    BOOST_CHECK( default_voteable_votes == alice_stake );

    make_next_maintenance_interval();
    BOOST_CHECK( voteable_idx.size() == expected_voteables );


    auto expected_objs_in_es = 2*(expected_voteables + 1);

    fc::usleep(fc::milliseconds(2000));
    string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
    _es.endpoint = _es.index_prefix + "*/data/_count";
    _es.query = query;
    auto res = graphene::utilities::simpleQuery(_es);
    variant j = fc::json::from_string(res);
    uint64_t obj_count = j["count"].as_uint64();
    BOOST_CHECK( obj_count == expected_objs_in_es );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( test_voting_stat_plugin_track_every_x_interval )
{ try {

    bpo::options_description cli;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "2",
        "--voting-stat-keep-objects-in-db",     "true"
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );


    ACTOR( alice );
    set_account_options( alice_id );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    const auto& alice_stat1 = get_voting_statistics_object( alice_id );
    BOOST_CHECK( alice_stat1.stake == 1 );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    const auto& alice_stat2 = get_voting_statistics_object( alice_id );
    // since this interval is even it should not be tracked
    BOOST_CHECK( alice_stat2.stake == 1 );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    const auto& alice_stat3 = get_voting_statistics_object( alice_id );
    // this should result in the correct object since all odd intervals are tracked
    BOOST_CHECK( alice_stat3.stake == 3 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( test_delete_after_interval_and_pushed_to_es )
{ try {

    bpo::options_description cli_vs;
    bpo::options_description cli_es;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli_vs, cfg );

    auto es_objects = app.get_plugin<es_objects_plugin>("es_objects");
    es_objects->plugin_set_program_options( cli_es, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "false",

        "--es-objects-voting-statistics",       "true",
        "--es-objects-voteable-statistics",     "false",
        "--es-objects-statistics-delete-allowed", "false",

        "--es-objects-bulk-replay",             "1",
        "--es-objects-proposals",               "false",
        "--es-objects-accounts",                "false",
        "--es-objects-assets",                  "false",
        "--es-objects-balances",                "false",
        "--es-objects-limit-orders",            "false",
        "--es-objects-asset-bitasset",          "false",
        "--es-objects-keep-only-current",       "true",
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );

    auto objects_deleted = graphene::utilities::deleteAll(_es);
    if( !objects_deleted )
        BOOST_FAIL( "elastic search DB could not be deleted" );


    const auto& idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();
    ACTOR( alice );
    set_account_options( alice_id );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    BOOST_CHECK( idx.size() == 1 );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    BOOST_CHECK( idx.size() == 1 );


    transfer( committee_account, alice_id, asset(1) );
    make_next_maintenance_interval();
    BOOST_CHECK( idx.size() == 1 );


    fc::usleep(fc::milliseconds(2000));
    string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
    _es.endpoint = _es.index_prefix + "*/data/_count";
    _es.query = query;
    auto res = graphene::utilities::simpleQuery(_es);
    variant j = fc::json::from_string(res);
    int64_t obj_count = j["count"].as_int64();
    BOOST_CHECK( obj_count == 3 );

} FC_LOG_AND_RETHROW() }

// TODO REMOVE
BOOST_AUTO_TEST_CASE( test_indices )
{ try {

    bpo::options_description cli_vs;
    bpo::options_description cli_es;
    bpo::options_description cfg;

    auto voting_stat = app.get_plugin<voting_stat_plugin>("voting_stat");
    voting_stat->plugin_set_program_options( cli_vs, cfg );

    const char* const plugin_argv[]{ "voting_stat",
        "--voting-stat-track-every-x-maint",    "1",
        "--voting-stat-keep-objects-in-db",     "true",
    };
    int plugin_argc = sizeof(plugin_argv)/sizeof(char*);

    bpo::variables_map var_map;
    bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfg ), var_map );
    app.initialize_plugins( var_map );


    ACTORS((alice)(bob));

    transfer( committee_account, alice_id, asset(1) );
    transfer( committee_account, bob_id,   asset(1) );
    make_next_maintenance_interval();

    transfer( committee_account, alice_id, asset(1) );
    transfer( committee_account, bob_id,   asset(1) );
    make_next_maintenance_interval();

    transfer( committee_account, alice_id, asset(1) );
    transfer( committee_account, bob_id,   asset(1) );
    make_next_maintenance_interval();

    const auto& block_idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();
    edump(("BLOCK"));
    for( const auto& o : block_idx )
        edump((o.block_number)(o.account));

    auto beg = block_idx.lower_bound( block_idx.begin()->block_number );
    auto end = block_idx.upper_bound( block_idx.begin()->block_number );

    edump(("BLOCK BOUND"));
    for(; beg != end; ++beg )
        edump((beg->block_number)(beg->account));


    edump(("RANGEPLAY"));
    auto last_block = block_idx.begin()->block_number;
    auto block_range = block_idx.equal_range( last_block );
    for( auto o = block_range.first; o != block_range.second; ++o ) {
        edump((o->block_number)(o->account));
    }

    for( const auto& o : block_idx )
        db.remove(o);

    auto range = block_idx.equal_range( boost::make_tuple( last_block, alice_id ) );
    edump((range.first==range.second));

    uint64_t delt = std::distance( range.first, range.second );
    edump((delt));

    //edump((range.first->block_number)(range.first->account));

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
