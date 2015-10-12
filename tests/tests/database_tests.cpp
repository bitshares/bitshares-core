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

#include <graphene/chain/account_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_AUTO_TEST_CASE( undo_test )
{
   try {
      database db;
      auto ses = db._undo_db.start_undo_session();
      const auto& bal_obj1 = db.create<account_balance_object>( [&]( account_balance_object& obj ){
               /* no balances right now */
      });
      auto id1 = bal_obj1.id;
      // abandon changes
      ses.undo();
      // start a new session
      ses = db._undo_db.start_undo_session();

      const auto& bal_obj2 = db.create<account_balance_object>( [&]( account_balance_object& obj ){
               /* no balances right now */
      });
      auto id2 = bal_obj2.id;
      BOOST_CHECK( id1 == id2 );
   } catch ( const fc::exception& e )
   {
      edump( (e.to_detail_string()) );
      throw;
   }
}
