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
#include <graphene/app/application.hpp>
#include <graphene/app/plugin.hpp>

#include <graphene/chain/key_object.hpp>
#include <graphene/time/time.hpp>

#include <graphene/account_history/account_history_plugin.hpp>

#include <fc/thread/thread.hpp>

#include <boost/filesystem/path.hpp>

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

using namespace graphene;

BOOST_AUTO_TEST_CASE( two_node_network )
{
   using namespace graphene::chain;
   using namespace graphene::app;
   try {
      fc::temp_directory app_dir;
      fc::temp_directory app2_dir;
      fc::temp_file genesis_json;
      fc::json::save_to_file(genesis_allocation(), genesis_json.path());

      fc::time_point_sec now( GRAPHENE_GENESIS_TIMESTAMP );

      graphene::app::application app1;
      app1.register_plugin<graphene::account_history::account_history_plugin>();
      bpo::variables_map cfg;
      cfg.emplace("p2p-endpoint", bpo::variable_value(string("127.0.0.1:3939"), false));
      cfg.emplace("genesis-json", bpo::variable_value(boost::filesystem::path(genesis_json.path()), false));
      app1.initialize(app_dir.path(), cfg);

      graphene::app::application app2;
      app2.register_plugin<account_history::account_history_plugin>();
      auto cfg2 = cfg;
      cfg2.erase("p2p-endpoint");
      cfg2.emplace("p2p-endpoint", bpo::variable_value(string("127.0.0.1:4040"), false));
      cfg2.emplace("seed-node", bpo::variable_value(vector<string>{"127.0.0.1:3939"}, false));
      app2.initialize(app2_dir.path(), cfg2);

      app1.startup();
      app2.startup();
      fc::usleep(fc::milliseconds(500));

      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 1);
      BOOST_CHECK_EQUAL(std::string(app1.p2p_node()->get_connected_peers().front().host.get_address()), "127.0.0.1");
      ilog("Connected!");

      fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
      fc::ecc::private_key genesis_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));
      graphene::chain::signed_transaction trx;
      trx.set_expiration(now + fc::seconds(30));
      std::shared_ptr<chain::database> db2 = app2.chain_database();

      trx.operations.push_back(key_create_operation({asset(), account_id_type(1), public_key_type(nathan_key.get_public_key())}));
      trx.validate();
      trx.sign(key_id_type(0), genesis_key);
      processed_transaction ptrx = app1.chain_database()->push_transaction(trx);
      app1.p2p_node()->broadcast(graphene::net::trx_message(trx));
      key_id_type nathan_key_id = ptrx.operation_results.front().get<object_id_type>();

      fc::usleep(fc::milliseconds(250));
      BOOST_CHECK(nathan_key_id(*app2.chain_database()).key_data.get<public_key_type>() == nathan_key.get_public_key());
      ilog("Pushed transaction");

      now += GRAPHENE_DEFAULT_BLOCK_INTERVAL;
      app2.p2p_node()->broadcast(graphene::net::block_message(db2->generate_block(
         now, db2->get_scheduled_witness( 1 ).first, genesis_key )));

      fc::usleep(fc::milliseconds(500));
      BOOST_CHECK_EQUAL(app1.p2p_node()->get_connection_count(), 1);
      BOOST_CHECK_EQUAL(app1.chain_database()->head_block_num(), 1);
   } catch( fc::exception& e ) {
      edump((e.to_detail_string()));
      throw;
   }
}
