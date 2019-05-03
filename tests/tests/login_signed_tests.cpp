/*
 * Copyright (c) 2019 Blockchain Projects B.V.
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
#include <boost/assign/list_of.hpp>

#include <graphene/chain/protocol/transaction.hpp>

#include <graphene/app/api.hpp>

#include <fc/crypto/digest.hpp>
#include <fc/crypto/base64.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;


struct login_signed_fixture : public database_fixture
{

};

BOOST_FIXTURE_TEST_SUITE( login_signed_tests, login_signed_fixture )

BOOST_AUTO_TEST_CASE( fail_with_timestamp_too_fresh )
{ try {
   
   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60*60; // too far in the future
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE( fail_with_timestamp_too_old )
{ try {

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() - 60*60; // too far in the past
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fail_with_not_transfer_op_in_trx )
{ try {

   ACTOR( alice );
   account_update_operation op;
   op.account = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fail_with_empty_signature_keys )
{ try {

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   
   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fail_with_wrong_signature )
{ try {

   ACTORS( (alice) (bob) );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( bob_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fail_as_default_user_no_lifetime_member )
{ try {

   api_access_info_signed info;
   info.required_lifetime_member = true;
   info.required_registrar = "";
   info.allowed_apis = { "database_api" };

   app.set_api_access_info_signed_default( {info} );

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fail_as_default_user_no_required_registrar )
{ try {

   api_access_info_signed info;
   info.required_lifetime_member = false;
   info.required_registrar = "required_registrar_name";
   info.allowed_apis = { "database_api" };

   app.set_api_access_info_signed_default( {info} );

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( !logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( pass_as_default_user_no_specials )
{ try {

   api_access_info_signed info;
   info.required_lifetime_member = false;
   info.required_registrar = "";
   info.allowed_apis = { "database_api" };

   app.set_api_access_info_signed_default( {info} );

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( pass_as_default_user_with_lifetime_member )
{ try {

   api_access_info_signed info;
   info.required_lifetime_member = true;
   info.required_registrar = "";
   info.allowed_apis = { "database_api" };

   app.set_api_access_info_signed_default( {info} );

   ACTOR( alice );
   db.modify( alice, [](account_object& obj) {
      obj.membership_expiration_date = time_point_sec::maximum();
   });

   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( pass_as_special_user )
{ try {

   api_access_info_signed info;
   info.required_lifetime_member = false;
   info.required_registrar = "";
   info.allowed_apis = { "database_api" };

   app.set_api_access_info_signed_user( "alice", std::move(info) );

   ACTOR( alice );
   transfer_operation op;
   op.from = alice_id;

   signed_transaction trx;
   trx.operations.push_back( op );
   trx.expiration = db.head_block_time() + 60;
   trx.sign( alice_private_key, db.get_chain_id() );

   auto json = fc::json::to_string<signed_transaction>( trx );
   auto encoded = fc::base64_encode( json );

   login_api login_api( app );
   login_api.enable_api( "database_api" );
   bool logged_in = login_api.login_signed( encoded );
   BOOST_CHECK( logged_in );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
