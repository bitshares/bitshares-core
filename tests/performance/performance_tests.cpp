/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/db/simple_index.hpp>

#include <fc/crypto/digest.hpp>
#include "../common/database_fixture.hpp"

using namespace graphene::chain;

//BOOST_FIXTURE_TEST_SUITE( performance_tests, database_fixture )

BOOST_AUTO_TEST_CASE( sigcheck_benchmark )
{
   fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
   auto digest = fc::sha256::hash("hello");
   auto sig = nathan_key.sign_compact( digest );
   auto start = fc::time_point::now();
   for( uint32_t i = 0; i < 100000; ++i )
      auto pub = fc::ecc::public_key( sig, digest );
   auto end = fc::time_point::now();
   auto elapsed = end-start;
   wdump( ((100000.0*1000000.0) / elapsed.count()) );
}
/*
BOOST_AUTO_TEST_CASE( transfer_benchmark )
{
   fc::ecc::private_key nathan_key = fc::ecc::private_key::generate();
   const key_object& key = register_key(nathan_key.get_public_key());
   const auto& committee_account = account_id_type()(db);
   auto start = fc::time_point::now();
   for( uint32_t i = 0; i < 1000*1000; ++i )
   {
      const auto& a = create_account("a"+fc::to_string(i), key.id);
      transfer( committee_account, a, asset(1000) );
   }
   auto end = fc::time_point::now();
   auto elapsed = end - start;
   wdump( (elapsed) );
}
*/

//BOOST_AUTO_TEST_SUITE_END()

//#define BOOST_TEST_MODULE "C++ Unit Tests for Graphene Blockchain Database"
#include <cstdlib>
#include <iostream>
#include <boost/test/included/unit_test.hpp>

boost::unit_test::test_suite* init_unit_test_suite(int argc, char* argv[]) {
   std::srand(time(NULL));
   std::cout << "Random number generator seeded to " << time(NULL) << std::endl;
   return nullptr;
}
