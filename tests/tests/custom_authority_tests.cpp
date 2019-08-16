/*
 * Copyright (c) 2019 Contributors
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

#include <string>
#include <boost/test/unit_test.hpp>
#include <fc/exception/exception.hpp>
#include <graphene/protocol/restriction_predicate.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/custom_authority_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(custom_authority_tests, database_fixture)

#define FUNC(TYPE) BOOST_PP_CAT(restriction::func_, TYPE)

template<typename Object>
unsigned_int member_index(string name) {
   unsigned_int index;
   fc::typelist::runtime::for_each(typename fc::reflector<Object>::native_members(), [&name, &index](auto t) mutable {
      if (name == decltype(t)::type::get_name())
         index = decltype(t)::type::index;
   });
   return index;
}

template<typename Expression>
void expect_exception_string(const string& s, Expression e) {
   try{
      e();
      FC_THROW_EXCEPTION(fc::assert_exception, "Expected exception with string ${s}, but no exception thrown",
                         ("s", s));
   } catch (fc::exception e) {
      FC_ASSERT(e.to_detail_string().find(s) != string::npos, "Did not find expected string ${s} in exception: ${e}",
                ("s", s)("e", e));
   }
}
#define EXPECT_EXCEPTION_STRING(S, E) \
    BOOST_TEST_CHECKPOINT("Expect exception containing string: " S); \
    expect_exception_string(S, E)

BOOST_AUTO_TEST_CASE(restriction_predicate_tests) { try {
   using namespace graphene::protocol;
   vector<restriction> restrictions;
   transfer_operation transfer;

   auto to_index = member_index<transfer_operation>("to");
   restrictions.emplace_back(to_index, FUNC(eq), account_id_type(12));
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 2);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<size_t>() == 0);
   transfer.to = account_id_type(12);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);

   restrictions.front() = restriction(fc::typelist::length<fc::reflector<transfer_operation>::native_members>(),
                                      FUNC(eq), account_id_type(12));
   BOOST_CHECK_THROW(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value),
                     fc::assert_exception);
   restrictions.front() = restriction(to_index, FUNC(eq), asset_id_type(12));
   BOOST_CHECK_THROW(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value),
                     fc::assert_exception);

   auto fee_index = member_index<transfer_operation>("fee");
   auto asset_id_index = member_index<asset>("asset_id");
   restrictions.front() = restriction(fee_index, FUNC(attr),
                                      vector<restriction>{restriction(asset_id_index, FUNC(eq), asset_id_type(0))});
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);
   restrictions.front().argument.get<vector<restriction>>().front().argument = asset_id_type(1);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 3);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<size_t>() == 0);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[2].get<size_t>() == 0);
   restrictions.emplace_back(to_index, FUNC(eq), account_id_type(12));
   transfer.to = account_id_type(12);
   transfer.fee.asset_id = asset_id_type(1);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);
   transfer.to = account_id_type(10);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 2);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<size_t>() == 1);

   account_update_operation update;
   restrictions.clear();
   auto extensions_index = member_index<account_update_operation>("extensions");
   auto authority_index = member_index<account_update_operation::ext>("owner_special_authority");
   restrictions.emplace_back(extensions_index, FUNC(attr),
                             vector<restriction>{restriction(authority_index, FUNC(eq), void_t())});
   auto predicate = get_restriction_predicate(restrictions, operation::tag<account_update_operation>::value);
   BOOST_CHECK_THROW(predicate(transfer), fc::assert_exception);
   BOOST_CHECK(predicate(update) == true);
   BOOST_CHECK(predicate(update).rejection_path.size() == 0);
   update.extensions.value.owner_special_authority = special_authority();
   BOOST_CHECK(predicate(update) == false);
   BOOST_CHECK_EQUAL(predicate(update).rejection_path.size(), 3);
   BOOST_CHECK(predicate(update).rejection_path[0].get<predicate_result::rejection_reason>() ==
               predicate_result::predicate_was_false);
   BOOST_CHECK(predicate(update).rejection_path[1].get<size_t>() == 0);
   BOOST_CHECK(predicate(update).rejection_path[2].get<size_t>() == 0);
   restrictions.front().argument.get<vector<restriction>>().front().restriction_type = FUNC(ne);
   predicate = get_restriction_predicate(restrictions, operation::tag<account_update_operation>::value);
   BOOST_CHECK(predicate(update) == true);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(custom_auths) { try {
   generate_blocks(HARDFORK_BSIP_40_TIME);
   generate_blocks(5);
   db.modify(global_property_id_type()(db), [](global_property_object& gpo) {
      gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
   });
   set_expiration(db, trx);
   ACTORS((alice)(bob))
   fund(alice, asset(1000*GRAPHENE_BLOCKCHAIN_PRECISION));
   fund(bob, asset(1000*GRAPHENE_BLOCKCHAIN_PRECISION));

   custom_authority_create_operation op;
   op.account = alice.get_id();
   op.auth.add_authority(bob.get_id(), 1);
   op.auth.weight_threshold = 1;
   op.enabled = true;
   op.valid_to = db.head_block_time() + 1000;
   op.operation_type = operation::tag<transfer_operation>::value;
   auto transfer_amount_index = member_index<transfer_operation>("amount");
   auto asset_amount_index = member_index<asset>("amount");
   auto assed_id_index = member_index<asset>("asset_id");
   op.restrictions = {restriction(transfer_amount_index, restriction::func_attr, vector<restriction>{
                          restriction(asset_amount_index, restriction::func_lt,
                                      int64_t(100*GRAPHENE_BLOCKCHAIN_PRECISION)),
                          restriction(assed_id_index, restriction::func_eq, asset_id_type(0))})};

   transfer_operation top;
   top.to = bob.get_id();
   top.from = alice.get_id();
   top.amount.amount = 99 * GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.operations = {top};
   sign(trx, bob_private_key);
   // No custom auth yet; bob's transfer should reject
   BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

   trx.clear();
   trx.operations = {op};
   sign(trx, alice_private_key);
   // Alice publishes the custom authority
   PUSH_TX(db, trx);

   custom_authority_id_type auth_id =
           db.get_index_type<custom_authority_index>().indices().get<by_account_custom>().find(alice_id)->id;

   trx.clear();
   trx.operations = {top};
   sign(trx, bob_private_key);
   // Now bob's transfer should succeed due to the custom authority
   PUSH_TX(db, trx);

   trx.operations.front().get<transfer_operation>().amount.amount = 100*GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   // If bob tries to transfer 100, it rejects because the restriction is strictly less than 100
   EXPECT_EXCEPTION_STRING("\"rejection_path\":[[2,\"predicate_was_false\"],[0,0],[0,0]]", [&] {PUSH_TX(db, trx);});

   op.restrictions.front().argument.get<vector<restriction>>().front().restriction_type = restriction::func_eq;
   custom_authority_update_operation uop;
   uop.account = alice.get_id();
   uop.authority_to_update = auth_id;
   uop.restrictions_to_remove = {0};
   uop.restrictions_to_add = {op.restrictions.front()};
   trx.clear();
   trx.operations = {uop};
   sign(trx, alice_private_key);
   // Alice publishes an update to the custom authority, making the restriction require exactly 100
   PUSH_TX(db, trx);

   BOOST_CHECK(auth_id(db).get_restrictions() == uop.restrictions_to_add);

   trx.clear();
   trx.operations = {top};
   trx.expiration += 5;
   sign(trx, bob_private_key);
   // The transfer of 99 should reject because the requirement is for exactly 100
   EXPECT_EXCEPTION_STRING("\"rejection_path\":[[2,\"predicate_was_false\"],[0,0],[0,0]]", [&] {PUSH_TX(db, trx);});

   trx.operations.front().get<transfer_operation>().amount.amount = 100*GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   // A transfer of 100 should succeed
   PUSH_TX(db, trx);
   auto transfer = trx;

   generate_block();

   trx.expiration += 5;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   // Another one should succeed
   PUSH_TX(db, trx);

   custom_authority_delete_operation dop;
   dop.account = alice.get_id();
   dop.authority_to_delete = auth_id;
   trx.clear();
   trx.operations = {dop};
   sign(trx, alice_private_key);
   // Alice deletes the custom authority
   PUSH_TX(db, trx);

   transfer.expiration += 10;
   transfer.clear_signatures();
   sign(transfer, bob_private_key);
   // The transfer should no longer work
   BOOST_CHECK_THROW(PUSH_TX(db, transfer), tx_missing_active_auth);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
