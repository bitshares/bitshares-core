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

#include <fc/optional.hpp>

#include <graphene/voting_stat/voting_stat_plugin.hpp>
#include <graphene/es_objects/es_objects.hpp>
#include <graphene/utilities/elasticsearch.hpp>

#include <graphene/app/api.hpp>

#include <graphene/chain/protocol/account.hpp>

#include <graphene/chain/voting_statistics_object.hpp>

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

            auto es_object = app.get_plugin<es_objects_plugin>("es_objects");
            bpo::options_description cli;
            bpo::options_description cfi;
            es_object->plugin_set_program_options( cli, cfi );

            bpo::variables_map var_map;

            int plugin_argc = 17;
            const char* const plugin_argv[]{ "voting_stat",
                "--es-objects-bulk-replay", "1",
                "--es-objects-proposals", "false",
                "--es-objects-accounts", "false",
                "--es-objects-assets", "false",
                "--es-objects-balances", "false",
                "--es-objects-limit-orders", "false",
                "--es-objects-asset-bitasset", "false",
                "--es-objects-keep-only-current", "false"
            };

            bpo::store( bpo::parse_command_line( plugin_argc, plugin_argv, cfi ), var_map );
            app.initialize_plugins( var_map ); 
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
        const auto& idx = db.get_index_type<voting_statistics_index>().indices().get<by_owner>();
        return *idx.find( id );
    }
};

BOOST_FIXTURE_TEST_SUITE( voting_stat_tests, voting_stat_fixture )

BOOST_AUTO_TEST_CASE( block_id_changes_with_each_interval )
{ try {

    make_next_maintenance_interval();
    auto first_block_id = voting_statistics_object::block_id;

    make_next_maintenance_interval();
    auto second_block_id = voting_statistics_object::block_id;
    BOOST_CHECK( first_block_id != second_block_id );

    make_next_maintenance_interval();
    auto third_block_id = voting_statistics_object::block_id;
    BOOST_CHECK( second_block_id != third_block_id );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( voting_statistics_without_proxy ) 
{ try {

    ACTOR( alice );
    transfer( committee_account, alice_id, asset(1) );
    set_account_options( alice_id );
    
    const auto& alice_acc = alice_id(db);
    BOOST_CHECK( *alice_acc.options.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_acc.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT );


    make_next_maintenance_interval();
    const auto& alice_stat1 = get_voting_statistics_object( alice_id );

    BOOST_CHECK( alice_stat1.proxy == GRAPHENE_PROXY_TO_SELF_ACCOUNT );
    BOOST_CHECK( !alice_stat1.has_proxy() );
    BOOST_CHECK( alice_stat1.proxy_for.empty() );
    BOOST_CHECK( alice_stat1.stake == 1 );
    BOOST_CHECK( *alice_stat1.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_stat1.get_total_voting_stake() == 1 );


    /* increase stake */
    transfer( committee_account, alice_id, asset(10) );

    make_next_maintenance_interval();
    const auto& alice_stat2 = get_voting_statistics_object( alice_id );

    BOOST_CHECK( alice_stat2.proxy == GRAPHENE_PROXY_TO_SELF_ACCOUNT );
    BOOST_CHECK( !alice_stat2.has_proxy() );
    BOOST_CHECK( alice_stat2.proxy_for.empty() );
    BOOST_CHECK( alice_stat2.stake == 11 );
    BOOST_CHECK( *alice_stat1.votes.begin() == default_vote_id );
    BOOST_CHECK( alice_stat1.get_total_voting_stake() == 11 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( voting_statistics_with_proxy ) 
{ try {
    /* clear es-db */
    auto objects_deleted = graphene::utilities::deleteAll(_es);
    if( !objects_deleted )
        BOOST_FAIL( "elastic search DB could not be deleted" );


    ACTORS( (alice)(bob)(charlie) );
    transfer( committee_account, alice_id, asset(1), asset() );
    transfer( committee_account, bob_id, asset(2), asset() );
    transfer( committee_account, charlie_id, asset(3), asset() );
    
    /* proxy chain: alice => bob => charlie */
    set_account_options( alice_id, bob_id );
    set_account_options( bob_id, charlie_id );
    set_account_options( charlie_id );
    
    make_next_maintenance_interval();
    const auto& alice_stat1   = get_voting_statistics_object( alice_id );
    const auto& bob_stat1     = get_voting_statistics_object( bob_id );
    const auto& charlie_stat1 = get_voting_statistics_object( charlie_id );

    BOOST_CHECK( alice_stat1.has_proxy() );
    BOOST_CHECK( alice_stat1.proxy == bob_id );
    BOOST_CHECK( alice_stat1.get_total_voting_stake() == 0 );

    BOOST_CHECK( bob_stat1.has_proxy() );
    BOOST_CHECK( bob_stat1.proxy == charlie_id );
    auto alice_proxied = *bob_stat1.proxy_for.begin();
    BOOST_CHECK( alice_proxied.first == alice_id && alice_proxied.second == 1 );
    BOOST_CHECK( bob_stat1.get_total_voting_stake() == 1 );
    
    BOOST_CHECK( !charlie_stat1.has_proxy() );
    auto bob_proxied = *charlie_stat1.proxy_for.begin();
    BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
    BOOST_CHECK( charlie_stat1.get_total_voting_stake() == (2 + 3) );


    /* proxy: alice => alice; bob => charlie; */
    set_account_options( alice_id, GRAPHENE_PROXY_TO_SELF_ACCOUNT );

    make_next_maintenance_interval();
    const auto& alice_stat2   = get_voting_statistics_object( alice_id );
    const auto& bob_stat2     = get_voting_statistics_object( bob_id );
    const auto& charlie_stat2 = get_voting_statistics_object( charlie_id );

    BOOST_CHECK( !alice_stat2.has_proxy() );
    BOOST_CHECK( alice_stat2.proxy_for.empty() );
    BOOST_CHECK( alice_stat2.get_total_voting_stake() == 1 );

    BOOST_CHECK( bob_stat2.has_proxy() );
    BOOST_CHECK( bob_stat2.proxy_for.empty() );
    BOOST_CHECK( bob_stat2.get_total_voting_stake() == 0 );

    BOOST_CHECK( !charlie_stat2.has_proxy() );
    bob_proxied = *charlie_stat2.proxy_for.begin();
    BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 2 );
    BOOST_CHECK( charlie_stat2.get_total_voting_stake() == (2 + 3) );


    /* proxy: alice => alice; bob => charlie; charlie => alice; stake increase */
    set_account_options( charlie_id, alice_id );
    transfer( committee_account, alice_id, asset(10) );
    transfer( committee_account, bob_id, asset(20) );
    transfer( committee_account, charlie_id, asset(30) );

    make_next_maintenance_interval();
    const auto& alice_stat3   = get_voting_statistics_object( alice_id );
    const auto& bob_stat3     = get_voting_statistics_object( bob_id );
    const auto& charlie_stat3 = get_voting_statistics_object( charlie_id );

    BOOST_CHECK( !alice_stat3.has_proxy() );
    auto charlie_proxied = *alice_stat3.proxy_for.begin();
    BOOST_CHECK( charlie_proxied.first == charlie_id && charlie_proxied.second == 33 );
    BOOST_CHECK( alice_stat3.get_total_voting_stake() == (11 + 33) );

    BOOST_CHECK( bob_stat3.has_proxy() );
    BOOST_CHECK( bob_stat3.proxy_for.empty() );
    BOOST_CHECK( bob_stat3.get_total_voting_stake() == 0 );

    BOOST_CHECK( charlie_stat3.has_proxy() );
    bob_proxied = *charlie_stat2.proxy_for.begin();
    BOOST_CHECK( bob_proxied.first == bob_id && bob_proxied.second == 22 );
    BOOST_CHECK( charlie_stat3.get_total_voting_stake() == 22 );


    /* only stake increase */
    transfer( committee_account, alice_id, asset(100) );
    transfer( committee_account, bob_id, asset(200) );
    transfer( committee_account, charlie_id, asset(300) );

    make_next_maintenance_interval();
    const auto& alice_stat4   = get_voting_statistics_object( alice_id );
    const auto& bob_stat4     = get_voting_statistics_object( bob_id );
    const auto& charlie_stat4 = get_voting_statistics_object( charlie_id );

    BOOST_CHECK( alice_stat4.stake == 111 );
    BOOST_CHECK( alice_stat4.get_total_voting_stake() == (111 + 333) ); 
    BOOST_CHECK( bob_stat4.stake == 222 );
    BOOST_CHECK( bob_stat4.get_total_voting_stake() == 0 );
    BOOST_CHECK( charlie_stat4.stake == 333 );
    BOOST_CHECK( charlie_stat4.get_total_voting_stake() == 222 ); 
    
    
    /* wait for es */
    fc::usleep(fc::milliseconds(1000));
    string query = "{ \"query\" : { \"bool\" : { \"must\" : [{\"match_all\": {}}] } } }";
    _es.endpoint = _es.index_prefix + "*/data/_count";
    _es.query = query;
    auto res = graphene::utilities::simpleQuery(_es);
    variant j = fc::json::from_string(res);
    BOOST_CHECK( j["count"].as_int64() == 12 );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
