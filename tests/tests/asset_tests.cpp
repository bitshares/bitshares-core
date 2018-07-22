/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
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
#include <string>
#include <cmath>

#include <graphene/chain/asset_object.hpp>
#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

BOOST_FIXTURE_TEST_SUITE(asset_tests, database_fixture)

BOOST_AUTO_TEST_CASE( asset_to_from_string )
{
   std::string positive_results[19];
   positive_results[0]  = "12345";
   positive_results[1]  = "1234.5";
   positive_results[2]  = "123.45";
   positive_results[3]  = "12.345";
   positive_results[4]  = "1.2345";
   positive_results[5]  = "0.12345";
   positive_results[6]  = "0.012345";
   positive_results[7]  = "0.0012345";
   positive_results[8]  = "0.00012345";
   positive_results[9]  = "0.000012345";
   positive_results[10] = "0.0000012345";
   positive_results[11] = "0.00000012345";
   positive_results[12] = "0.000000012345";
   positive_results[13] = "0.0000000012345";
   positive_results[14] = "0.00000000012345";
   positive_results[15] = "0.000000000012345";
   positive_results[16] = "0.0000000000012345";
   positive_results[17] = "0.00000000000012345";
   positive_results[18] = "0.000000000000012345";
   std::string negative_results[19];
   for(int i = 0; i < 19; ++i)
   {
      negative_results[i] = "-" + positive_results[i];
   }
   graphene::chain::asset_object test_obj;
   graphene::chain::share_type amt12345 = 12345;
   BOOST_TEST_MESSAGE( "Testing positive numbers" );
   for (int i = 0; i < 19; i++)
   {
      test_obj.precision = i;
      BOOST_CHECK_EQUAL(positive_results[i], test_obj.amount_to_string(amt12345));
   }
   BOOST_TEST_MESSAGE( "Testing negative numbers" );
   for (int i = 0; i < 19; i++)
   {
      test_obj.precision = i;
      BOOST_CHECK_EQUAL(negative_results[i], test_obj.amount_to_string(amt12345 * -1));
   }
}

BOOST_AUTO_TEST_CASE( asset_holders )
{

   graphene::app::asset_api asset_api(db);

   // create an asset and some accounts
   create_bitasset("USD", account_id_type());
   auto dan = create_account("dan");
   auto bob = create_account("bob");
   auto alice = create_account("alice");

   transfer(account_id_type()(db), dan, asset(100));
   transfer(account_id_type()(db), alice, asset(200));
   transfer(account_id_type()(db), bob, asset(300));

   vector<account_asset_balance> holders = asset_api.get_asset_holders(asset_id_type(), 0, 100);
   BOOST_CHECK_EQUAL(holders.size(), 4);
   // we can only guarantee the order if compiler flag is on
   #ifdef ASSET_BALANCE_SORTED 
      BOOST_CHECK(holders[0].name == "committee-account");
      BOOST_CHECK(holders[1].name == "bob");
      BOOST_CHECK(holders[2].name == "alice");
      BOOST_CHECK(holders[3].name == "dan");
   #endif

}

BOOST_AUTO_TEST_SUITE_END()
