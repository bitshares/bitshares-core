#include <string>
#include <boost/test/unit_test.hpp>
#include <fc/exception/exception.hpp>

#include <graphene/chain/hardfork.hpp>

#include "../common/database_fixture.hpp"


using namespace graphene::chain;
using namespace graphene::chain::test;

/**
 * BSIP81: Asset owners may specify different market fee rate for maker orders and taker orders
 */
BOOST_FIXTURE_TEST_SUITE(simple_maker_taker_fee_tests, database_fixture)

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

         upgrade_to_lifetime_member(izzy);

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
         uop.new_options.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = jillcoin.get_id()(db);
         uint16_t expected_taker_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


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
         expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, issuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = jillcoin.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);

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
         const auto &bitsmart = create_bitasset("SMARTBIT", smartissuer.id);


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
         uop.new_options.taker_fee_percent = new_taker_fee_percent;

         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?

         // Check the taker fee
         asset_object updated_asset = bitsmart.get_id()(db);
         uint16_t expected_taker_fee_percent = 0; // Before the HF it should be set to 0
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


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
         expected_taker_fee_percent = updated_asset.options.market_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);


         //////
         // After HF, test invalid taker fees
         //////
         uop.new_options.taker_fee_percent = GRAPHENE_100_PERCENT + 1;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         GRAPHENE_CHECK_THROW(PUSH_TX(db, trx), fc::exception); // An exception should be thrown indicating the reason
         // TOOD: Check the specific exception?


         //////
         // After HF, test that new values can be set
         //////
         uop.new_options.taker_fee_percent = new_taker_fee_percent;
         trx.clear();
         trx.operations.push_back(uop);
         db.current_fee_schedule().set_fee(trx.operations.back());
         sign(trx, smartissuer_private_key);
         PUSH_TX(db, trx); // No exception should be thrown

         // Check the taker fee
         updated_asset = bitsmart.get_id()(db);
         expected_taker_fee_percent = new_taker_fee_percent;
         BOOST_CHECK_EQUAL(expected_taker_fee_percent, updated_asset.options.taker_fee_percent);

      } FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()