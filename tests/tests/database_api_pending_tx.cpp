/*
 * Copyright (c) 2017 Blockchain BV
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

struct database_api_tests_fixture : public database_fixture
{
    processed_transaction push_transaction_for_account_creation(const std::string& account_name)
    {
        auto account_key = generate_private_key(account_name);
        signed_transaction trx;
        set_expiration(db, trx);
        trx.operations.push_back(make_account(account_name, account_key.get_public_key()));
        trx.validate();
        return db.push_transaction(trx, ~0);
    }

    void trigger_transactions_applying()
    {
        db.generate_block(db.get_slot_time(1),
                          db.get_scheduled_witness(1),
                          generate_private_key("null_key"),
                          ~0 | database::skip_undo_history_check);
    }

    void check_transaction_in_list(const map<transaction_id_type, signed_transaction>& left, const transaction& right)
    {
        BOOST_CHECK(left.find(right.id()) != left.end());
    }
};

BOOST_FIXTURE_TEST_SUITE(database_api_tests, database_api_tests_fixture)

BOOST_AUTO_TEST_CASE(list_pending_proposals_empty) {
    try {
        graphene::app::database_api database_api(db);

        auto pending_transactions = database_api.list_pending_transactions();
        BOOST_CHECK(pending_transactions.empty());
    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(list_pending_proposals_one) {
    try {
        graphene::app::database_api database_api(db);

        auto sam_transaction = push_transaction_for_account_creation("sam");

        auto pending_transactions = database_api.list_pending_transactions();

        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 1);
        check_transaction_in_list(pending_transactions, sam_transaction);
    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(list_pending_proposals_several) {
    try {
        graphene::app::database_api database_api(db);

        auto sam_transaction = push_transaction_for_account_creation("sam");
        auto dan_transaction = push_transaction_for_account_creation("dan");

        auto pending_transactions = database_api.list_pending_transactions();

        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 2);
        check_transaction_in_list(pending_transactions, sam_transaction);
        check_transaction_in_list(pending_transactions, dan_transaction);
    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(list_pending_proposals_one_after_applying) {
    try {
        graphene::app::database_api database_api(db);

        auto sam_transaction = push_transaction_for_account_creation("sam");

        auto pending_transactions = database_api.list_pending_transactions();
        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 1);
        check_transaction_in_list(pending_transactions, sam_transaction);

        trigger_transactions_applying();

        pending_transactions = database_api.list_pending_transactions();
        BOOST_CHECK(pending_transactions.empty());
    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(list_pending_proposals_several_after_applying) {
    try {
        graphene::app::database_api database_api(db);

        auto sam_transaction = push_transaction_for_account_creation("sam");
        auto dan_transaction = push_transaction_for_account_creation("dan");

        auto pending_transactions = database_api.list_pending_transactions();
        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 2);
        check_transaction_in_list(pending_transactions, sam_transaction);
        check_transaction_in_list(pending_transactions, dan_transaction);

        trigger_transactions_applying();

        pending_transactions = database_api.list_pending_transactions();
        BOOST_CHECK(pending_transactions.empty());
    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(list_pending_proposals_postponed) {
    try {
        graphene::app::database_api database_api(db);

        db.modify(db.get_global_properties(), [](global_property_object& properties) {
            //Size in bytes. Empiricaly found to limit block size for two test transactions
            properties.parameters.maximum_block_size = 650;
        });

        auto sam_transaction = push_transaction_for_account_creation("sam");
        auto dan_transaction = push_transaction_for_account_creation("dan");
        auto jon_transaction = push_transaction_for_account_creation("jon");

        auto pending_transactions = database_api.list_pending_transactions();
        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 3);
        check_transaction_in_list(pending_transactions, sam_transaction);
        check_transaction_in_list(pending_transactions, dan_transaction);
        check_transaction_in_list(pending_transactions, jon_transaction);

        trigger_transactions_applying();

        pending_transactions = database_api.list_pending_transactions();
        BOOST_REQUIRE_EQUAL(pending_transactions.size(), 1);
        check_transaction_in_list(pending_transactions, jon_transaction);

        trigger_transactions_applying();

        pending_transactions = database_api.list_pending_transactions();
        BOOST_CHECK(pending_transactions.empty());

    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(subscribe_to_pending_transactions) {
    try {
        graphene::app::database_api database_api(db);

        signed_transaction transaction_in_notification;
        database_api.subscribe_to_pending_transactions([&]( const variant& signed_transaction_object ){
            transaction_in_notification = signed_transaction_object.as<signed_transaction>(GRAPHENE_MAX_NESTED_OBJECTS);
        });

        auto sam_transaction = push_transaction_for_account_creation("sam");
        BOOST_CHECK(sam_transaction.id() == transaction_in_notification.id());

        auto dan_transaction = push_transaction_for_account_creation("dan");
        BOOST_CHECK(dan_transaction.id() == transaction_in_notification.id());

    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(unsubscribe_from_pending_transactions) {
    try {
        graphene::app::database_api database_api(db);

        database_api.subscribe_to_pending_transactions([&]( const variant& signed_transaction_object ){
            BOOST_FAIL("This callback should not be called, because subscription was canceled.");
        });

        database_api.unsubscribe_from_pending_transactions();

        push_transaction_for_account_creation("sam");

    } FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
