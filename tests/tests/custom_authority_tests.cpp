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
#include <graphene/protocol/market.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/custom_authority_object.hpp>
#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

namespace graphene { namespace protocol {
bool operator==(const restriction& a, const restriction& b) {
   if (std::tie(a.member_index, a.restriction_type) != std::tie(b.member_index, b.restriction_type))
      return false;
   if (a.argument.is_type<void_t>())
      return b.argument.is_type<void_t>();
   using Value_Argument = static_variant<fc::typelist::slice<restriction::argument_type::list, 1>>;
   return Value_Argument::import_from(a.argument) == Value_Argument::import_from(b.argument);
}
} }


BOOST_FIXTURE_TEST_SUITE(custom_authority_tests, database_fixture)

#define FUNC(TYPE) BOOST_PP_CAT(restriction::func_, TYPE)

size_t restriction_count(const vector<restriction>& rs) {
   return std::for_each(rs.begin(), rs.end(), restriction::adder()).sum;
}
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
   } catch (const fc::exception& e) {
      FC_ASSERT(e.to_detail_string().find(s) != string::npos, "Did not find expected string ${s} in exception: ${e}",
                ("s", s)("e", e));
   }
}
#define EXPECT_EXCEPTION_STRING(S, E) \
    BOOST_TEST_CHECKPOINT("Expect exception containing string: " S); \
    expect_exception_string(S, E)

BOOST_AUTO_TEST_CASE(restriction_predicate_tests) { try {
   using namespace graphene::protocol;
   //////
   // Create a restriction that authorizes transfers only made to Account ID 12
   //////
   vector<restriction> restrictions;
   auto to_index = member_index<transfer_operation>("to");
   restrictions.emplace_back(to_index, FUNC(eq), account_id_type(12));

   //////
   // Create an operation that transfers to Account ID 0
   // This should violate the restriction
   //////
   transfer_operation transfer;
   // Check that the proposed operation to account ID 0 is not compliant with the restriction to account ID 12
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   // Inspect the reasons why the proposed operation was rejected
   // The rejection path will reference portions of the restrictions
   //[
   //  {
   //    "member_index": 2,
   //    "restriction_type": 0,
   //    "argument": [
   //      7,
   //      "1.2.12"
   //    ],
   //    "extensions": []
   //  }
   //]
   BOOST_CHECK_EQUAL(restriction_count(restrictions), 1);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 2);
   // Index 0 (the outer-most) rejection path refers to the first and only restriction
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<size_t>() == 0);
   // Index 1 (the inner-most) rejection path refers to the first and only argument for an account ID of 1.2.12
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);

   //////
   // Create an operation that transfer to Account ID 12
   // This should satisfy the restriction
   //////
   transfer.to = account_id_type(12);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);


   //////
   // Create an INVALID restriction that references an invalid member index
   // (Index 6 is greater than the highest 0-based index of 5)
   // of the transfer operation
   //////
   restrictions.front() = restriction(fc::typelist::length<fc::reflector<transfer_operation>::native_members>(),
                                      FUNC(eq), account_id_type(12));
   //[
   //  {
   //    "member_index": 6,
   //    "restriction_type": 0,
   //    "argument": [
   //      7,
   //      "1.2.12"
   //    ],
   //    "extensions": []
   //  }
   //]
   //
   // This restriction should throw an exception related to an invalid member index
   //   10 assert_exception: Assert Exception
   //   r.member_index < typelist::length<member_list>(): Invalid member index 6 for object graphene::protocol::transfer_operation
   //           {"I":6,"O":"graphene::protocol::transfer_operation"}
   //   th_a  restriction_predicate.hxx:493 create_field_predicate
   BOOST_CHECK_THROW(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value),
                     fc::assert_exception);


   //////
   // Create an INVALID restriction that compares a transfer operation's account ID type to an asset ID type
   //////
   restrictions.front() = restriction(to_index, FUNC(eq), asset_id_type(12));
   //[
   //  {
   //    "member_index": 2,
   //    "restriction_type": 0,
   //    "argument": [
   //      8,
   //      "1.3.12"
   //    ],
   //    "extensions": []
   //  }
   //]
   //
   // This restriction should throw an exception related to invalid type
   //   10 assert_exception: Assert Exception
   //   Invalid types for predicate
   //   {}
   //   th_a  restriction_predicate.hxx:147 predicate_invalid
   //
   //   {"fc::get_typename<Field>::name()":"graphene::protocol::account_id_type","func":"func_eq","arg":[8,"1.3.12"]}
   //   th_a  restriction_predicate.hxx:476 create_predicate_function
   BOOST_CHECK_THROW(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value),
                     fc::assert_exception);

   //////
   // Create a restriction such that the operation fee must be paid with Asset ID 0
   //////
   auto fee_index = member_index<transfer_operation>("fee");
   auto asset_id_index = member_index<asset>("asset_id");
   restrictions.front() = restriction(fee_index, FUNC(attr),
                                      vector<restriction>{restriction(asset_id_index, FUNC(eq), asset_id_type(0))});

   //////
   // Check the transfer operation that pays the fee with Asset ID 0
   // This should satisfy the restriction.
   //////
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);

   //////
   // Change the restriction such that the operation fee must be paid with Asset ID 1
   //////
   restrictions.front().argument.get<vector<restriction>>().front().argument = asset_id_type(1);
   //[
   //  {
   //    "member_index": 0,
   //    "restriction_type": 10,
   //    "argument": [
   //      39,
   //      [
   //        {
   //          "member_index": 1,
   //          "restriction_type": 0,
   //          "argument": [
   //            8,
   //            "1.3.1"
   //          ],
   //          "extensions": []
   //        }
   //      ]
   //    ],
   //    "extensions": []
   //  }
   //]

   BOOST_CHECK_EQUAL(restriction_count(restrictions), 2);
   //////
   // Check the transfer operation that pays the fee with Asset ID 0 against the restriction.
   // This should violate the restriction.
   //////
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   // Inspect the reasons why the proposed operation was rejected
   // The rejection path will reference portions of the restrictions
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 3);
   // Index 0 (the outer-most) rejection path refers to the first and only restriction
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<size_t>() == 0);
   // Index 1 rejection path refers to the first and only argument of the restriction
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<size_t>() == 0);
   // Index 2 (the inner-most) rejection path refers to the first and only argument
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[2].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);

   //////
   // Create a restriction that authorizes transfers only to Account ID 12
   //////
   restrictions.emplace_back(to_index, FUNC(eq), account_id_type(12));
   //[
   //  {
   //    "member_index": 0,
   //    "restriction_type": 10,
   //    "argument": [
   //      39,
   //      [
   //        {
   //          "member_index": 1,
   //          "restriction_type": 0,
   //          "argument": [
   //            8,
   //            "1.3.1"
   //          ],
   //          "extensions": []
   //        }
   //      ]
   //    ],
   //    "extensions": []
   //  },
   //  {
   //    "member_index": 2,
   //    "restriction_type": 0,
   //    "argument": [
   //      7,
   //      "1.2.12"
   //    ],
   //    "extensions": []
   //  }
   //]
   BOOST_CHECK_EQUAL(restriction_count(restrictions), 3);

   //////
   // Create a transfer operation that authorizes transfer to Account ID 12
   // This operation should satisfy the restriction
   //////
   transfer.to = account_id_type(12);
   transfer.fee.asset_id = asset_id_type(1);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == true);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 0);

   //////
   // Create a transfer operation that transfers to Account ID 10
   // This operation should violate the restriction
   //////
   transfer.to = account_id_type(10);
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer) == false);
   // Inspect the reasons why the proposed operation was rejected
   // The rejection path will reference portions of the restrictions
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path.size() == 2);
   // Index 0 (the outer-most) rejection path refers to the first and only restriction
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[0].get<size_t>() == 1);
   // Index 1 (the inner-most) rejection path refers to the first and only argument
   BOOST_CHECK(get_restriction_predicate(restrictions, operation::tag<transfer_operation>::value)(transfer)
               .rejection_path[1].get<predicate_result::rejection_reason>() == predicate_result::predicate_was_false);

   //////
   // Create a restriction where the ext.owner_special_authority field is unspecified
   //////
   restrictions.clear();
   auto extensions_index = member_index<account_update_operation>("extensions");
   auto authority_index = member_index<account_update_operation::ext>("owner_special_authority");
   restrictions.emplace_back(extensions_index, FUNC(attr),
                             vector<restriction>{restriction(authority_index, FUNC(eq), void_t())});
   //[
   //  {
   //    "member_index": 5,
   //    "restriction_type": 10,
   //    "argument": [
   //      39,
   //      [
   //        {
   //          "member_index": 1,
   //          "restriction_type": 0,
   //          "argument": [
   //            0,
   //            {}
   //          ],
   //          "extensions": []
   //        }
   //      ]
   //    ],
   //    "extensions": []
   //  }
   //]
   BOOST_CHECK_EQUAL(restriction_count(restrictions), 2);
   auto predicate = get_restriction_predicate(restrictions, operation::tag<account_update_operation>::value);

   //////
   // Create an account update operation without any owner_special_authority extension
   //////
   account_update_operation update;
   // The transfer operation should violate the restriction because it does not have an ext field
   BOOST_CHECK_THROW(predicate(transfer), fc::assert_exception);
   // The update operation should satisfy the restriction
   BOOST_CHECK(predicate(update) == true);
   BOOST_CHECK(predicate(update).rejection_path.size() == 0);

   //////
   // Change the update operation to include an owner_special_authority
   // This should violate the restriction
   //////
   update.extensions.value.owner_special_authority = special_authority();
   BOOST_CHECK(predicate(update) == false);
   BOOST_CHECK_EQUAL(predicate(update).rejection_path.size(), 3);
   // Index 0 (the outer-most) rejection path refers to the first and only restriction
   BOOST_CHECK(predicate(update).rejection_path[0].get<size_t>() == 0);
   // Index 1 rejection path refers to the first and only argument of the restriction
   BOOST_CHECK(predicate(update).rejection_path[1].get<size_t>() == 0);
   // Index 2 (the inner-most) rejection path refers to the first and only argument
   BOOST_CHECK(predicate(update).rejection_path[2].get<predicate_result::rejection_reason>() ==
               predicate_result::predicate_was_false);

   //////
   // Change the restriction where the ext.owner_special_authority field must be specified
   //////
   restrictions.front().argument.get<vector<restriction>>().front().restriction_type = FUNC(ne);
   //[
   //  {
   //    "member_index": 5,
   //    "restriction_type": 10,
   //    "argument": [
   //      39,
   //      [
   //        {
   //          "member_index": 1,
   //          "restriction_type": 1,
   //          "argument": [
   //            0,
   //            {}
   //          ],
   //          "extensions": []
   //        }
   //      ]
   //    ],
   //    "extensions": []
   //  }
   //]

   //////
   // The update operation should satisfy the new restriction because the ext.owner_special_authority is specified
   //////
   predicate = get_restriction_predicate(restrictions, operation::tag<account_update_operation>::value);
   BOOST_CHECK(predicate(update) == true);
} FC_LOG_AND_RETHROW() }

   /**
    * Test predicates containing logical ORs
    * Test of authorization and revocation of one account (Alice) authorizing multiple other accounts (Bob and Charlie)
    * to transfer out of her account by using a single custom active authority with two logical OR branches.
    *
    * This can alternatively be achieved by using two custom active authority authorizations
    * as is done in multiple_transfer_custom_auths
    */
   BOOST_AUTO_TEST_CASE(logical_or_transfer_predicate_tests) {
      try {
         using namespace graphene::protocol;
         //////
         // Create a restriction that authorizes transfers only made to Account ID 12 or Account 15
         //////
         auto to_index = member_index<transfer_operation>("to");
         vector<restriction> branch1 = vector<restriction>{restriction(to_index, FUNC(eq), account_id_type(12))};
         vector<restriction> branch2 = vector<restriction>{restriction(to_index, FUNC(eq), account_id_type(15))};
         unsigned_int dummy_index = 999;
         vector<restriction> or_restrictions = {
                 restriction(dummy_index, FUNC(logical_or), vector<vector<restriction>>{branch1, branch2})};
         //[
         //  {
         //    "member_index": 999,
         //    "restriction_type": 11,
         //    "argument": [
         //      40,
         //      [
         //        [
         //          {
         //            "member_index": 2,
         //            "restriction_type": 0,
         //            "argument": [
         //              7,
         //              "1.2.12"
         //            ],
         //            "extensions": []
         //          }
         //        ],
         //        [
         //          {
         //            "member_index": 2,
         //            "restriction_type": 0,
         //            "argument": [
         //              7,
         //              "1.2.15"
         //            ],
         //            "extensions": []
         //          }
         //        ]
         //      ]
         //    ],
         //    "extensions": []
         //  }
         //]
         BOOST_CHECK_EQUAL(restriction_count(or_restrictions), 3);
         auto predicate = get_restriction_predicate(or_restrictions, operation::tag<transfer_operation>::value);

         //////
         // Create an operation that transfers to Account ID 12
         // This should satisfy the restriction because Account ID 12 is authorized to transfer
         //////
         transfer_operation transfer_to_12 = transfer_operation();
         transfer_to_12.to = account_id_type(12);
         BOOST_CHECK_EQUAL(predicate(transfer_to_12).rejection_path.size(), 0);

         //////
         // Create an operation that transfers to Account ID 15
         // This should satisfy the restriction because Account ID 15 is authorized to transfer
         //////
         transfer_operation transfer_to_15 = transfer_operation();
         transfer_to_15.to = account_id_type(15);
         BOOST_CHECK(predicate(transfer_to_15) == true);
         BOOST_CHECK_EQUAL(predicate(transfer_to_15).rejection_path.size(), 0);

         //////
         // Create an operation that transfers to Account ID 1
         // This should violate the restriction because Account 1 is not authorized to transfer
         //////
         transfer_operation transfer_to_1;
         transfer_to_1.to = account_id_type(1);
         BOOST_CHECK(predicate(transfer_to_1) == false);
         BOOST_CHECK_EQUAL(predicate(transfer_to_1).rejection_path.size(), 2);
         // Index 0 (the outer-most) rejection path refers to the first and only restriction
         BOOST_CHECK(predicate(transfer_to_1).rejection_path[0].get<size_t>() == 0);
         // Index 1 (the inner-most) rejection path refers to the first and only argument:
         // the vector of branches each of which are one level deep
         vector<predicate_result> branch_results = predicate(
                 transfer_to_1).rejection_path[1].get<vector<predicate_result>>();
         unsigned long nbr_branches = branch_results.size();
         BOOST_CHECK_EQUAL(nbr_branches, 2);
         for (unsigned long j = 0; j < nbr_branches; ++j) {
            predicate_result &result = branch_results.at(j);
            BOOST_CHECK_EQUAL(result.success, false);

            BOOST_CHECK_EQUAL(result.rejection_path.size(), 2);
            // Index 0 (the outer-most) rejection path refers to the first and only restriction
            BOOST_CHECK_EQUAL(result.rejection_path[0].get<size_t>(), 0);
            // Index 1 (the inner-most) rejection path refers to the first and only argument for an account ID:
            // either 1.2.12 or 1.2.15
            BOOST_CHECK(result.rejection_path[1].get<predicate_result::rejection_reason>() ==
                        predicate_result::predicate_was_false);

         }

      } FC_LOG_AND_RETHROW()
   }


BOOST_AUTO_TEST_CASE(custom_auths) { try {
   //////
   // Initialize the test
   //////
   generate_blocks(HARDFORK_BSIP_40_TIME);
   generate_blocks(5);
   db.modify(global_property_id_type()(db), [](global_property_object& gpo) {
      gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
   });
   set_expiration(db, trx);
   ACTORS((alice)(bob))
   fund(alice, asset(1000*GRAPHENE_BLOCKCHAIN_PRECISION));
   fund(bob, asset(1000*GRAPHENE_BLOCKCHAIN_PRECISION));

   //////
   // Create a custom authority where Bob is authorized to transfer from Alice's account
   // if and only if the transfer amount is less than 100 of Asset ID 0.
   // This custom authority is NOT YET published.
   //////
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
   //[
   //  {
   //    "member_index": 3,
   //    "restriction_type": 10,
   //    "argument": [
   //      39,
   //      [
   //        {
   //          "member_index": 0,
   //          "restriction_type": 2,
   //          "argument": [
   //            2,
   //            10000000
   //          ],
   //          "extensions": []
   //        },
   //        {
   //          "member_index": 1,
   //          "restriction_type": 0,
   //          "argument": [
   //            8,
   //            "1.3.0"
   //          ],
   //          "extensions": []
   //        }
   //      ]
   //    ],
   //    "extensions": []
   //  }
   //]
   BOOST_CHECK_EQUAL(restriction_count(op.restrictions), 3);


   //////
   // Bob attempts to transfer 99 CORE from Alice's account
   // This attempt should fail because it is attempted before the custom authority is published
   //////
   transfer_operation top;
   top.to = bob.get_id();
   top.from = alice.get_id();
   top.amount.amount = 99 * GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.operations = {top};
   sign(trx, bob_private_key);
   // No custom auth yet; bob's transfer should reject
   BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

   //////
   // Alice publishes the custom authority
   //////
   trx.clear();
   trx.operations = {op};
   sign(trx, alice_private_key);
   PUSH_TX(db, trx);

   custom_authority_id_type auth_id =
           db.get_index_type<custom_authority_index>().indices().get<by_account_custom>().find(alice_id)->id;

   //////
   // Bob attempts to transfer 99 CORE from Alice's account
   // This attempt should succeed because it is attempted after the custom authority is published
   //////
   trx.clear();
   trx.operations = {top};
   sign(trx, bob_private_key);
   PUSH_TX(db, trx);

   //////
   // Bob attempts to transfer 100 CORE from Alice's account
   // This attempt should fail because it exceeds the authorized amount
   //////
   trx.operations.front().get<transfer_operation>().amount.amount = 100*GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   // If bob tries to transfer 100, it rejects because the restriction is strictly less than 100
   EXPECT_EXCEPTION_STRING("\"rejection_path\":[[0,0],[0,0],[2,\"predicate_was_false\"]]", [&] {PUSH_TX(db, trx);});

   //////
   // Update the custom authority so that Bob is authorized to transfer from Alice's account
   // if and only if the transfer amount EXACTLY EQUALS 100 of Asset ID 0.
   // This custom authority is NOT YET published.
   //////
   op.restrictions.front().argument.get<vector<restriction>>().front().restriction_type = restriction::func_eq;
   custom_authority_update_operation uop;
   uop.account = alice.get_id();
   uop.authority_to_update = auth_id;
   uop.restrictions_to_remove = {0};
   uop.restrictions_to_add = {op.restrictions.front()};
   trx.clear();
   trx.operations = {uop};
   sign(trx, alice_private_key);
   PUSH_TX(db, trx);

   BOOST_CHECK(auth_id(db).get_restrictions() == uop.restrictions_to_add);

   //////
   // Bob attempts to transfer 99 CORE from Alice's account
   // This attempt should fail because only transfers of 100 CORE are authorized
   //////
   trx.clear();
   trx.operations = {top};
   trx.expiration += 5;
   sign(trx, bob_private_key);
   // The transfer of 99 should reject because the requirement is for exactly 100
   EXPECT_EXCEPTION_STRING("\"rejection_path\":[[0,0],[0,0],[2,\"predicate_was_false\"]]", [&] {PUSH_TX(db, trx);});

   //////
   // Bob attempts to transfer 100 CORE from Alice's account
   // This attempt should succeed because transfers of exactly 100 CORE are authorized by Alice
   //////
   trx.operations.front().get<transfer_operation>().amount.amount = 100*GRAPHENE_BLOCKCHAIN_PRECISION;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   PUSH_TX(db, trx);
   auto transfer = trx;

   generate_block();

   //////
   // Bob attempts to transfer 100 CORE from Alice's account AGAIN
   // This attempt should succeed because there are no limits to the transfer size nor quantity
   // besides the available CORE in Alice's account
   //////
   trx.expiration += 5;
   trx.clear_signatures();
   sign(trx, bob_private_key);
   PUSH_TX(db, trx);

   //////
   // Alice revokes the custom authority for Bob
   //////
   custom_authority_delete_operation dop;
   dop.account = alice.get_id();
   dop.authority_to_delete = auth_id;
   trx.clear();
   trx.operations = {dop};
   sign(trx, alice_private_key);
   PUSH_TX(db, trx);

   //////
   // Bob attempts to transfer 100 CORE from Alice's account
   // This attempt should fail because it is attempted after the custom authority has been revoked
   //////
   transfer.expiration += 10;
   transfer.clear_signatures();
   sign(transfer, bob_private_key);
   BOOST_CHECK_THROW(PUSH_TX(db, transfer), tx_missing_active_auth);
} FC_LOG_AND_RETHROW() }


   /**
    * Test of authorization and revocation of one account (Alice) authorizing multiple other accounts (Bob and Charlie)
    * to transfer out of her account by using two distinct custom active authorities.
    *
    * This can alternatively be achieved by using a single custom active authority with two logical OR branches
    * as is done in logical_or_transfer_predicate_tests
    */
   BOOST_AUTO_TEST_CASE(multiple_transfer_custom_auths) {
      try {
         //////
         // Initialize the test
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);
         ACTORS((alice)(bob)(charlie)(diana))
         fund(alice, asset(1000 * GRAPHENE_BLOCKCHAIN_PRECISION));
         fund(bob, asset(1000 * GRAPHENE_BLOCKCHAIN_PRECISION));


         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Charlie
         // This attempt should fail because Alice has not authorized anyone to transfer from her account
         //////
         transfer_operation bob_transfers_from_alice_to_charlie;
         bob_transfers_from_alice_to_charlie.to = charlie.get_id();
         bob_transfers_from_alice_to_charlie.from = alice.get_id();
         bob_transfers_from_alice_to_charlie.amount.amount = 100 * GRAPHENE_BLOCKCHAIN_PRECISION;
         trx.operations = {bob_transfers_from_alice_to_charlie};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should fail because Alice has not authorized anyone to transfer from her account
         //////
         transfer_operation bob_transfers_from_alice_to_diana;
         bob_transfers_from_alice_to_diana.to = diana.get_id();
         bob_transfers_from_alice_to_diana.from = alice.get_id();
         bob_transfers_from_alice_to_diana.amount.amount = 60 * GRAPHENE_BLOCKCHAIN_PRECISION;
         trx.operations = {bob_transfers_from_alice_to_diana};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

         //////
         // Charlie attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should fail because Alice has not authorized anyone to transfer from her account
         //////
         transfer_operation charlie_transfers_from_alice_to_diana;
         charlie_transfers_from_alice_to_diana.to = diana.get_id();
         charlie_transfers_from_alice_to_diana.from = alice.get_id();
         charlie_transfers_from_alice_to_diana.amount.amount = 25 * GRAPHENE_BLOCKCHAIN_PRECISION;
         trx.operations = {charlie_transfers_from_alice_to_diana};
         sign(trx, charlie_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

         //////
         // Create a custom authority where Bob is authorized to transfer from Alice's account to Charlie
         //////
         custom_authority_create_operation op;
         op.account = alice.get_id();
         op.auth.add_authority(bob.get_id(), 1);
         op.auth.weight_threshold = 1;
         op.enabled = true;
         op.valid_to = db.head_block_time() + 1000;
         op.operation_type = operation::tag<transfer_operation>::value;
         auto to_index = member_index<transfer_operation>("to");
         vector<restriction> restrictions;
         restrictions.emplace_back(to_index, FUNC(eq), charlie.get_id());
         op.restrictions = restrictions;
         //[
         //  {
         //    "member_index": 2,
         //    "restriction_type": 0,
         //    "argument": [
         //      7,
         //      "1.2.18"
         //    ],
         //    "extensions": []
         //  }
         //]

         // Alice publishes the custom authority
         trx.clear();
         trx.operations = {op};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);

         custom_authority_id_type ca_bob_transfers_from_alice_to_charlie =
                 db.get_index_type<custom_authority_index>().indices().get<by_account_custom>().find(alice_id)->id;

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Charlie
         // This attempt should succeed because it is attempted after the custom authority is published
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_charlie};
         sign(trx, bob_private_key);
         PUSH_TX(db, trx);

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should fail because Alice has not authorized Bob to transfer to Diana
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_diana};
         sign(trx, charlie_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

         //////
         // Charlie attempts to transfer 100 CORE from Alice's account to Charlie
         // This attempt should fail because Alice has not authorized Charlie to transfer to Diana
         //////
         trx.clear();
         trx.operations = {charlie_transfers_from_alice_to_diana};
         sign(trx, charlie_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);



         //////
         // Advance the blockchain to generate distinctive hash IDs for the re-used transactions
         //////
         generate_blocks(1);

         //////
         // Create a custom authority where Charlie is authorized to transfer from Alice's account to Diana
         //////
         op = custom_authority_create_operation();
         op.account = alice.get_id();
         op.auth.add_authority(charlie.get_id(), 1);
         op.auth.weight_threshold = 1;
         op.enabled = true;
         op.valid_to = db.head_block_time() + 1000;
         op.operation_type = operation::tag<transfer_operation>::value;
         restrictions.clear();
         restrictions.emplace_back(to_index, FUNC(eq), diana.get_id());
         op.restrictions = restrictions;
         //[
         //  {
         //    "member_index": 2,
         //    "restriction_type": 0,
         //    "argument": [
         //      7,
         //      "1.2.19"
         //    ],
         //    "extensions": []
         //  }
         //]

         // Alice publishes the additional custom authority
         trx.clear();
         trx.operations = {op};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);

         // Note the additional custom authority
         const auto &ca_index = db.get_index_type<custom_authority_index>().indices().get<by_account_custom>();

         auto ca_alice_range = ca_index.equal_range(alice_id);
         long nbr_alice_auths = std::distance(ca_alice_range.first, ca_alice_range.second);
         BOOST_CHECK_EQUAL(2, nbr_alice_auths);
         auto iter = ca_alice_range.first;
         custom_authority_id_type *ca_charlie_transfers_from_alice_to_diana = nullptr;
         while (iter != ca_index.end()) {
            custom_authority_id_type ca_id = iter->id;
            const custom_authority_object *ca = db.find<custom_authority_object>(ca_id);
            flat_map<account_id_type, weight_type> ca_authorities = ca->auth.account_auths;
            BOOST_CHECK_EQUAL(1, ca_authorities.size());
            if (ca_authorities.find(charlie.get_id()) != ca_authorities.end()) {
               ca_charlie_transfers_from_alice_to_diana = &ca_id;
               break;
            }

            iter++;
         }
         BOOST_CHECK(ca_charlie_transfers_from_alice_to_diana != nullptr);

         //////
         // Charlie attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should succeed because it is attempted after the custom authority is published
         //////
         trx.clear();
         trx.operations = {charlie_transfers_from_alice_to_diana};
         sign(trx, charlie_private_key);
         PUSH_TX(db, trx);

         //////
         // Bob should still be able to transfer from Alice to Charlie
         // Bob attempts to transfer 100 CORE from Alice's account to Charlie
         // This attempt should succeed because it was previously authorized by Alice
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_charlie};
         sign(trx, bob_private_key);
         PUSH_TX(db, trx);

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should fail because Alice has not authorized Bob to transfer to Diana
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_diana};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);



         //////
         // Advance the blockchain to generate distinctive hash IDs for the re-used transactions
         //////
         generate_blocks(1);

         //////
         // Alice revokes the custom authority for Bob
         //////
         custom_authority_delete_operation revoke_bob_authorization;
         revoke_bob_authorization.account = alice.get_id();
         revoke_bob_authorization.authority_to_delete = ca_bob_transfers_from_alice_to_charlie;
         trx.clear();
         trx.operations = {revoke_bob_authorization};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Charlie
         // This attempt should fail because Alice has revoked authorization for Bob to transfer from her account
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_charlie};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

         //////
         // Charlie attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should succeed because Alice should still be authorized to transfer from Alice account
         //////
         trx.clear();
         trx.operations = {charlie_transfers_from_alice_to_diana};
         sign(trx, charlie_private_key);
         PUSH_TX(db, trx);

         //////
         // Bob attempts to transfer 100 CORE from Alice's account to Diana
         // This attempt should fail because Alice has not authorized Bob to transfer to Diana
         //////
         trx.clear();
         trx.operations = {bob_transfers_from_alice_to_diana};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

      } FC_LOG_AND_RETHROW()
   }

   /**
    * Test of authorization and revocation of one account (Alice) authorizing another account (Bob)
    * to trade with her account but not to transfer out of her account
    */
   BOOST_AUTO_TEST_CASE(authorized_trader_custom_auths) {
      try {
         //////
         // Initialize the blockchain
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);


         //////
         // Initialize: Define a market-issued asset called USDBIT
         //////
         ACTORS((feedproducer));
         const auto &bitusd = create_bitasset("USDBIT", feedproducer_id);
         const auto &core = asset_id_type()(db);
         update_feed_producers(bitusd, {feedproducer.id});

         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         current_feed.settlement_price = bitusd.amount(1) / core.amount(5);
         publish_feed(bitusd, feedproducer, current_feed);


         //////
         // Initialize: Fund some accounts
         //////
         ACTORS((alice)(bob)(charlie)(diana))
         fund(alice, asset(5000 * GRAPHENE_BLOCKCHAIN_PRECISION));
         fund(bob, asset(100 * GRAPHENE_BLOCKCHAIN_PRECISION));


         //////
         // Bob attempts to create a limit order on behalf of Alice
         // This should fail because Bob is not authorized to trade with her account
         //////
         set_expiration( db, trx );
         trx.operations.clear();

         limit_order_create_operation buy_order;
         buy_order.seller = alice_id;
         buy_order.amount_to_sell = core.amount(59);
         buy_order.min_to_receive = bitusd.amount(7);
         buy_order.expiration = time_point_sec::maximum();

         trx.clear();
         trx.operations = {buy_order};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Alice authorizes Bob to place limit orders that offer the core asset for sale
         //////
         custom_authority_create_operation authorize_limit_orders;
         authorize_limit_orders.account = alice.get_id();
         authorize_limit_orders.auth.add_authority(bob.get_id(), 1);
         authorize_limit_orders.auth.weight_threshold = 1;
         authorize_limit_orders.enabled = true;
         authorize_limit_orders.valid_to = db.head_block_time() + 1000;
         authorize_limit_orders.operation_type = operation::tag<limit_order_create_operation>::value;
         trx.clear();
         trx.operations = {authorize_limit_orders};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);

         auto caa =
                 db.get_index_type<custom_authority_index>().indices().get<by_account_custom>().find(alice.get_id());
         custom_authority_id_type auth_id = caa->id;

         custom_authority_create_operation authorize_limit_order_cancellations;
         authorize_limit_order_cancellations.account = alice.get_id();
         authorize_limit_order_cancellations.auth.add_authority(bob.get_id(), 1);
         authorize_limit_order_cancellations.auth.weight_threshold = 1;
         authorize_limit_order_cancellations.enabled = true;
         authorize_limit_order_cancellations.valid_to = db.head_block_time() + 1000;
         authorize_limit_order_cancellations.operation_type = operation::tag<limit_order_cancel_operation>::value;
         trx.clear();
         trx.operations = {authorize_limit_order_cancellations};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the buy order transaction
         //////
         generate_blocks(1);


         //////
         // Bob attempts to create a limit order on behalf of Alice
         // This should succeed because Bob is authorized to create limit orders
         //////
         trx.clear();
         trx.operations = {buy_order};
         sign(trx, bob_private_key);
         auto processed_buy = PUSH_TX(db, trx);
         const limit_order_object *buy_order_object = db.find<limit_order_object>( processed_buy.operation_results[0].get<object_id_type>() );


         //////
         // Bob attempts to cancel the limit order on behalf of Alice
         // This should succeed because Bob is authorized to cancel limit orders
         //////
         limit_order_cancel_operation cancel_order;
         cancel_order.fee_paying_account = alice_id;
         cancel_order.order = buy_order_object->id;
         trx.clear();
         trx.operations = {cancel_order};
         sign(trx, bob_private_key);
         auto processed_cancelled = PUSH_TX(db, trx);

         //////
         // Bob attempts to transfer funds out of Alice's account
         // This should fail because Bob is not authorized to transfer funds out of her account
         //////
         transfer_operation top;
         top.to = bob.get_id();
         top.from = alice.get_id();
         top.amount.amount = 99 * GRAPHENE_BLOCKCHAIN_PRECISION;
         trx.operations = {top};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the buy order transaction
         //////
         generate_blocks(1);


         //////
         // Alice attempts to create her own limit order
         // This should succeed because Alice has not relinquished her own authority to trade
         //////
         buy_order = limit_order_create_operation();
         buy_order.seller = alice_id;
         buy_order.amount_to_sell = core.amount(59);
         buy_order.min_to_receive = bitusd.amount(7);
         buy_order.expiration = time_point_sec::maximum();
         trx.clear();
         trx.operations = {buy_order};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);


         //////
         // Alice revokes/disables the authorization to create limit orders
         //////
         custom_authority_update_operation disable_authorizations;
         disable_authorizations.account = alice.get_id();
         disable_authorizations.authority_to_update = auth_id;
         disable_authorizations.new_enabled = false;
         trx.clear();
         trx.operations = {disable_authorizations};
         sign(trx, alice_private_key);
         PUSH_TX(db, trx);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the buy order transaction
         //////
         generate_blocks(1);


         //////
         // Bob attempts to create a limit order on behalf of Alice
         // This should fail because Bob is not authorized to trade with her account
         //////
         trx.clear();
         trx.operations = {buy_order};
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of authorization of one account (feedproducer) authorizing another account (Bob)
    * to publish feeds. The authorization remains associated with account even when the account changes its keys.
    */
   BOOST_AUTO_TEST_CASE(feed_publisher_authorizes_other_account) {
      try {
         //////
         // Initialize the blockchain
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);


         //////
         // Initialize: Define a market-issued asset called USDBIT
         //////
         ACTORS((feedproducer));
         const auto &bitusd = create_bitasset("USDBIT", feedproducer_id);
         const auto &core = asset_id_type()(db);
         update_feed_producers(bitusd, {feedproducer.id});

         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         current_feed.settlement_price = bitusd.amount(1) / core.amount(5);
         publish_feed(bitusd, feedproducer, current_feed);


         //////
         // Initialize: Fund other accounts
         //////
         ACTORS((bob))
         fund(bob, asset(100 * GRAPHENE_BLOCKCHAIN_PRECISION));


         //////
         // Advance the blockchain to generate a distinctive hash ID for the publish feed transaction
         //////
         generate_blocks(1);


         //////
         // Bob attempts to publish feed of USDBIT on behalf of feedproducer
         // This should fail because Bob is not authorized to publish the feed
         //////
         asset_publish_feed_operation pop;
         pop.publisher = feedproducer.id;
         pop.asset_id = bitusd.id;
         pop.feed = current_feed;
         if (pop.feed.core_exchange_rate.is_null())
            pop.feed.core_exchange_rate = pop.feed.settlement_price;
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // feedproducer authorizes Bob to publish feeds on its behalf
         //////
         custom_authority_create_operation authorize_feed_publishing;
         authorize_feed_publishing.account = feedproducer.get_id();
         authorize_feed_publishing.auth.add_authority(bob.get_id(), 1);
         authorize_feed_publishing.auth.weight_threshold = 1;
         authorize_feed_publishing.enabled = true;
         authorize_feed_publishing.valid_to = db.head_block_time() + 1000;
         authorize_feed_publishing.operation_type = operation::tag<asset_publish_feed_operation>::value;
         trx.clear();
         trx.operations = {authorize_feed_publishing};
         sign(trx, feedproducer_private_key);
         PUSH_TX(db, trx);

         custom_authority_id_type auth_id =
                 db.get_index_type<custom_authority_index>().indices().get<by_account_custom>().find(feedproducer.id)->id;

         //////
         // Bob attempts to publish feed of USDBIT on behalf of feedproducer
         // This should succeed because Bob is authorized by feedproducer to publish the feed
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, bob_private_key);
         PUSH_TX(db, trx);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the publish feed transaction
         //////
         generate_blocks(1);


         //////
         // Bob creates a new key
         //////
         fc::ecc::private_key new_bob_private_key = generate_private_key("new Bob key");
         public_key_type new_bob_public_key = public_key_type(new_bob_private_key.get_public_key());


         //////
         // Bob attempts to publish feed of USDBIT on behalf of feedproducer with new key
         // This should fail because the new key is not associated with Bob on the blockchain
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, new_bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the publish feed transaction
         //////
         generate_blocks(1);


         //////
         // Bob changes his account's active key
         //////
         account_update_operation uop;
         uop.account = bob.get_id();
         uop.active = authority(1, new_bob_public_key, 1);
         trx.clear();
         trx.operations.emplace_back(std::move(uop));
         sign(trx, bob_private_key);
         PUSH_TX(db, trx);


         //////
         // Bob attempts to publish feed of USDBIT on behalf of feedproducer
         // This should succeed because Bob's new key is associated with Bob's authorized account.
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, new_bob_private_key);
         PUSH_TX(db, trx);


         //////
         // Feedproducer revokes/disables the authorization by disabling it
         //////
         custom_authority_update_operation disable_authorizations;
         disable_authorizations.account = feedproducer.get_id();
         disable_authorizations.authority_to_update = auth_id;
         disable_authorizations.new_enabled = false;
         trx.clear();
         trx.operations = {disable_authorizations};
         sign(trx, feedproducer_private_key);
         PUSH_TX(db, trx);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the publish feed transaction
         //////
         generate_blocks(1);


         //////
         // Bob attempts to publish feed of USDBIT on behalf of feedproducer with new key
         // This should fail because Bob's account is no longer authorized by feedproducer
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, new_bob_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of authorization of one account (feedproducer) authorizing another key
    * to publish feeds
    */
   BOOST_AUTO_TEST_CASE(authorized_feed_publisher_other_key_custom_auths) {
      try {
         //////
         // Initialize the blockchain
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);


         //////
         // Initialize: Define a market-issued asset called USDBIT
         //////
         ACTORS((feedproducer));
         const auto &bitusd = create_bitasset("USDBIT", feedproducer_id);
         const auto &core = asset_id_type()(db);
         update_feed_producers(bitusd, {feedproducer.id});

         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         current_feed.settlement_price = bitusd.amount(1) / core.amount(5);
         // publish_feed(bitusd, feedproducer, current_feed);
         asset_publish_feed_operation pop;
         pop.publisher = feedproducer.id;
         pop.asset_id = bitusd.id;
         pop.feed = current_feed;
         if (pop.feed.core_exchange_rate.is_null())
            pop.feed.core_exchange_rate = pop.feed.settlement_price;


         //////
         // Advance the blockchain to generate a distinctive hash ID for the publish feed transaction
         //////
         generate_blocks(1);


         //////
         // Define a key that can be authorized
         // This can be a new key or an existing key. The existing key may even be the active key of an account.
         //////
         fc::ecc::private_key some_private_key = generate_private_key("some key");
         public_key_type some_public_key = public_key_type(some_private_key.get_public_key());


         //////
         // feedproducer authorizes a key to publish feeds on its behalf
         //////
         custom_authority_create_operation authorize_feed_publishing;
         authorize_feed_publishing.account = feedproducer.get_id();
         authorize_feed_publishing.auth.add_authority(some_public_key, 1);
         authorize_feed_publishing.auth.weight_threshold = 1;
         authorize_feed_publishing.enabled = true;
         authorize_feed_publishing.valid_to = db.head_block_time() + 1000;
         authorize_feed_publishing.operation_type = operation::tag<asset_publish_feed_operation>::value;
         trx.clear();
         trx.operations = {authorize_feed_publishing};
         sign(trx, feedproducer_private_key);
         PUSH_TX(db, trx);


         //////
         // Any software client with this key attempts to publish feed of USDBIT on behalf of feedproducer
         // This should succeed because the pusher of this transaction signs the transaction with the authorized key
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(pop));
         sign(trx, some_private_key);
         PUSH_TX(db, trx);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of authorization of one account (faucet) authorizing another key
    * to register accounts
    */
   BOOST_AUTO_TEST_CASE(authorized_faucet_other_key_custom_auths) {
      try {
         //////
         // Initialize the blockchain
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);


         //////
         // Initialize: faucet account
         //////
         ACTORS((faucet)(charlie));
         fund(faucet, asset(500000 * GRAPHENE_BLOCKCHAIN_PRECISION));
         account_upgrade_operation uop;
         uop.account_to_upgrade = faucet.get_id();
         uop.upgrade_to_lifetime_member = true;
         trx.clear();
         trx.operations.emplace_back(std::move(uop));
         sign(trx, faucet_private_key);
         PUSH_TX(db, trx);

         // Lambda for creating account
         auto create_account_by_name = [&](const string &name, const account_object& registrar) {
            account_create_operation create_op;
            create_op.name = name;
            public_key_type new_key = public_key_type(generate_private_key(name + " seed").get_public_key());
            create_op.registrar = registrar.id;
            create_op.owner = authority(1, new_key, 1);
            create_op.active = authority(1, new_key, 1);
            create_op.options.memo_key = new_key;
            create_op.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;

            return create_op;
         };


         //////
         // Attempt to register an account with this key
         // This should succeed because faucet is a lifetime member account
         //////
         string name = "account1";
         account_create_operation create_op = create_account_by_name(name, faucet);
         trx.clear();
         trx.operations = {create_op};
         sign(trx, faucet_private_key);
         PUSH_TX(db, trx);


         //////
         // Define a key that can be authorized
         // This can be a new key or an existing key. The existing key may even be the active key of an account.
         //////
         fc::ecc::private_key some_private_key = generate_private_key("some key");
         public_key_type some_public_key = public_key_type(some_private_key.get_public_key());


         //////
         // Attempt to register an account with this key
         // This should fail because the key is not authorized to register any accounts
         //////
         name = "account2";
         create_op = create_account_by_name(name, faucet);
         trx.clear();
         trx.operations = {create_op};
         sign(trx, some_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // faucet authorizes a key to register accounts on its behalf
         //////
         custom_authority_create_operation authorize_account_registration;
         authorize_account_registration.account = faucet.get_id();
         authorize_account_registration.auth.add_authority(some_public_key, 1);
         authorize_account_registration.auth.weight_threshold = 1;
         authorize_account_registration.enabled = true;
         authorize_account_registration.valid_to = db.head_block_time() + 1000;
         authorize_account_registration.operation_type = operation::tag<account_create_operation>::value;
         trx.clear();
         trx.operations = {authorize_account_registration};
         sign(trx, faucet_private_key);
         PUSH_TX(db, trx);


         //////
         // Advance the blockchain to generate a distinctive hash ID for the account registration transaction
         //////
         generate_blocks(1);


         //////
         // Attempt to register an account with this key
         // This should succeed because the key is authorized to register any accounts
         //////
         trx.clear();
         trx.operations.emplace_back(std::move(create_op));
         sign(trx, some_private_key);
         PUSH_TX(db, trx);


         //////
         // Attempt to register an account with this key
         // This should succeed because the key is authorized to register any accounts
         //////
         create_op = create_account_by_name("account3", faucet);
         trx.clear();
         trx.operations = {create_op};
         sign(trx, some_private_key);
         PUSH_TX(db, trx);


         //////
         // Attempt to transfer funds out of the faucet account
         // This should fail because the key is not authorized to transfer from the faucet account
         //////
         transfer_operation top;
         top.amount.amount = 99 * GRAPHENE_BLOCKCHAIN_PRECISION;
         top.from = faucet.get_id();
         top.to = charlie.get_id();
         top.fee.asset_id = asset_id_type(1);
         trx.clear();
         trx.operations = {top};
         sign(trx, some_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Attempt to register an account with this key
         // This should succeed because the key is authorized to register any accounts
         //////
         create_op = create_account_by_name("account4", faucet);
         trx.clear();
         trx.operations = {create_op};
         sign(trx, some_private_key);
         PUSH_TX(db, trx);

      } FC_LOG_AND_RETHROW()
   }

   /**
    * Test of authorization of a key to transfer one asset type (USDBIT) from one account (coldwallet)
    * to another account (hotwallet)
    */
   BOOST_AUTO_TEST_CASE(authorized_cold_wallet_key_custom_auths) {
      try {
         //////
         // Initialize the blockchain
         //////
         generate_blocks(HARDFORK_BSIP_40_TIME);
         generate_blocks(5);
         db.modify(global_property_id_type()(db), [](global_property_object &gpo) {
            gpo.parameters.extensions.value.custom_authority_options = custom_authority_options_type();
         });
         set_expiration(db, trx);


         //////
         // Initialize: Accounts
         ACTORS((feedproducer)(coldwallet)(hotwallet)(hacker));
         int64_t init_balance(100 * GRAPHENE_BLOCKCHAIN_PRECISION);

         //////
         // Initialize: Define a market-issued asset called USDBIT
         //////
         // Define core asset
         const auto &core = asset_id_type()(db);
         asset_id_type core_id = core.id;

         // Create a smart asset
         const asset_object &bitusd = create_bitasset("USDBIT", feedproducer_id);
         asset_id_type usd_id = bitusd.id;
         update_feed_producers(bitusd, {feedproducer.id});
         price_feed current_feed;
         current_feed.maintenance_collateral_ratio = 1750;
         current_feed.maximum_short_squeeze_ratio = 1100;
         current_feed.settlement_price = bitusd.amount(1) / core.amount(5);
         publish_feed(bitusd, feedproducer, current_feed);


         //////
         // Fund coldwallet with core asset
         //////
         fund(coldwallet, asset(init_balance));
         // coldwallet will borrow 1000 bitUSD
         borrow(coldwallet, bitusd.amount(1000), asset(15000));
         int64_t alice_balance_usd_before_offer = get_balance(coldwallet_id, usd_id);
         BOOST_CHECK_EQUAL( 1000,  alice_balance_usd_before_offer);
         int64_t coldwallet_balance_core_before_offer = get_balance(coldwallet_id, core_id);
         BOOST_CHECK_EQUAL( init_balance - 15000, coldwallet_balance_core_before_offer );


         //////
         // Define a key that can be authorized
         // This can be a new key or an existing key. The existing key may even be the active key of an account.
         //////
         fc::ecc::private_key some_private_key = generate_private_key("some key");
         public_key_type some_public_key = public_key_type(some_private_key.get_public_key());


         //////
         // Create a custom authority where the key is authorized to transfer from the coldwallet account
         // if and only if the transfer asset type is USDBIT and the recipient account is hotwallet.
         //////
         custom_authority_create_operation op;
         op.account = coldwallet.get_id();
         op.auth.add_authority(some_public_key, 1);
         op.auth.weight_threshold = 1;
         op.enabled = true;
         op.valid_to = db.head_block_time() + 1000;

         op.operation_type = operation::tag<transfer_operation>::value;

         auto to_index = member_index<transfer_operation>("to");
         op.restrictions.emplace_back(to_index, FUNC(eq), hotwallet_id);

         auto transfer_amount_index = member_index<transfer_operation>("amount");
         auto assed_id_index = member_index<asset>("asset_id");
         op.restrictions.emplace_back(restriction(transfer_amount_index, restriction::func_attr, vector<restriction>{
                 restriction(assed_id_index, restriction::func_eq, usd_id)}));
         //[
         //  {
         //    "member_index": 2,
         //    "restriction_type": 0,
         //    "argument": [
         //      7,
         //      "1.2.18"
         //    ],
         //    "extensions": []
         //  },
         //  {
         //    "member_index": 3,
         //    "restriction_type": 10,
         //    "argument": [
         //      39,
         //      [
         //        {
         //          "member_index": 1,
         //          "restriction_type": 0,
         //          "argument": [
         //            8,
         //            "1.3.2"
         //          ],
         //          "extensions": []
         //        }
         //      ]
         //    ],
         //    "extensions": []
         //  }
         //]
         BOOST_CHECK_EQUAL(restriction_count(op.restrictions), 3);

         // Publish the new custom authority
         trx.clear();
         trx.operations = {op};
         sign(trx, coldwallet_private_key);
         PUSH_TX(db, trx);


         //////
         // Attempt to transfer USDBIT asset out of the coldwallet to the hacker account
         // This should fail because the key is not authorized to transfer to the hacker account
         //////
         transfer_operation top;
         top.from = coldwallet.get_id();
         top.to = hacker.get_id();
         top.amount.asset_id = usd_id;
         top.amount.amount = 99;
         top.fee.asset_id = core_id;
         trx.clear();
         trx.operations = {top};
         sign(trx, some_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Attempt to transfer CORE asset out of the coldwallet to the hotwallet account
         // This should fail because the key is not authorized to transfer core asset to the hotwallet account
         //////
         top = transfer_operation();
         top.from = coldwallet.get_id();
         top.to = hotwallet.get_id();
         top.amount.asset_id = core_id;
         top.amount.amount = 99;
         top.fee.asset_id = core_id;
         trx.clear();
         trx.operations = {top};
         sign(trx, some_private_key);
         BOOST_CHECK_THROW(PUSH_TX(db, trx), tx_missing_active_auth);


         //////
         // Attempt to transfer USDBIT asset out of the coldwallet to the hotwallet account
         // This should succeed because the key is authorized to transfer USDBIT asset to the hotwallet account
         //////
         top = transfer_operation();
         top.from = coldwallet.get_id();
         top.to = hotwallet.get_id();
         top.amount.asset_id = usd_id;
         top.amount.amount = 99;
         top.fee.asset_id = core_id;
         trx.clear();
         trx.operations = {top};
         sign(trx, some_private_key);
         PUSH_TX(db, trx);

      } FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()
