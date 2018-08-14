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

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/db/simple_index.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"
#include <cstdlib>
#include <iostream>

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( performance_tests, database_fixture )

BOOST_AUTO_TEST_CASE( sigcheck_benchmark )
{
   fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
   auto digest = fc::sha256::hash("hello");
   auto sig = nathan_key.sign_compact( digest );
   auto start = fc::time_point::now();
   const uint32_t cycles = 100000;
   for( uint32_t i = 0; i < cycles; ++i )
      fc::ecc::public_key( sig, digest );
   auto end = fc::time_point::now();
   auto elapsed = end-start;
   wlog( "Benchmark: verify ${sps} signatures/s", ("sps",(cycles*1000000.0)/elapsed.count()) );
}

BOOST_AUTO_TEST_CASE( transfer_benchmark )
{
   const fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
   const fc::ecc::public_key  nathan_pub = nathan_key.get_public_key();;
   const auto& committee_account = account_id_type()(db);
   const uint32_t cycles = 100000;
   std::vector<const account_object*> accounts;
   accounts.reserve( cycles );
   auto start = fc::time_point::now();
   for( uint32_t i = 0; i < cycles; ++i )
   {
      const auto& a = create_account("a"+fc::to_string(i), nathan_pub);
      accounts[i] = &a;
   }
   auto end = fc::time_point::now();
   auto elapsed = end - start;
   wlog( "Create ${aps} accounts/s", ("aps",(cycles*1000000.0)/elapsed.count()) );

   start = fc::time_point::now();
   for( uint32_t i = 0; i < cycles; ++i )
      transfer( committee_account, *(accounts[i]), asset(1000) );
   end = fc::time_point::now();
   elapsed = end - start;
   wlog( "${aps} transfers/s", ("aps",(cycles*1000000.0)/elapsed.count()) );
}

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/included/unit_test.hpp>

boost::unit_test::test_suite* init_unit_test_suite(int argc, char* argv[]) {
   std::srand(time(NULL));
   std::cout << "Random number generator seeded to " << time(NULL) << std::endl;
   return nullptr;
}
