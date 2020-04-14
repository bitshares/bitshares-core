#include <string>
#include <boost/test/unit_test.hpp>
#include <fc/exception/exception.hpp>

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>

#include "../common/database_fixture.hpp"


using namespace graphene::chain;
using namespace graphene::chain::test;

struct simple_maker_taker_database_fixture : database_fixture {
   simple_maker_taker_database_fixture()
           : database_fixture() {
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv) {
      const time_point_sec order_expiration = time_point_sec::maximum();
      const price &fee_core_exchange_rate = price::unit_price();
      limit_order_create_operation op = create_sell_operation(user, amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(account_id_type user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation op = create_sell_operation(user(db), amount, recv, order_expiration,
                                                              fee_core_exchange_rate);
      return op;
   }

   const limit_order_create_operation
   create_sell_operation(const account_object &user, const asset &amount, const asset &recv,
                         const time_point_sec order_expiration,
                         const price &fee_core_exchange_rate) {
      limit_order_create_operation sell_order;
      sell_order.seller = user.id;
      sell_order.amount_to_sell = amount;
      sell_order.min_to_receive = recv;
      sell_order.expiration = order_expiration;

      return sell_order;
   }

   const asset_create_operation create_user_issued_asset_operation(const string &name, const account_object &issuer,
                                                                   uint16_t flags, const price &core_exchange_rate,
                                                                   uint8_t precision, uint16_t maker_fee_percent,
                                                                   uint16_t taker_fee_percent) {
      asset_create_operation creator;
      creator.issuer = issuer.id;
      creator.fee = asset();
      creator.symbol = name;
      creator.common_options.max_supply = 0;
      creator.precision = precision;

      creator.common_options.core_exchange_rate = core_exchange_rate;
      creator.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      creator.common_options.flags = flags;
      creator.common_options.issuer_permissions = flags;
      creator.common_options.market_fee_percent = maker_fee_percent;
      creator.common_options.extensions.value.taker_fee_percent = taker_fee_percent;

      return creator;

   }
};


/**
 * BSIP81: Asset owners may specify different market fee rate for maker orders and taker orders
 */
BOOST_FIXTURE_TEST_SUITE(simple_maker_taker_fee_tests, simple_maker_taker_database_fixture)

   /**
    * Test of setting taker fee before HF and after HF for a UIA
    */
   BOOST_AUTO_TEST_CASE(setting_taker_fees_uia) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy));
         account_id_type issuer_id = jill.id;
         fc::ecc::private_key issuer_private_key = jill_private_key;

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));
         uint16_t market_fee_percent = 20 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                market_fee_percent);

         //////
         // Before HF, test inability to set taker fees
         //////
         asset_update_operation uop;
         uop.issuer = issuer_id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uint16_t new_taker_fee_percent = uop.new_options.market_fee_percent / 2;
         uop.new_options.extensions.value.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception);
         // TODO: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = jillcoin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());


         //////
         // Before HF, test inability to set taker fees with an asset update operation inside of a proposal
         //////
         {
            trx.clear();
            set_expiration(db, trx);

            uint64_t alternate_taker_fee_percent = new_taker_fee_percent * 2;
            uop.new_options.extensions.value.taker_fee_percent = alternate_taker_fee_percent;

            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(uop);

            trx.operations.push_back(cop);
            // sign(trx, issuer_private_key);
            GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception);

            // Check the taker fee is not changed because the proposal has not been approved
            updated_asset = jillcoin.get_id()(db);
            BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());
         }


         //////
         // Before HF, test inability to set taker fees with an asset create operation inside of a proposal
         //////
         {
            trx.clear();
            set_expiration(db, trx);

            uint64_t maker_fee_percent = 10 * GRAPHENE_1_PERCENT;
            uint64_t taker_fee_percent = 2 * GRAPHENE_1_PERCENT;
            asset_create_operation ac_op = create_user_issued_asset_operation("JCOIN2", jill, charge_market_fee, price,
                                                                              2,
                                                                              maker_fee_percent, taker_fee_percent);

            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(ac_op);

            trx.operations.push_back(cop);
            // sign(trx, issuer_private_key);

            GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // The proposal should be rejected

         }


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test default values of taker fee after HF
         // After the HF its default value should be the market fee percent
         // which is effectively the new maker fee percent
         //////
         updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.extensions.value.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TODO: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.extensions.value.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = jillcoin.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, test ability to set taker fees with an asset update operation inside of a proposal
         //////
         {
            trx.clear();
            set_expiration(db, trx);

            uint64_t alternate_taker_fee_percent = new_taker_fee_percent * 2;
            uop.new_options.extensions.value.taker_fee_percent = alternate_taker_fee_percent;

            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(uop);

            trx.operations.push_back(cop);
            // sign(trx, issuer_private_key);
            processed_transaction processed = PUSH_TX(db, trx); // No exception should be thrown

            // Check the taker fee is not changed because the proposal has not been approved
            updated_asset = jillcoin.get_id()(db);
            expected_taker_fee_percent = new_taker_fee_percent;
            BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
            BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


            // Approve the proposal
            trx.clear();
            proposal_id_type pid = processed.operation_results[0].get<object_id_type>();

            proposal_update_operation pup;
            pup.fee_paying_account = jill.id;
            pup.proposal = pid;
            pup.active_approvals_to_add.insert(jill.id);
            trx.operations.push_back(pup);
            set_expiration(db, trx);
            sign(trx, jill_private_key);

            PUSH_TX(db, trx); // No exception should be thrown

            // Advance to after proposal expires
            generate_blocks(cop.expiration_time);

            // Check the taker fee is not changed because the proposal has not been approved
            updated_asset = jillcoin.get_id()(db);
            expected_taker_fee_percent = alternate_taker_fee_percent;
            BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
            BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         }


         //////
         // After HF, test ability to set taker fees with an asset create operation inside of a proposal
         //////
         {
            trx.clear();
            set_expiration(db, trx);

            uint64_t maker_fee_percent = 10 * GRAPHENE_1_PERCENT;
            uint64_t taker_fee_percent = 2 * GRAPHENE_1_PERCENT;
            asset_create_operation ac_op = create_user_issued_asset_operation("JCOIN2", jill, charge_market_fee, price,
                                                                              2,
                                                                              maker_fee_percent, taker_fee_percent);

            proposal_create_operation cop;
            cop.review_period_seconds = 86400;
            uint32_t buffer_seconds = 60 * 60;
            cop.expiration_time = db.head_block_time() + *cop.review_period_seconds + buffer_seconds;
            cop.fee_paying_account = GRAPHENE_TEMP_ACCOUNT;
            cop.proposed_ops.emplace_back(ac_op);

            trx.operations.push_back(cop);
            // sign(trx, issuer_private_key);

            processed_transaction processed = PUSH_TX(db, trx); // No exception should be thrown

            // Check the asset does not exist because the proposal has not been approved
            const auto& asset_idx = db.get_index_type<asset_index>().indices().get<by_symbol>();
            const auto itr = asset_idx.find("JCOIN2");
            BOOST_CHECK(itr == asset_idx.end());

            // Approve the proposal
            trx.clear();
            proposal_id_type pid = processed.operation_results[0].get<object_id_type>();

            proposal_update_operation pup;
            pup.fee_paying_account = jill.id;
            pup.proposal = pid;
            pup.active_approvals_to_add.insert(jill.id);
            trx.operations.push_back(pup);
            set_expiration(db, trx);
            sign(trx, jill_private_key);

            PUSH_TX(db, trx); // No exception should be thrown

            // Advance to after proposal expires
            generate_blocks(cop.expiration_time);

            // Check the taker fee is not changed because the proposal has not been approved
            BOOST_CHECK(asset_idx.find("JCOIN2") != asset_idx.end());
            updated_asset = *asset_idx.find("JCOIN2");
            expected_taker_fee_percent = taker_fee_percent;
            BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
            BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);
            uint16_t expected_maker_fee_percent = maker_fee_percent;
            BOOST_CHECK_EQUAL(expected_maker_fee_percent, updated_asset.options.market_fee_percent);

         }

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of setting taker fee before HF and after HF for a smart asset
    */
   BOOST_AUTO_TEST_CASE(setting_taker_fees_smart_asset) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
//         const asset_object &bitsmart = create_bitasset("SMARTBIT", smartissuer.id);
         const asset_object bitsmart = create_bitasset("SMARTBIT", smartissuer.id);


         generate_blocks(HARDFORK_615_TIME); // get around Graphene issue #615 feed expiration bug
         generate_block();

         //////
         // Before HF, test inability to set taker fees
         //////
         asset_update_operation uop;
         uop.issuer = smartissuer.id;
         uop.asset_to_update = bitsmart.get_id();
         uop.new_options = bitsmart.options;
         uint16_t new_taker_fee_percent = uop.new_options.market_fee_percent / 2;
         uop.new_options.extensions.value.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TODO: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = bitsmart.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test default values of taker fee after HF
         // After the HF its default value should be the market fee percent
         // which is effectively the new maker fee percent
         //////
         updated_asset = bitsmart.get_id()(db);
         uint16_t expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.extensions.value.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TODO: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.extensions.value.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = bitsmart.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test the default taker fee values of multiple different assets after HF
    */
   BOOST_AUTO_TEST_CASE(default_taker_fees) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((alice)(bob)(charlie)(smartissuer));

         // Initialize tokens with custom market fees
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t alice1coin_market_fee_percent = 1 * GRAPHENE_1_PERCENT;
         const asset_object alice1coin = create_user_issued_asset("ALICE1COIN", alice, charge_market_fee, price, 2,
                                                                  alice1coin_market_fee_percent);

         const uint16_t alice2coin_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object alice2coin = create_user_issued_asset("ALICE2COIN", alice, charge_market_fee, price, 2,
                                                                  alice2coin_market_fee_percent);

         const uint16_t bob1coin_market_fee_percent = 3 * GRAPHENE_1_PERCENT;
         const asset_object bob1coin = create_user_issued_asset("BOB1COIN", alice, charge_market_fee, price, 2,
                                                                bob1coin_market_fee_percent);

         const uint16_t bob2coin_market_fee_percent = 4 * GRAPHENE_1_PERCENT;
         const asset_object bob2coin = create_user_issued_asset("BOB2COIN", alice, charge_market_fee, price, 2,
                                                                bob2coin_market_fee_percent);

         const uint16_t charlie1coin_market_fee_percent = 4 * GRAPHENE_1_PERCENT;
         const asset_object charlie1coin = create_user_issued_asset("CHARLIE1COIN", alice, charge_market_fee, price, 2,
                                                                    charlie1coin_market_fee_percent);

         const uint16_t charlie2coin_market_fee_percent = 5 * GRAPHENE_1_PERCENT;
         const asset_object charlie2coin = create_user_issued_asset("CHARLIE2COIN", alice, charge_market_fee, price, 2,
                                                                    charlie2coin_market_fee_percent);

         const uint16_t bitsmart1coin_market_fee_percent = 7 * GRAPHENE_1_PERCENT;
         create_bitasset("SMARTBIT1", smartissuer.id, bitsmart1coin_market_fee_percent);
         generate_blocks(1); // The smart asset's ID will be updated after a block is generated
//         const asset_object &bitsmart1 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT1");
         const asset_object bitsmart1 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT1");

         const uint16_t bitsmart2coin_market_fee_percent = 8 * GRAPHENE_1_PERCENT;
         create_bitasset("SMARTBIT2", smartissuer.id, bitsmart2coin_market_fee_percent);
         generate_blocks(1); // The smart asset's ID will be updated after a block is generated
//         const asset_object &bitsmart2 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT2");
         const asset_object bitsmart2 = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("SMARTBIT2");


         //////
         // Before HF, test the market/maker fees for each asset
         //////
         asset_object updated_asset;
         uint16_t expected_fee_percent;

         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // Before HF, test that taker fees are not set
         //////
         // Check the taker fee
         updated_asset = alice1coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = alice2coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = bob1coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = bob2coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = charlie1coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = charlie2coin.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = bitsmart1.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());

         updated_asset = bitsmart2.get_id()(db);
         BOOST_CHECK(!updated_asset.options.extensions.value.taker_fee_percent.valid());


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test the maker fees for each asset are unchanged
         //////
         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // After HF, test the taker fees for each asset are set, by default, to the maker fees
         //////
         updated_asset = alice1coin.get_id()(db);
         expected_fee_percent = alice1coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = alice2coin.get_id()(db);
         expected_fee_percent = alice2coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = bob1coin.get_id()(db);
         expected_fee_percent = bob1coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = bob2coin.get_id()(db);
         expected_fee_percent = bob2coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = charlie1coin.get_id()(db);
         expected_fee_percent = charlie1coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = charlie2coin.get_id()(db);
         expected_fee_percent = charlie2coin_market_fee_percent;
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = bitsmart1.get_id()(db);
         expected_fee_percent = bitsmart1coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         updated_asset = bitsmart2.get_id()(db);
         expected_fee_percent = bitsmart2coin_market_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a UIA
    */
   BOOST_AUTO_TEST_CASE(simple_match_and_fill_with_different_fees_uia) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t IZZY_PRECISION = 1000;
         const uint16_t izzy_market_fee_percent = 5 * GRAPHENE_1_PERCENT;
         const asset_object izzycoin = create_user_issued_asset("ICOIN", izzy, charge_market_fee, price, 3,
                                                                izzy_market_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = jill_maker_fee_percent / 2;

         uint16_t izzy_maker_fee_percent = izzy_market_fee_percent;
         uint16_t izzy_taker_fee_percent = izzy_maker_fee_percent / 2;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uop.new_options.extensions.value.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         // Set the new taker fee for IZZYCOIN
         uop.issuer = izzy.id;
         uop.asset_to_update = izzycoin.get_id();
         uop.new_options = izzycoin.options;
         uop.new_options.extensions.value.taker_fee_percent = izzy_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, izzy_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for IZZYCOIN
         updated_asset = izzycoin.get_id()(db);
         expected_taker_fee_percent = izzy_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, create limit orders that will perfectly match
         //////
         BOOST_TEST_MESSAGE("Issuing 10 jillcoin to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 300 izzycoin to bob");
         issue_uia(bob, izzycoin.amount(300 * IZZY_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 300 * IZZY_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 IZZYCOIN
         limit_order_create_operation alice_sell_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            izzycoin.amount(300 *
                                                                                            IZZY_PRECISION));
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object* alice_order_before = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order_before != nullptr);

         // Bob is willing to sell 300 IZZYCOIN for at least 10 JILLCOIN
         limit_order_create_operation bob_sell_op = create_sell_operation(bob.id, izzycoin.amount(300 * IZZY_PRECISION),
                                                                          jillcoin.amount(
                                                                                  10 *
                                                                                  JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(bob_sell_op);
         asset bob_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check that the orders were filled by ensuring that they are no longer on the order books
         const limit_order_object* alice_order = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order == nullptr);
         const limit_order_object* bob_order = db.find<limit_order_object>(bob_order_id);
         BOOST_CHECK(bob_order == nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving IZZYCOIN
         asset expected_izzy_fee = izzycoin.amount(
                 300 * IZZY_PRECISION * izzy_maker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(alice, izzycoin),
                             (300 * IZZY_PRECISION) - alice_sell_fee.amount.value - expected_izzy_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin),
                             (10 * JILL_PRECISION) - bob_sell_fee.amount.value - expected_jill_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 0);

         // Check the asset issuer's accumulated fees
         BOOST_CHECK(izzycoin.dynamic_asset_data_id(db).accumulated_fees == expected_izzy_fee.amount);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee.amount);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a UIA
    *
    * Test the filling of a taker fee when the **maker** fee percent is set to 0.  This tests some optimizations
    * in database::calculate_market_fee().
    */
   BOOST_AUTO_TEST_CASE(simple_match_and_fill_with_different_fees_uia_2) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 0 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t IZZY_PRECISION = 1000;
         const uint16_t izzy_market_fee_percent = 0 * GRAPHENE_1_PERCENT;
         const asset_object izzycoin = create_user_issued_asset("ICOIN", izzy, charge_market_fee, price, 3,
                                                                izzy_market_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = 1 * GRAPHENE_1_PERCENT;

         uint16_t izzy_maker_fee_percent = izzy_market_fee_percent;
         uint16_t izzy_taker_fee_percent = 3 * GRAPHENE_1_PERCENT;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options.market_fee_percent = jill_maker_fee_percent;
         uop.new_options = jillcoin.options;
         uop.new_options.extensions.value.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         // Set the new taker fee for IZZYCOIN
         uop.issuer = izzy.id;
         uop.asset_to_update = izzycoin.get_id();
         uop.new_options.market_fee_percent = izzy_maker_fee_percent;
         uop.new_options = izzycoin.options;
         uop.new_options.extensions.value.taker_fee_percent = izzy_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, izzy_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for IZZYCOIN
         updated_asset = izzycoin.get_id()(db);
         expected_taker_fee_percent = izzy_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, create limit orders that will perfectly match
         //////
         BOOST_TEST_MESSAGE("Issuing 10 jillcoin to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 300 izzycoin to bob");
         issue_uia(bob, izzycoin.amount(300 * IZZY_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 300 * IZZY_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 IZZYCOIN
         limit_order_create_operation alice_sell_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            izzycoin.amount(300 *
                                                                                            IZZY_PRECISION));
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object* alice_order_before = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order_before != nullptr);

         // Bob is willing to sell 300 IZZYCOIN for at least 10 JILLCOIN
         limit_order_create_operation bob_sell_op = create_sell_operation(bob.id, izzycoin.amount(300 * IZZY_PRECISION),
                                                                          jillcoin.amount(
                                                                                  10 *
                                                                                  JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(bob_sell_op);
         asset bob_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check that the orders were filled by ensuring that they are no longer on the order books
         const limit_order_object* alice_order = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order == nullptr);
         const limit_order_object* bob_order = db.find<limit_order_object>(bob_order_id);
         BOOST_CHECK(bob_order == nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving IZZYCOIN
         asset expected_izzy_fee = izzycoin.amount(
                 300 * IZZY_PRECISION * izzy_maker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(alice, izzycoin),
                             (300 * IZZY_PRECISION) - alice_sell_fee.amount.value - expected_izzy_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin),
                             (10 * JILL_PRECISION) - bob_sell_fee.amount.value - expected_jill_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 0);

         // Check the asset issuer's accumulated fees
         BOOST_CHECK(izzycoin.dynamic_asset_data_id(db).accumulated_fees == expected_izzy_fee.amount);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee.amount);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a UIA
    *
    * Test the filling of a taker fee when the **taker** fee percent is set to 0.  This tests some optimizations
    * in database::calculate_market_fee().
    */
   BOOST_AUTO_TEST_CASE(simple_match_and_fill_with_different_fees_uia_3) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));

         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t IZZY_PRECISION = 1000;
         const uint16_t izzy_market_fee_percent = 5 * GRAPHENE_1_PERCENT;
         const asset_object izzycoin = create_user_issued_asset("ICOIN", izzy, charge_market_fee, price, 3,
                                                                izzy_market_fee_percent);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = 0 * GRAPHENE_1_PERCENT;

         uint16_t izzy_maker_fee_percent = izzy_market_fee_percent;
         uint16_t izzy_taker_fee_percent = 0 * GRAPHENE_1_PERCENT;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options.market_fee_percent = jill_maker_fee_percent;
         uop.new_options = jillcoin.options;
         uop.new_options.extensions.value.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         // Set the new taker fee for IZZYCOIN
         uop.issuer = izzy.id;
         uop.asset_to_update = izzycoin.get_id();
         uop.new_options.market_fee_percent = izzy_maker_fee_percent;
         uop.new_options = izzycoin.options;
         uop.new_options.extensions.value.taker_fee_percent = izzy_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, izzy_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for IZZYCOIN
         updated_asset = izzycoin.get_id()(db);
         expected_taker_fee_percent = izzy_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         //////
         // After HF, create limit orders that will perfectly match
         //////
         BOOST_TEST_MESSAGE("Issuing 10 jillcoin to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 300 izzycoin to bob");
         issue_uia(bob, izzycoin.amount(300 * IZZY_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 300 * IZZY_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 IZZYCOIN
         limit_order_create_operation alice_sell_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            izzycoin.amount(300 *
                                                                                            IZZY_PRECISION));
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object* alice_order_before = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order_before != nullptr);

         // Bob is willing to sell 300 IZZYCOIN for at least 10 JILLCOIN
         limit_order_create_operation bob_sell_op = create_sell_operation(bob.id, izzycoin.amount(300 * IZZY_PRECISION),
                                                                          jillcoin.amount(
                                                                                  10 *
                                                                                  JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(bob_sell_op);
         asset bob_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check that the orders were filled by ensuring that they are no longer on the order books
         const limit_order_object* alice_order = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order == nullptr);
         const limit_order_object* bob_order = db.find<limit_order_object>(bob_order_id);
         BOOST_CHECK(bob_order == nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving IZZYCOIN
         asset expected_izzy_fee = izzycoin.amount(
                 300 * IZZY_PRECISION * izzy_maker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(alice, izzycoin),
                             (300 * IZZY_PRECISION) - alice_sell_fee.amount.value - expected_izzy_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin),
                             (10 * JILL_PRECISION) - bob_sell_fee.amount.value - expected_jill_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(bob, izzycoin), 0);

         // Check the asset issuer's accumulated fees
         BOOST_CHECK(izzycoin.dynamic_asset_data_id(db).accumulated_fees == expected_izzy_fee.amount);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee.amount);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a smart asset
    */
   BOOST_AUTO_TEST_CASE(simple_match_and_fill_with_different_fees_smart_asset) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));
         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t SMARTBIT_PRECISION = 10000;
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
//         const asset_object &smartbit = create_bitasset("SMARTBIT", smartissuer.id, smartbit_market_fee_percent,
//                                                        charge_market_fee, 4);
         const asset_object smartbit = create_bitasset("SMARTBIT", smartissuer.id, smartbit_market_fee_percent,
                                                        charge_market_fee, 4);
         const auto &core = asset_id_type()(db);

         update_feed_producers(smartbit, {feedproducer.id});

         price_feed current_feed;
         current_feed.settlement_price = smartbit.amount(100) / core.amount(100);
         current_feed.maintenance_collateral_ratio = 1750; // need to set this explicitly, testnet has a different default
         publish_feed(smartbit, feedproducer, current_feed);

         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = jill_maker_fee_percent / 2;

         uint16_t smartbit_maker_fee_percent = 1 * GRAPHENE_1_PERCENT;
         uint16_t smartbit_taker_fee_percent = 3 * GRAPHENE_1_PERCENT;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uop.new_options.extensions.value.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         // Set the new taker fee for SMARTBIT
         uop = asset_update_operation();
         uop.issuer = smartissuer.id;
         uop.asset_to_update = smartbit.get_id();
         uop.new_options = smartbit.options;
         uop.new_options.market_fee_percent = smartbit_maker_fee_percent;
         uop.new_options.extensions.value.taker_fee_percent = smartbit_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for SMARTBIT
         updated_asset = smartbit.get_id()(db);
         expected_taker_fee_percent = smartbit_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         // Check the maker fee for SMARTBIT
         updated_asset = smartbit.get_id()(db);
         expected_taker_fee_percent = smartbit_maker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // After HF, create limit orders that will perfectly match
         //////
         BOOST_TEST_MESSAGE("Issuing 10 jillcoin to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 300 SMARTBIT to bob");
         transfer(committee_account, bob.id, asset(10000000));
         publish_feed(smartbit, feedproducer, current_feed); // Publish a recent feed
         borrow(bob, smartbit.amount(300 * SMARTBIT_PRECISION), asset(2 * 300 * SMARTBIT_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 300 * SMARTBIT_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 SMARTBIT
         limit_order_create_operation alice_sell_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            smartbit.amount(300 * SMARTBIT_PRECISION));
         trx.clear();
         trx.operations.push_back(alice_sell_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type alice_order_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object *alice_order_before = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order_before != nullptr);


         // Bob is willing to sell 300 SMARTBIT for at least 10 JILLCOIN
         limit_order_create_operation bob_sell_op
                 = create_sell_operation(bob.id, smartbit.amount(300 * SMARTBIT_PRECISION),
                                         jillcoin.amount(10 * JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(bob_sell_op);
         asset bob_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type bob_order_id = ptx.operation_results[0].get<object_id_type>();

         // Check that the orders were filled by ensuring that they are no longer on the order books
         const limit_order_object *alice_order = db.find<limit_order_object>(alice_order_id);
         BOOST_CHECK(alice_order == nullptr);
         const limit_order_object *bob_order = db.find<limit_order_object>(bob_order_id);
         BOOST_CHECK(bob_order == nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving SMARTBIT
         asset expected_smartbit_fee = smartbit.amount(
                 300 * SMARTBIT_PRECISION * smartbit_maker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(alice, smartbit),
                             (300 * SMARTBIT_PRECISION) - alice_sell_fee.amount.value -
                             expected_smartbit_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin),
                             (10 * JILL_PRECISION) - bob_sell_fee.amount.value - expected_jill_fee.amount.value);
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 0);

         // Check the asset issuer's accumulated fees
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == expected_smartbit_fee.amount);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee.amount);

      } FC_LOG_AND_RETHROW()
   }


   /**
    * Test of different maker and taker fees charged when filling limit orders after HF for a smart asset
    *
    * 1. (Order 1) An order will be placed to offer JCOIN
    *
    * 2. (Order 2) A matching-order will be placed to offer SMARTBIT.
    *     Order 2 is large enough that it should be partially filled, and Order 1 will be completely filled.
    *     Order 1 should be charged a maker fee, and Order 2 should be charged a taker fee.
    *     Order 2 should remain on the book.
    *
    * 3. (Order 3) A matching order will be placed to offer JCOIN.
    *     Order 3 should be charged a taker fee, and Order 2 should be charged a maker fee.
    *
    * Summary: Order 2 should be charged a taker fee when matching Order 1, and Order 2 should be charged a maker fee when matching Order 3.
    */
   BOOST_AUTO_TEST_CASE(partial_maker_partial_taker_fills) {
      try {
         // Initialize for the current time
         trx.clear();
         set_expiration(db, trx);

         // Initialize actors
         ACTORS((jill)(izzy)(alice)(bob)(charlie));
         ACTORS((smartissuer)(feedproducer));

         // Initialize tokens
         price price(asset(1, asset_id_type(1)), asset(1));
         const uint16_t JILL_PRECISION = 100;
         const uint16_t jill_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
         const asset_object jillcoin = create_user_issued_asset("JCOIN", jill, charge_market_fee, price, 2,
                                                                jill_market_fee_percent);

         const uint16_t SMARTBIT_PRECISION = 10000;
         const uint16_t smartbit_market_fee_percent = 2 * GRAPHENE_1_PERCENT;
//         const asset_object &smartbit = create_bitasset("SMARTBIT", smartissuer.id, smartbit_market_fee_percent,
//                                                        charge_market_fee, 4);
         const asset_object smartbit = create_bitasset("SMARTBIT", smartissuer.id, smartbit_market_fee_percent,
                                                        charge_market_fee, 4);
         const auto &core = asset_id_type()(db);

         update_feed_producers(smartbit, {feedproducer.id});

         price_feed current_feed;
         current_feed.settlement_price = smartbit.amount(100) / core.amount(100);
         current_feed.maintenance_collateral_ratio = 1750; // need to set this explicitly, testnet has a different default
         publish_feed(smartbit, feedproducer, current_feed);

         FC_ASSERT(smartbit.bitasset_data(db).current_feed.settlement_price == current_feed.settlement_price);


         //////
         // Advance to activate hardfork
         //////
         generate_blocks(HARDFORK_BSIP_81_TIME);
         generate_block();
         trx.clear();
         set_expiration(db, trx);


         //////
         // After HF, test that new values can be set
         //////
         // Define the new taker fees
         uint16_t jill_maker_fee_percent = jill_market_fee_percent;
         uint16_t jill_taker_fee_percent = jill_maker_fee_percent / 2;

         uint16_t smartbit_maker_fee_percent = 1 * GRAPHENE_1_PERCENT;
         uint16_t smartbit_taker_fee_percent = 3 * GRAPHENE_1_PERCENT;

         // Set the new taker fee for JILLCOIN
         asset_update_operation uop;
         uop.issuer = jill.id;
         uop.asset_to_update = jillcoin.get_id();
         uop.new_options = jillcoin.options;
         uop.new_options.extensions.value.taker_fee_percent = jill_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, jill_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for JILLCOIN
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = jill_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);


         // Set the new taker fee for SMARTBIT
         uop = asset_update_operation();
         uop.issuer = smartissuer.id;
         uop.asset_to_update = smartbit.get_id();
         uop.new_options = smartbit.options;
         uop.new_options.market_fee_percent = smartbit_maker_fee_percent;
         uop.new_options.extensions.value.taker_fee_percent = smartbit_taker_fee_percent;

         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee for SMARTBIT
         updated_asset = smartbit.get_id()(db);
         expected_taker_fee_percent = smartbit_taker_fee_percent;
         BOOST_CHECK(updated_asset.options.extensions.value.taker_fee_percent.valid());
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, *updated_asset.options.extensions.value.taker_fee_percent);

         // Check the maker fee for SMARTBIT
         updated_asset = smartbit.get_id()(db);
         expected_taker_fee_percent = smartbit_maker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.market_fee_percent);


         //////
         // Create Orders 1 and 2 that will match.
         // Order 1 will be completely filled, and Order 2 will be partially filled.
         //////
         // Initialize token balance of actors
         BOOST_TEST_MESSAGE("Issuing 10 JCOIN to alice");
         issue_uia(alice, jillcoin.amount(10 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking alice's balance");
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 10 * JILL_PRECISION);

         BOOST_TEST_MESSAGE("Issuing 600 SMARTBIT to bob");
         transfer(committee_account, bob.id, asset(2 * 1000 * SMARTBIT_PRECISION));
         publish_feed(smartbit, feedproducer, current_feed); // Publish a recent feed
         borrow(bob, smartbit.amount(600 * SMARTBIT_PRECISION), asset(2 * 600 * SMARTBIT_PRECISION));
         BOOST_TEST_MESSAGE("Checking bob's balance");
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 600 * SMARTBIT_PRECISION);

         // Alice and Bob place orders which match, and are completely filled by each other
         // Alice is willing to sell 10 JILLCOIN for at least 300 SMARTBIT
         limit_order_create_operation order_1_op = create_sell_operation(alice.id,
                                                                            jillcoin.amount(10 * JILL_PRECISION),
                                                                            smartbit.amount(300 * SMARTBIT_PRECISION));
         trx.clear();
         trx.operations.push_back(order_1_op);
         asset alice_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, alice_private_key);
         processed_transaction ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type order_1_id = ptx.operation_results[0].get<object_id_type>();

         const limit_order_object *order_1_before = db.find<limit_order_object>(order_1_id);
         BOOST_CHECK(order_1_before != nullptr);


         // Bob is willing to sell 600 SMARTBIT for at least 20 JILLCOIN
         limit_order_create_operation order_2_op
                 = create_sell_operation(bob.id, smartbit.amount(600 * SMARTBIT_PRECISION),
                                         jillcoin.amount(20 * JILL_PRECISION));
         trx.clear();
         trx.operations.push_back(order_2_op);
         asset order_2_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, bob_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type order_2_id = ptx.operation_results[0].get<object_id_type>();

         // Check that order 1 was completely filled by ensuring that they it is no longer on the order book
         const limit_order_object *order_1 = db.find<limit_order_object>(order_1_id);
         BOOST_CHECK(order_1 == nullptr);
         // Check that order 2  was partially filled by ensuring that they it is still on the order book
         const limit_order_object *order_2 = db.find<limit_order_object>(order_2_id);
         BOOST_CHECK(order_2 != nullptr);


         // Check the new balances of the maker
         // Alice was the maker; she is receiving SMARTBIT
         asset expected_smartbit_fee = smartbit.amount(
                 300 * SMARTBIT_PRECISION * smartbit_maker_fee_percent / GRAPHENE_100_PERCENT);
         int64_t expected_alice_balance_after_order_2 =
                 (300 * SMARTBIT_PRECISION) - alice_sell_fee.amount.value - expected_smartbit_fee.amount.value;
         BOOST_REQUIRE_EQUAL(get_balance(alice, smartbit), expected_alice_balance_after_order_2);
         BOOST_REQUIRE_EQUAL(get_balance(alice, jillcoin), 0);

         // Check the new balance of the taker
         // Bob was the taker; he is receiving JILLCOIN
         asset expected_jill_fee = jillcoin.amount(
                 10 * JILL_PRECISION * jill_taker_fee_percent / GRAPHENE_100_PERCENT);
         int64_t expected_bob_balance_after_order_2 =
                 (10 * JILL_PRECISION) - order_2_sell_fee.amount.value - expected_jill_fee.amount.value;
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin), expected_bob_balance_after_order_2);
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 0);

         // Check the asset issuer's accumulated fees
         share_type expected_smartbit_fee_after_order_2 = expected_smartbit_fee.amount;
         share_type expected_jill_fee_after_order_2 = expected_jill_fee.amount;
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == expected_smartbit_fee_after_order_2);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee_after_order_2);


         //////
         // Create Order 3 that will the remainder of match Order 2
         //////
         // Initialize token balance of actors
         BOOST_TEST_MESSAGE("Issuing 5 JCOIN to charlie");
         trx.clear();
         issue_uia(charlie, jillcoin.amount(5 * JILL_PRECISION));
         BOOST_TEST_MESSAGE("Checking charlie's balance");
         BOOST_REQUIRE_EQUAL(get_balance(charlie, jillcoin), 5 * JILL_PRECISION);

         // Charlie is is willing to sell 5 JILLCOIN for at least 150 SMARTBIT
         limit_order_create_operation order_3_op = create_sell_operation(charlie.id,
                                                                         jillcoin.amount(5 * JILL_PRECISION),
                                                                         smartbit.amount(150 * SMARTBIT_PRECISION));
         trx.clear();
         trx.operations.push_back(order_3_op);
         asset charlie_sell_fee = db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, charlie_private_key);
         ptx = PUSH_TX(db, trx); // No exception should be thrown
         limit_order_id_type order_3_id = ptx.operation_results[0].get<object_id_type>();

         // Order 3 should be completely filled
         const limit_order_object *order_3 = db.find<limit_order_object>(order_3_id);
         BOOST_CHECK(order_3 == nullptr);

         // Order 2 should be partially filled and still present on the order books
         const limit_order_object *order_2_after = db.find<limit_order_object>(order_2_id);
         BOOST_CHECK(order_2_after != nullptr);

         // Check the new balance of the taker
         // Charlie was the taker; he is receiving SMARTBIT
         expected_smartbit_fee = smartbit.amount(
                 150 * SMARTBIT_PRECISION * smartbit_taker_fee_percent / GRAPHENE_100_PERCENT);
         int64_t expected_charlie_balance_after_order_3 =
                 (150 * SMARTBIT_PRECISION) - charlie_sell_fee.amount.value - expected_smartbit_fee.amount.value;
         BOOST_REQUIRE_EQUAL(get_balance(charlie, smartbit), expected_charlie_balance_after_order_3);
         BOOST_REQUIRE_EQUAL(get_balance(charlie, jillcoin), 0);

         // Check the new balance of the maker
         // Bob was the maker; he is receiving JILLCOIN
         asset expected_jill_order_3_fee = jillcoin.amount(
                 5 * JILL_PRECISION * jill_maker_fee_percent / GRAPHENE_100_PERCENT);
         int64_t expected_bob_balance_after_order_3 =
                 expected_bob_balance_after_order_2
                 + (5 * JILL_PRECISION) - expected_jill_order_3_fee.amount.value;
         BOOST_REQUIRE_EQUAL(get_balance(bob, jillcoin), expected_bob_balance_after_order_3);
         BOOST_REQUIRE_EQUAL(get_balance(bob, smartbit), 0);

         // Check the asset issuer's accumulated fees
         share_type expected_smartbit_fee_after_order_3 =
                 expected_smartbit_fee_after_order_2 + expected_smartbit_fee.amount;
         share_type expected_jill_fee_after_order_3 = expected_jill_fee_after_order_2 + expected_jill_fee.amount;
         BOOST_CHECK(smartbit.dynamic_asset_data_id(db).accumulated_fees == expected_smartbit_fee_after_order_3);
         BOOST_CHECK(jillcoin.dynamic_asset_data_id(db).accumulated_fees == expected_jill_fee_after_order_3);

      } FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()