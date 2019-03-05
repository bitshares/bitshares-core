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

#include <graphene/app/api.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/utilities/tempdir.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;
BOOST_FIXTURE_TEST_SUITE( history_api_tests, database_fixture )

BOOST_AUTO_TEST_CASE(get_account_history) {
   try {
      graphene::app::history_api hist_api(app);

      //account_id_type() do 3 ops
      create_bitasset("USD", account_id_type());
      create_account("dan");
      create_account("bob");


      generate_block();
      fc::usleep(fc::milliseconds(2000));

      int asset_create_op_id = operation::tag<asset_create_operation>::value;
      int account_create_op_id = operation::tag<account_create_operation>::value;

      //account_id_type() did 3 ops and includes id0
      vector<operation_history_object> histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 100, operation_history_id_type());

      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);
      BOOST_CHECK_EQUAL(histories[2].op.which(), asset_create_op_id);

      // 1 account_create op larger than id1
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 100, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK(histories[0].id.instance() != 0);
      BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);


      // Limit 2 returns 2 result
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 2, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK(histories[1].id.instance() != 0);
      BOOST_CHECK_EQUAL(histories[1].op.which(), account_create_op_id);
      // bob has 1 op
      histories = hist_api.get_account_history("bob", operation_history_id_type(), 100, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);


   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(get_account_history_additional) {
   try {
      graphene::app::history_api hist_api(app);

      // A = account_id_type() with records { 5, 3, 1, 0 }, and
      // B = dan with records { 6, 4, 2, 1 }
      // account_id_type() and dan share operation id 1(account create) - share can be also in id 0

      // no history at all in the chain
      vector<operation_history_object> histories = hist_api.get_account_history("1.2.0", operation_history_id_type(0), 4, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      create_bitasset("USD", account_id_type()); // create op 0
      generate_block();
      // what if the account only has one history entry and it is 0?
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 0u);

      const account_object& dan = create_account("dan"); // create op 1

      create_bitasset("CNY", dan.id); // create op 2
      create_bitasset("BTC", account_id_type()); // create op 3
      create_bitasset("XMR", dan.id); // create op 4
      create_bitasset("EUR", account_id_type()); // create op 5
      create_bitasset("OIL", dan.id); // create op 6

      generate_block();

      // f(A, 0, 4, 9) = { 5, 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

      // f(A, 0, 4, 6) = { 5, 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(6));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

      // f(A, 0, 4, 5) = { 5, 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(5));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

      // f(A, 0, 4, 4) = { 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(4));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

      // f(A, 0, 4, 3) = { 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(3));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

      // f(A, 0, 4, 2) = { 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

      // f(A, 0, 4, 1) = { 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

      // f(A, 0, 4, 0) = { 5, 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 4, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

      // f(A, 1, 5, 9) = { 5, 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

      // f(A, 1, 5, 6) = { 5, 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(6));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

      // f(A, 1, 5, 5) = { 5, 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(5));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

      // f(A, 1, 5, 4) = { 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(4));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

      // f(A, 1, 5, 3) = { 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(3));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

      // f(A, 1, 5, 2) = { }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(A, 1, 5, 1) = { }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(A, 1, 5, 0) = { 5, 3 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 5, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

      // f(A, 0, 3, 9) = { 5, 3, 1 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(A, 0, 3, 6) = { 5, 3, 1 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(6));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(A, 0, 3, 5) = { 5, 3, 1 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(5));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(A, 0, 3, 4) = { 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(4));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

      // f(A, 0, 3, 3) = { 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(3));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 0u);

      // f(A, 0, 3, 2) = { 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

      // f(A, 0, 3, 1) = { 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

      // f(A, 0, 3, 0) = { 5, 3, 1 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(), 3, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(B, 0, 4, 9) = { 6, 4, 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

      // f(B, 0, 4, 6) = { 6, 4, 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(6));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

      // f(B, 0, 4, 5) = { 4, 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(5));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(B, 0, 4, 4) = { 4, 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(4));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);

      // f(B, 0, 4, 3) = { 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(3));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

      // f(B, 0, 4, 2) = { 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 1u);

      // f(B, 0, 4, 1) = { 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 1u);

      // f(B, 0, 4, 0) = { 6, 4, 2, 1 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(), 4, operation_history_id_type());
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);

      // f(B, 2, 4, 9) = { 6, 4 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

      // f(B, 2, 4, 6) = { 6, 4 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(6));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

      // f(B, 2, 4, 5) = { 4 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(5));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);

      // f(B, 2, 4, 4) = { 4 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(4));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);

      // f(B, 2, 4, 3) = { }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(3));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(B, 2, 4, 2) = { }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(B, 2, 4, 1) = { }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(B, 2, 4, 0) = { 6, 4 }
      histories = hist_api.get_account_history("dan", operation_history_id_type(2), 4, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);

      // 0 limits
      histories = hist_api.get_account_history("dan", operation_history_id_type(0), 0, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(3), 0, operation_history_id_type(9));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // non existent account
      histories = hist_api.get_account_history("1.2.18", operation_history_id_type(0), 4, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // create a new account C = alice { 7 }
      create_account("alice");

      generate_block();

      // f(C, 0, 4, 10) = { 7 }
      histories = hist_api.get_account_history("alice", operation_history_id_type(0), 4, operation_history_id_type(10));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);

      // f(C, 8, 4, 10) = { }
      histories = hist_api.get_account_history("alice", operation_history_id_type(8), 4, operation_history_id_type(10));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // f(A, 0, 10, 0) = { 7, 5, 3, 1, 0 }
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 5u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 7u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 5u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[4].id.instance(), 0u);

   }
   catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(track_account) {
   try {
      graphene::app::history_api hist_api(app);

      // account_id_type() is not tracked

      // account_id_type() creates alice(not tracked account)
      create_account("alice");

      //account_id_type() creates some ops
      create_bitasset("CNY", account_id_type());
      create_bitasset("USD", account_id_type());

      // account_id_type() creates dan(account tracked)
      const account_object& dan = create_account("dan");
      auto dan_id = dan.id;

      // dan makes 1 op
      create_bitasset("EUR", dan_id);

      generate_block();

      // anything against account_id_type() should be {}
      vector<operation_history_object> histories = hist_api.get_account_history("1.2.0", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("1.2.0", operation_history_id_type(1), 1, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // anything against alice should be {}
      histories = hist_api.get_account_history("alice", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("alice", operation_history_id_type(1), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("alice", operation_history_id_type(1), 1, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // dan should have history
      histories = hist_api.get_account_history("dan", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 3u);

      // create more ops, starting with an untracked account
      create_bitasset( "BTC", account_id_type() );
      create_bitasset( "GBP", dan_id );

      generate_block();

      histories = hist_api.get_account_history("dan", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 3u);

      db.pop_block();

      // Try again, should result in same object IDs
      create_bitasset( "BTC", account_id_type() );
      create_bitasset( "GBP", dan_id );

      generate_block();

      histories = hist_api.get_account_history("dan", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 3u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 6u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 3u);
   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}
BOOST_AUTO_TEST_CASE(track_account2) {
   try {
      graphene::app::history_api hist_api(app);

      // account_id_type() is tracked

      // account_id_type() creates alice(tracked account)
      const account_object& alice = create_account("alice");
      auto alice_id = alice.id;

      //account_id_type() creates some ops
      create_bitasset("CNY", account_id_type());
      create_bitasset("USD", account_id_type());

      // alice makes 1 op
      create_bitasset("EUR", alice_id);

      // account_id_type() creates dan(account not tracked)
      create_account("dan");

      generate_block();

      // all account_id_type() should have 4 ops {4,2,1,0}
      vector<operation_history_object> histories = hist_api.get_account_history("committee-account", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 4u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 4u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 2u);
      BOOST_CHECK_EQUAL(histories[2].id.instance(), 1u);
      BOOST_CHECK_EQUAL(histories[3].id.instance(), 0u);

      // all alice account should have 2 ops {3, 0}
      histories = hist_api.get_account_history("alice", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);
      BOOST_CHECK_EQUAL(histories[1].id.instance(), 0u);

      // alice first op should be {0}
      histories = hist_api.get_account_history("alice", operation_history_id_type(0), 1, operation_history_id_type(1));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 0u);

      // alice second op should be {3}
      histories = hist_api.get_account_history("alice", operation_history_id_type(1), 1, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 3u);

      // anything against dan should be {}
      histories = hist_api.get_account_history("dan", operation_history_id_type(0), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("dan", operation_history_id_type(1), 10, operation_history_id_type(0));
      BOOST_CHECK_EQUAL(histories.size(), 0u);
      histories = hist_api.get_account_history("dan", operation_history_id_type(1), 1, operation_history_id_type(2));
      BOOST_CHECK_EQUAL(histories.size(), 0u);

   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(get_account_history_operations) {
   try {
      graphene::app::history_api hist_api(app);

      //account_id_type() do 3 ops
      create_bitasset("CNY", account_id_type());
      create_account("sam");
      create_account("alice");

      generate_block();
      fc::usleep(fc::milliseconds(2000));

      int asset_create_op_id = operation::tag<asset_create_operation>::value;
      int account_create_op_id = operation::tag<account_create_operation>::value;

      //account_id_type() did 1 asset_create op
      vector<operation_history_object> histories = hist_api.get_account_history_operations(
            "committee-account", asset_create_op_id, operation_history_id_type(), operation_history_id_type(), 100);
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].id.instance(), 0u);
      BOOST_CHECK_EQUAL(histories[0].op.which(), asset_create_op_id);

      //account_id_type() did 2 account_create ops
      histories = hist_api.get_account_history_operations(
            "committee-account", account_create_op_id, operation_history_id_type(), operation_history_id_type(), 100);
      BOOST_CHECK_EQUAL(histories.size(), 2u);
      BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);

      // No asset_create op larger than id1
      histories = hist_api.get_account_history_operations(
            "committee-account", asset_create_op_id, operation_history_id_type(), operation_history_id_type(1), 100);
      BOOST_CHECK_EQUAL(histories.size(), 0u);

      // Limit 1 returns 1 result
      histories = hist_api.get_account_history_operations(
            "committee-account", account_create_op_id, operation_history_id_type(),operation_history_id_type(), 1);
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);

      // alice has 1 op
      histories = hist_api.get_account_history_operations(
         "alice", account_create_op_id, operation_history_id_type(),operation_history_id_type(), 100);
      BOOST_CHECK_EQUAL(histories.size(), 1u);
      BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);

      // create a bunch of accounts
      for(int i = 0; i < 80; ++i)
      {
         std::string acct_name = "mytempacct" + std::to_string(i);
         create_account(acct_name);
      }
      generate_block();

      // history is set to limit transactions to 75 (see database_fixture.hpp)
      // so asking for more should only return 75 (and not throw exception, 
      // see https://github.com/bitshares/bitshares-core/issues/1490
      histories = hist_api.get_account_history_operations(
            "committee-account", account_create_op_id, operation_history_id_type(), operation_history_id_type(), 100);
      BOOST_CHECK_EQUAL(histories.size(), 75u);
      if (histories.size() > 0)
         BOOST_CHECK_EQUAL(histories[0].op.which(), account_create_op_id);
      

   } catch (fc::exception &e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( market_history )
{
   class simple_tx
   {
      public:
      simple_tx() {}
      simple_tx(
         const graphene::chain::account_object& from, 
         const graphene::chain::account_object& to, 
         const graphene::chain::price& amount)
         : from(from), to(to), amount(amount) {}
      const graphene::chain::account_object to;
      const graphene::chain::account_object from;
      const graphene::chain::price amount;
   };
   class time_series
   {
      public:
      time_series() {}
      time_series(uint32_t secs, std::vector<simple_tx> transactions) : secs(secs), transactions(transactions) {}
      uint32_t secs;
      std::vector<simple_tx> transactions;
   };
   try {
      app.enable_plugin("market_history");
      graphene::app::history_api hist_api( app );

      // create the needed things on the chain
      ACTORS( (bob) (alice) );
      transfer( committee_account, alice.id, asset(1000000000) );
      transfer( committee_account, bob.id  , asset(1000000000) );
      const graphene::chain::asset_object& usd = create_bitasset( "USD", account_id_type() );
      // set up feed producers
      {
         asset_update_feed_producers_operation op;
         op.asset_to_update = usd.id;
         op.issuer = committee_account;
         op.new_feed_producers = {committee_account, alice.id, bob.id};
         trx.operations.push_back(op);
         sign( trx, private_key );
         PUSH_TX( db, trx, ~0 );
         generate_block();
         trx.clear();
         publish_feed( committee_account, asset_id_type(), 2, usd.id, 1, asset_id_type() );
         publish_feed( alice.id, asset_id_type(), 1, usd.id, 2, asset_id_type() );
         publish_feed( bob.id, asset_id_type(), 2, usd.id, 1, asset_id_type() );
      }
      const graphene::chain::asset_object& cny = create_bitasset( "CNY", account_id_type() );
      // set up feed producers
      {
         asset_update_feed_producers_operation op;
         op.asset_to_update = cny.id;
         op.issuer = committee_account;
         op.new_feed_producers = {committee_account, alice.id, bob.id};
         trx.operations.push_back(op);
         sign( trx, private_key );
         PUSH_TX( db, trx, ~0 );
         generate_block();
         trx.clear();
         publish_feed( committee_account, asset_id_type(), 1, cny.id, 2, asset_id_type() );
         publish_feed( alice.id, asset_id_type(), 1, cny.id, 2, asset_id_type() );
         publish_feed( bob.id, asset_id_type(), 2, cny.id, 1, asset_id_type() );
      }
      generate_blocks( db.get_dynamic_global_properties().next_maintenance_time );
      borrow( alice, asset(100000, usd.id), asset(1000000) );
      borrow( bob  , asset(100000, usd.id), asset(1000000) );
      borrow( alice, asset(100000, cny.id), asset(1000000) );
      borrow( bob  , asset(100000, cny.id), asset(1000000) );

      // get bucket size
      boost::container::flat_set<uint32_t> bucket_sizes = hist_api.get_market_history_buckets();
      uint32_t bucket_size = *bucket_sizes.begin(); // 15 secs when I checked.

      // make some transaction data
      uint32_t current_price = 5000;

      // a function to transmit a transaction
      auto transmit_tx = [this, alice, bob, alice_private_key, bob_private_key](const simple_tx& tx) {
         const auto seller_private_key = ( tx.from.id == alice.id ? alice_private_key : bob_private_key );
         const auto buyer_private_key  = ( tx.from.id == alice.id ? bob_private_key : alice_private_key );
         // create an order
         graphene::chain::limit_order_create_operation limit;
         limit.amount_to_sell = tx.amount.base;
         limit.min_to_receive = tx.amount.quote;
         limit.fill_or_kill = false;
         limit.seller = tx.from.id;
         limit.expiration = fc::time_point_sec( fc::time_point::now().sec_since_epoch() + 60 );
         // send the order
         set_expiration( db, trx );
         trx.operations.push_back( limit );
         sign( trx, seller_private_key );
         PUSH_TX( db,  trx, ~0 );
         trx.clear();
         // send a second order that will fill the first
         graphene::chain::limit_order_create_operation buy;
         buy.amount_to_sell = tx.amount.quote;
         buy.min_to_receive = tx.amount.base;
         buy.fill_or_kill = false;
         buy.seller = tx.to.id;
         buy.expiration = limit.expiration;
         set_expiration( db, trx );
         trx.operations.push_back( buy );
         sign( trx, buyer_private_key );
         PUSH_TX( db,  trx, ~0 );
         trx.clear();
      };

      uint32_t num_bars = 10;
      std::vector<time_series> bars(num_bars);

      generate_blocks( HARDFORK_CORE_625_TIME );

      for( uint32_t i = 0; i < num_bars; ++i )  // each bucket
      {
         uint32_t num_transactions = std::rand() % 100;
         std::vector<simple_tx> trxs(num_transactions);
         for( int tx_num = 0; tx_num < num_transactions; ++tx_num )
         {
            bool alice_buys = std::rand() % 2;
            bool buy_usd = std::rand() % 2;
            current_price = ( current_price + ( std::rand() % 2 ? 1 : -1) );
            simple_tx stx(
               ( alice_buys ? alice : bob ),
               ( alice_buys ? bob : alice ),
               graphene::chain::price( 
                  graphene::chain::asset(1, (buy_usd ? cny.id : usd.id ) ), 
                  graphene::chain::asset(current_price, (buy_usd ? usd.id : cny.id ) )
               )
               );
            trxs.push_back(stx);
            // send matching buy and sell to the server
            transmit_tx(stx);
         }
         generate_block();
         bars.push_back( time_series(i, trxs) );
      }
      // grab history
      auto server_history = hist_api.get_market_history( 
            static_cast<std::string>(usd.id), // asset_id_type
            static_cast<std::string>(cny.id), // asset_id_type
            bucket_size, // bucket_seconds
            fc::time_point_sec(0), // start
            fc::time_point_sec(db.head_block_time()) ); // end
      // compare what comes back with what we think we should have
      BOOST_CHECK_EQUAL( server_history.size(), 0 );
   } catch ( fc::exception &e ) {
      BOOST_FAIL( e.to_detail_string() );
   }
}

BOOST_AUTO_TEST_SUITE_END()
