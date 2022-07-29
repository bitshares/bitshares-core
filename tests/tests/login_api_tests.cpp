/*
 * Copyright (c) 2022 Abit More, and contributors.
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

#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(login_api_tests, database_fixture)

BOOST_AUTO_TEST_CASE( get_config_test )
{ try {
   auto default_opt = graphene::app::application_options::get_default();
   auto opt = app.get_options();

   graphene::app::login_api login_api1( app );

   BOOST_CHECK( login_api1.get_info() == "Test API node" );

   BOOST_CHECK_THROW( login_api1.get_config(), fc::exception );

   login_api1.login("",""); // */*
   auto config = login_api1.get_config();

   BOOST_CHECK_EQUAL( default_opt.api_limit_get_call_orders, config.api_limit_get_call_orders );
   BOOST_CHECK_EQUAL( opt.api_limit_get_call_orders, config.api_limit_get_call_orders );

   BOOST_CHECK_EQUAL( default_opt.api_limit_get_full_accounts_subscribe, uint32_t(100) );
   BOOST_CHECK_EQUAL( opt.api_limit_get_full_accounts_subscribe, uint32_t(120) );
   BOOST_CHECK_EQUAL( config.api_limit_get_full_accounts_subscribe, uint32_t(120) );

} FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_CASE( login_test )
{ try {
   graphene::app::login_api login_api1( app );
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 0u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );

   login_api1.login("",""); // */*
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 3u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   auto db_api1 = login_api1.database();
   auto his_api1 = login_api1.history();
   auto nb_api1 = login_api1.network_broadcast();

   login_api1.login("user2","superpassword2");
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 1u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   BOOST_CHECK_THROW( login_api1.database(), fc::exception );
   auto his_api2 = login_api1.history();
   BOOST_CHECK( his_api1 == his_api2 );

   login_api1.login("user2","superpassword3"); // wrong password
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 0u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   BOOST_CHECK_THROW( login_api1.database(), fc::exception );
   BOOST_CHECK_THROW( login_api1.history(), fc::exception );

   login_api1.login("bytemaster","looooooooooooooooongpassword"); // wrong password
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 0u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   BOOST_CHECK_THROW( login_api1.database(), fc::exception );
   BOOST_CHECK_THROW( login_api1.history(), fc::exception );

   login_api1.login("bytemaster","supersecret");
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 10u );
   auto nn_api3 = login_api1.network_node();
   auto db_api3 = login_api1.database();
   auto his_api3 = login_api1.history();
   auto ord_api3 = login_api1.orders();
   auto nb_api3 = login_api1.network_broadcast();
   auto as_api3 = login_api1.asset();
   auto cr_api3 = login_api1.crypto();
   auto blk_api3 = login_api1.block();
   auto co_api3 = login_api1.custom_operations();
   auto dbg_api3 = login_api1.debug();
   BOOST_CHECK( his_api1 == his_api3 );

   login_api1.logout();
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 0u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   BOOST_CHECK_THROW( login_api1.database(), fc::exception );
   BOOST_CHECK_THROW( login_api1.history(), fc::exception );

   login_api1.login("bytemaster2","randompassword"); // */*
   BOOST_CHECK_EQUAL( login_api1.get_available_api_sets().size(), 3u );
   BOOST_CHECK_THROW( login_api1.network_node(), fc::exception );
   auto db_api4 = login_api1.database();
   auto his_api4 = login_api1.history();
   auto nb_api4 = login_api1.network_broadcast();
   BOOST_CHECK( his_api1 == his_api4 );

} FC_CAPTURE_LOG_AND_RETHROW( (0) ) }

BOOST_AUTO_TEST_SUITE_END()
