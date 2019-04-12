/*
 * Copyright (c) 2018 manikey123, and contributors.
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

#include <graphene/chain/asset_object.hpp>
#include <graphene/app/api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;

BOOST_FIXTURE_TEST_SUITE(grouped_orders_api_tests, database_fixture)
BOOST_AUTO_TEST_CASE(api_limit_get_grouped_limit_orders) {
   try
   {
   app.enable_plugin("grouped_orders");
   graphene::app::orders_api orders_api(app);
   optional< api_access_info > acc;
   optional<price> start;

	//account_id_type() do 3 ops
   create_bitasset("USD", account_id_type());
   create_account("dan");
   create_account("bob");
   asset_id_type bit_jmj_id = create_bitasset("JMJBIT").id;
   generate_block();
   fc::usleep(fc::milliseconds(100));
   GRAPHENE_CHECK_THROW(orders_api.get_grouped_limit_orders(std::string( static_cast<object_id_type>(asset_id_type())), std::string( static_cast<object_id_type>(asset_id_type())),10, start,260), fc::exception);
   vector< limit_order_group > orders =orders_api.get_grouped_limit_orders(std::string( static_cast<object_id_type>(asset_id_type())), std::string( static_cast<object_id_type>(bit_jmj_id)), 10,start,240);
   BOOST_REQUIRE_EQUAL( orders.size(), 0u);
   }catch (fc::exception &e)
   {
    edump((e.to_detail_string()));
    throw;
   }
}
BOOST_AUTO_TEST_SUITE_END()
