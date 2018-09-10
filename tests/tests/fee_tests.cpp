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

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>

#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/fba_accumulator_id.hpp>

#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/exceptions.hpp>

#include <boost/test/unit_test.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( fee_tests, database_fixture )

BOOST_AUTO_TEST_CASE( nonzero_fee_test )
{
   try
   {
      ACTORS((alice)(bob));

      const share_type prec = asset::scaled_precision( asset_id_type()(db).precision );

      // Return number of core shares (times precision)
      auto _core = [&]( int64_t x ) -> asset
      {  return asset( x*prec );    };

      transfer( committee_account, alice_id, _core(1000000) );

      // make sure the database requires our fee to be nonzero
      enable_fees();

      signed_transaction tx;
      transfer_operation xfer_op;
      xfer_op.from = alice_id;
      xfer_op.to = bob_id;
      xfer_op.amount = _core(1000);
      xfer_op.fee = _core(0);
      tx.operations.push_back( xfer_op );
      set_expiration( db, tx );
      sign( tx, alice_private_key );
      GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), insufficient_fee );
   }
   catch( const fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE(asset_claim_fees_test)
{
   try
   {
      ACTORS((alice)(bob)(izzy)(jill));
      // Izzy issues asset to Alice
      // Jill issues asset to Bob
      // Alice and Bob trade in the market and pay fees
      // Verify that Izzy and Jill can claim the fees

      const share_type core_prec = asset::scaled_precision( asset_id_type()(db).precision );

      // Return number of core shares (times precision)
      auto _core = [&]( int64_t x ) -> asset
      {  return asset( x*core_prec );    };

      transfer( committee_account, alice_id, _core(1000000) );
      transfer( committee_account,   bob_id, _core(1000000) );
      transfer( committee_account,  izzy_id, _core(1000000) );
      transfer( committee_account,  jill_id, _core(1000000) );

      asset_id_type izzycoin_id = create_bitasset( "IZZYCOIN", izzy_id,   GRAPHENE_1_PERCENT, charge_market_fee ).id;
      asset_id_type jillcoin_id = create_bitasset( "JILLCOIN", jill_id, 2*GRAPHENE_1_PERCENT, charge_market_fee ).id;

      const share_type izzy_prec = asset::scaled_precision( asset_id_type(izzycoin_id)(db).precision );
      const share_type jill_prec = asset::scaled_precision( asset_id_type(jillcoin_id)(db).precision );

      auto _izzy = [&]( int64_t x ) -> asset
      {   return asset( x*izzy_prec, izzycoin_id );   };
      auto _jill = [&]( int64_t x ) -> asset
      {   return asset( x*jill_prec, jillcoin_id );   };

      update_feed_producers( izzycoin_id(db), { izzy_id } );
      update_feed_producers( jillcoin_id(db), { jill_id } );

      const asset izzy_satoshi = asset(1, izzycoin_id);
      const asset jill_satoshi = asset(1, jillcoin_id);

      // Izzycoin is worth 100 BTS
      price_feed feed;
      feed.settlement_price = price( _izzy(1), _core(100) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( izzycoin_id(db), izzy, feed );

      // Jillcoin is worth 30 BTS
      feed.settlement_price = price( _jill(1), _core(30) );
      feed.maintenance_collateral_ratio = 175 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      feed.maximum_short_squeeze_ratio = 150 * GRAPHENE_COLLATERAL_RATIO_DENOM / 100;
      publish_feed( jillcoin_id(db), jill, feed );

      enable_fees();

      // Alice and Bob create some coins
      borrow( alice_id, _izzy( 200), _core( 60000) );
      borrow(   bob_id, _jill(2000), _core(180000) );

      // Alice and Bob place orders which match
      create_sell_order( alice_id, _izzy(100), _jill(300) );   // Alice is willing to sell her Izzy's for 3 Jill
      create_sell_order(   bob_id, _jill(700), _izzy(200) );   // Bob is buying up to 200 Izzy's for up to 3.5 Jill

      // 100 Izzys and 300 Jills are matched, so the fees should be
      //   1 Izzy (1%) and 6 Jill (2%).

      auto claim_fees = [&]( account_id_type issuer, asset amount_to_claim )
      {
         asset_claim_fees_operation claim_op;
         claim_op.issuer = issuer;
         claim_op.amount_to_claim = amount_to_claim;
         signed_transaction tx;
         tx.operations.push_back( claim_op );
         db.current_fee_schedule().set_fee( tx.operations.back() );
         set_expiration( db, tx );
         fc::ecc::private_key   my_pk = (issuer == izzy_id) ? izzy_private_key : jill_private_key;
         fc::ecc::private_key your_pk = (issuer == izzy_id) ? jill_private_key : izzy_private_key;
         sign( tx, your_pk );
         GRAPHENE_REQUIRE_THROW( PUSH_TX( db, tx ), fc::exception );
         tx.clear_signatures();
         sign( tx, my_pk );
         PUSH_TX( db, tx );
      };

      {
         const asset_object& izzycoin = izzycoin_id(db);
         const asset_object& jillcoin = jillcoin_id(db);

         //wdump( (izzycoin)(izzycoin.dynamic_asset_data_id(db))((*izzycoin.bitasset_data_id)(db)) );
         //wdump( (jillcoin)(jillcoin.dynamic_asset_data_id(db))((*jillcoin.bitasset_data_id)(db)) );

         // check the correct amount of fees has been awarded
         BOOST_CHECK( izzycoin.dynamic_asset_data_id(db).accumulated_fees == _izzy(1).amount );
         BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == _jill(6).amount );

      }

      if( db.head_block_time() <= HARDFORK_413_TIME )
      {
         // can't claim before hardfork
         GRAPHENE_REQUIRE_THROW( claim_fees( izzy_id, _izzy(1) ), fc::exception );
         generate_blocks( HARDFORK_413_TIME );
         while( db.head_block_time() <= HARDFORK_413_TIME )
         {
            generate_block();
         }
      }

      {
         const asset_object& izzycoin = izzycoin_id(db);
         const asset_object& jillcoin = jillcoin_id(db);

         // can't claim more than balance
         GRAPHENE_REQUIRE_THROW( claim_fees( izzy_id, _izzy(1) + izzy_satoshi ), fc::exception );
         GRAPHENE_REQUIRE_THROW( claim_fees( jill_id, _jill(6) + jill_satoshi ), fc::exception );

         // can't claim asset that doesn't belong to you
         GRAPHENE_REQUIRE_THROW( claim_fees( jill_id, izzy_satoshi ), fc::exception );
         GRAPHENE_REQUIRE_THROW( claim_fees( izzy_id, jill_satoshi ), fc::exception );

         // can claim asset in one go
         claim_fees( izzy_id, _izzy(1) );
         GRAPHENE_REQUIRE_THROW( claim_fees( izzy_id, izzy_satoshi ), fc::exception );
         BOOST_CHECK( izzycoin.dynamic_asset_data_id(db).accumulated_fees == _izzy(0).amount );

         // can claim in multiple goes
         claim_fees( jill_id, _jill(4) );
         BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == _jill(2).amount );
         GRAPHENE_REQUIRE_THROW( claim_fees( jill_id, _jill(2) + jill_satoshi ), fc::exception );
         claim_fees( jill_id, _jill(2) );
         BOOST_CHECK( jillcoin.dynamic_asset_data_id(db).accumulated_fees == _jill(0).amount );
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE(asset_claim_pool_test)
{
    try
    {
        ACTORS((alice)(bob));
        // Alice and Bob create some user issued assets
        // Alice deposits BTS to the fee pool
        // Alice claimes fee pool of her asset and can't claim pool of Bob's asset

        const share_type core_prec = asset::scaled_precision( asset_id_type()(db).precision );

        // return number of core shares (times precision)
        auto _core = [&core_prec]( int64_t x ) -> asset
        {  return asset( x*core_prec );    };

        const asset_object& alicecoin = create_user_issued_asset( "ALICECOIN", alice,  0 );
        const asset_object& aliceusd = create_user_issued_asset( "ALICEUSD", alice, 0 );

        asset_id_type alicecoin_id = alicecoin.id;
        asset_id_type aliceusd_id = aliceusd.id;
        asset_id_type bobcoin_id = create_user_issued_asset( "BOBCOIN", bob, 0).id;

        // prepare users' balance
        issue_uia( alice, aliceusd.amount( 20000000 ) );
        issue_uia( alice, alicecoin.amount( 10000000 ) );

        transfer( committee_account, alice_id, _core(1000) );
        transfer( committee_account, bob_id, _core(1000) );

        enable_fees();

        auto claim_pool = [&]( const account_id_type issuer, const asset_id_type asset_to_claim,
                              const asset& amount_to_fund, const asset_object& fee_asset  )
        {
            asset_claim_pool_operation claim_op;
            claim_op.issuer = issuer;
            claim_op.asset_id = asset_to_claim;
            claim_op.amount_to_claim = amount_to_fund;

            signed_transaction tx;
            tx.operations.push_back( claim_op );
            db.current_fee_schedule().set_fee( tx.operations.back(), fee_asset.options.core_exchange_rate );
            set_expiration( db, tx );
            sign( tx, alice_private_key );
            PUSH_TX( db, tx );

        };

        auto claim_pool_proposal = [&]( const account_id_type issuer, const asset_id_type asset_to_claim,
                                        const asset& amount_to_fund, const asset_object& fee_asset  )
        {
            asset_claim_pool_operation claim_op;
            claim_op.issuer = issuer;
            claim_op.asset_id = asset_to_claim;
            claim_op.amount_to_claim = amount_to_fund;

            const auto& curfees = *db.get_global_properties().parameters.current_fees;
            const auto& proposal_create_fees = curfees.get<proposal_create_operation>();
            proposal_create_operation prop;
            prop.fee_paying_account = alice_id;
            prop.proposed_ops.emplace_back( claim_op );
            prop.expiration_time =  db.head_block_time() + fc::days(1);
            prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );

            signed_transaction tx;
            tx.operations.push_back( prop );
            db.current_fee_schedule().set_fee( tx.operations.back(), fee_asset.options.core_exchange_rate );
            set_expiration( db, tx );
            sign( tx, alice_private_key );
            PUSH_TX( db, tx );

        };

        const asset_object& core_asset = asset_id_type()(db);

        // deposit 100 BTS to the fee pool of ALICEUSD asset
        fund_fee_pool( alice_id(db), aliceusd_id(db), _core(100).amount );

        // Unable to claim pool before the hardfork
        GRAPHENE_REQUIRE_THROW( claim_pool( alice_id, aliceusd_id, _core(1), core_asset), fc::exception );
        GRAPHENE_REQUIRE_THROW( claim_pool_proposal( alice_id, aliceusd_id, _core(1), core_asset), fc::exception );

        // Fast forward to hard fork date
        generate_blocks( HARDFORK_CORE_188_TIME );

        // New reference for core_asset after having produced blocks
        const asset_object& core_asset_hf = asset_id_type()(db);

        // can't claim pool because it is empty
        GRAPHENE_REQUIRE_THROW( claim_pool( alice_id, alicecoin_id, _core(1), core_asset_hf), fc::exception );

        // deposit 300 BTS to the fee pool of ALICECOIN asset
        fund_fee_pool( alice_id(db), alicecoin_id(db), _core(300).amount );

        // Test amount of CORE in fee pools
        BOOST_CHECK( alicecoin_id(db).dynamic_asset_data_id(db).fee_pool == _core(300).amount );
        BOOST_CHECK( aliceusd_id(db).dynamic_asset_data_id(db).fee_pool == _core(100).amount );

        // can't claim pool of an asset that doesn't belong to you
        GRAPHENE_REQUIRE_THROW( claim_pool( alice_id, bobcoin_id, _core(200), core_asset_hf), fc::exception );

        // can't claim more than is available in the fee pool
        GRAPHENE_REQUIRE_THROW( claim_pool( alice_id, alicecoin_id, _core(400), core_asset_hf ), fc::exception );

        // can't pay fee in the same asset whose pool is being drained
        GRAPHENE_REQUIRE_THROW( claim_pool( alice_id, alicecoin_id, _core(200), alicecoin_id(db) ), fc::exception );

        // can claim BTS back from the fee pool
        claim_pool( alice_id, alicecoin_id, _core(200), core_asset_hf );
        BOOST_CHECK( alicecoin_id(db).dynamic_asset_data_id(db).fee_pool == _core(100).amount );

        // can pay fee in the asset other than the one whose pool is being drained
        share_type balance_before_claim = get_balance( alice_id, asset_id_type() );
        claim_pool( alice_id, alicecoin_id, _core(100), aliceusd_id(db) );
        BOOST_CHECK( alicecoin_id(db).dynamic_asset_data_id(db).fee_pool == _core(0).amount );

        //check balance after claiming pool
        share_type current_balance = get_balance( alice_id, asset_id_type() );
        BOOST_CHECK( balance_before_claim + _core(100).amount == current_balance );

        // can create a proposal to claim claim pool after hard fork
        claim_pool_proposal( alice_id, aliceusd_id, _core(1), core_asset_hf);
    }
    FC_LOG_AND_RETHROW()
}

///////////////////////////////////////////////////////////////
// cashback_test infrastructure                              //
///////////////////////////////////////////////////////////////

#define CHECK_BALANCE( actor_name, amount ) \
   BOOST_CHECK_EQUAL( get_balance( actor_name ## _id, asset_id_type() ), amount )

#define CHECK_VESTED_CASHBACK( actor_name, amount ) \
   BOOST_CHECK_EQUAL( actor_name ## _id(db).statistics(db).pending_vested_fees.value, amount )

#define CHECK_UNVESTED_CASHBACK( actor_name, amount ) \
   BOOST_CHECK_EQUAL( actor_name ## _id(db).statistics(db).pending_fees.value, amount )

#define GET_CASHBACK_BALANCE( account ) \
   ( (account.cashback_vb.valid()) \
   ? account.cashback_balance(db).balance.amount.value \
   : 0 )

#define CHECK_CASHBACK_VBO( actor_name, _amount ) \
   BOOST_CHECK_EQUAL( GET_CASHBACK_BALANCE( actor_name ## _id(db) ), _amount )

#define P100 GRAPHENE_100_PERCENT
#define P1 GRAPHENE_1_PERCENT

uint64_t pct( uint64_t percentage, uint64_t val )
{
   fc::uint128_t x = percentage;
   x *= val;
   x /= GRAPHENE_100_PERCENT;
   return x.to_uint64();
}

uint64_t pct( uint64_t percentage0, uint64_t percentage1, uint64_t val )
{
   return pct( percentage1, pct( percentage0, val ) );
}

uint64_t pct( uint64_t percentage0, uint64_t percentage1, uint64_t percentage2, uint64_t val )
{
   return pct( percentage2, pct( percentage1, pct( percentage0, val ) ) );
}

struct actor_audit
{
   int64_t b0 = 0;      // starting balance parameter
   int64_t bal = 0;     // balance should be this
   int64_t ubal = 0;    // unvested balance (in VBO) should be this
   int64_t ucb = 0;     // unvested cashback in account_statistics should be this
   int64_t vcb = 0;     // vested cashback in account_statistics should be this
   int64_t ref_pct = 0; // referrer percentage should be this
};

BOOST_AUTO_TEST_CASE( cashback_test )
{ try {
   /*                        Account Structure used in this test                         *
    *                                                                                    *
    *               /-----------------\       /-------------------\                      *
    *               | life (Lifetime) |       |  rog (Lifetime)   |                      *
    *               \-----------------/       \-------------------/                      *
    *                  | Ref&Reg    | Refers     | Registers  | Registers                *
    *                  |            | 75         | 25         |                          *
    *                  v            v            v            |                          *
    *  /----------------\         /----------------\          |                          *
    *  |  ann (Annual)  |         |  dumy (basic)  |          |                          *
    *  \----------------/         \----------------/          |-------------.            *
    * 80 | Refers      L--------------------------------.     |             |            *
    *    v                     Refers                80 v     v 20          |            *
    *  /----------------\                         /----------------\        |            *
    *  |  scud (basic)  |<------------------------|  stud (basic)  |        |            *
    *  \----------------/ 20   Registers          | (Upgrades to   |        | 5          *
    *                                             |   Lifetime)    |        v            *
    *                                             \----------------/   /--------------\  *
    *                                                         L------->| pleb (Basic) |  *
    *                                                       95 Refers  \--------------/  *
    *                                                                                    *
    * Fee distribution chains (80-20 referral/net split, 50-30 referrer/LTM split)       *
    * life : 80% -> life, 20% -> net                                                     *
    * rog: 80% -> rog, 20% -> net                                                        *
    * ann (before upg): 80% -> life, 20% -> net                                          *
    * ann (after upg): 80% * 5/8 -> ann, 80% * 3/8 -> life, 20% -> net                   *
    * stud (before upg): 80% * 5/8 -> ann, 80% * 3/8 -> life, 20% * 80% -> rog,          *
    *                    20% -> net                                                      *
    * stud (after upg): 80% -> stud, 20% -> net                                          *
    * dumy : 75% * 80% -> life, 25% * 80% -> rog, 20% -> net                             *
    * scud : 80% * 5/8 -> ann, 80% * 3/8 -> life, 20% * 80% -> stud, 20% -> net          *
    * pleb : 95% * 80% -> stud, 5% * 80% -> rog, 20% -> net                              *
    */

   BOOST_TEST_MESSAGE("Creating actors");

   ACTOR(life);
   ACTOR(rog);
   PREP_ACTOR(ann);
   PREP_ACTOR(scud);
   PREP_ACTOR(dumy);
   PREP_ACTOR(stud);
   PREP_ACTOR(pleb);
   // use ##_public_key vars to silence unused variable warning
   BOOST_CHECK_GT(ann_public_key.key_data.size(), 0);
   BOOST_CHECK_GT(scud_public_key.key_data.size(), 0);
   BOOST_CHECK_GT(dumy_public_key.key_data.size(), 0);
   BOOST_CHECK_GT(stud_public_key.key_data.size(), 0);
   BOOST_CHECK_GT(pleb_public_key.key_data.size(), 0);

   account_id_type ann_id, scud_id, dumy_id, stud_id, pleb_id;
   actor_audit alife, arog, aann, ascud, adumy, astud, apleb;

   alife.b0 = 100000000;
   arog.b0 = 100000000;
   aann.b0 = 1000000;
   astud.b0 = 1000000;
   astud.ref_pct = 80 * GRAPHENE_1_PERCENT;
   ascud.ref_pct = 80 * GRAPHENE_1_PERCENT;
   adumy.ref_pct = 75 * GRAPHENE_1_PERCENT;
   apleb.ref_pct = 95 * GRAPHENE_1_PERCENT;

   transfer(account_id_type(), life_id, asset(alife.b0));
   alife.bal += alife.b0;
   transfer(account_id_type(), rog_id, asset(arog.b0));
   arog.bal += arog.b0;
   upgrade_to_lifetime_member(life_id);
   upgrade_to_lifetime_member(rog_id);

   BOOST_TEST_MESSAGE("Enable fees");
   const auto& fees = db.get_global_properties().parameters.current_fees;

#define CustomRegisterActor(actor_name, registrar_name, referrer_name, referrer_rate) \
   { \
      account_create_operation op; \
      op.registrar = registrar_name ## _id; \
      op.referrer = referrer_name ## _id; \
      op.referrer_percent = referrer_rate*GRAPHENE_1_PERCENT; \
      op.name = BOOST_PP_STRINGIZE(actor_name); \
      op.options.memo_key = actor_name ## _private_key.get_public_key(); \
      op.active = authority(1, public_key_type(actor_name ## _private_key.get_public_key()), 1); \
      op.owner = op.active; \
      op.fee = fees->calculate_fee(op); \
      trx.operations = {op}; \
      sign( trx,  registrar_name ## _private_key ); \
      actor_name ## _id = PUSH_TX( db, trx ).operation_results.front().get<object_id_type>(); \
      trx.clear(); \
   }
#define CustomAuditActor(actor_name)                                \
   if( actor_name ## _id != account_id_type() )                     \
   {                                                                \
      CHECK_BALANCE( actor_name, a ## actor_name.bal );             \
      CHECK_VESTED_CASHBACK( actor_name, a ## actor_name.vcb );     \
      CHECK_UNVESTED_CASHBACK( actor_name, a ## actor_name.ucb );   \
      CHECK_CASHBACK_VBO( actor_name, a ## actor_name.ubal );       \
   }

#define CustomAudit()                                \
   {                                                 \
      CustomAuditActor( life );                      \
      CustomAuditActor( rog );                       \
      CustomAuditActor( ann );                       \
      CustomAuditActor( stud );                      \
      CustomAuditActor( dumy );                      \
      CustomAuditActor( scud );                      \
      CustomAuditActor( pleb );                      \
   }

   int64_t reg_fee    = fees->get< account_create_operation >().premium_fee;
   int64_t xfer_fee   = fees->get< transfer_operation >().fee;
   int64_t upg_an_fee = fees->get< account_upgrade_operation >().membership_annual_fee;
   int64_t upg_lt_fee = fees->get< account_upgrade_operation >().membership_lifetime_fee;
   // all percentages here are cut from whole pie!
   uint64_t network_pct = 20 * P1;
   uint64_t lt_pct = 375 * P100 / 1000;

   BOOST_TEST_MESSAGE("Register and upgrade Ann");
   {
      CustomRegisterActor(ann, life, life, 75);
      alife.vcb += reg_fee; alife.bal += -reg_fee;
      CustomAudit();

      transfer(life_id, ann_id, asset(aann.b0));
      alife.vcb += xfer_fee; alife.bal += -xfer_fee -aann.b0; aann.bal += aann.b0;
      CustomAudit();

      upgrade_to_annual_member(ann_id);
      aann.ucb += upg_an_fee; aann.bal += -upg_an_fee;

      // audit distribution of fees from Ann
      alife.ubal += pct( P100-network_pct, aann.ucb );
      alife.bal  += pct( P100-network_pct, aann.vcb );
      aann.ucb = 0; aann.vcb = 0;
      CustomAudit();
   }

   BOOST_TEST_MESSAGE("Register dumy and stud");
   CustomRegisterActor(dumy, rog, life, 75);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   CustomAudit();

   CustomRegisterActor(stud, rog, ann, 80);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   CustomAudit();

   BOOST_TEST_MESSAGE("Upgrade stud to lifetime member");

   transfer(life_id, stud_id, asset(astud.b0));
   alife.vcb += xfer_fee; alife.bal += -astud.b0 -xfer_fee; astud.bal += astud.b0;
   CustomAudit();

   upgrade_to_lifetime_member(stud_id);
   astud.ucb += upg_lt_fee; astud.bal -= upg_lt_fee;

/*
network_cut:   20000
referrer_cut:  40000 -> ann
registrar_cut: 10000 -> rog
lifetime_cut:  30000 -> life

NET : net
LTM : net' ltm
REF : net' ltm' ref
REG : net' ltm' ref'
*/

   // audit distribution of fees from stud
   alife.ubal += pct( P100-network_pct,      lt_pct,                     astud.ucb );
   aann.ubal  += pct( P100-network_pct, P100-lt_pct,      astud.ref_pct, astud.ucb );
   arog.ubal  += pct( P100-network_pct, P100-lt_pct, P100-astud.ref_pct, astud.ucb );
   astud.ucb  = 0;
   CustomAudit();

   BOOST_TEST_MESSAGE("Register pleb and scud");

   CustomRegisterActor(pleb, rog, stud, 95);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   CustomAudit();

   CustomRegisterActor(scud, stud, ann, 80);
   astud.vcb += reg_fee; astud.bal += -reg_fee;
   CustomAudit();

   generate_block();

   BOOST_TEST_MESSAGE("Wait for maintenance interval");

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   // audit distribution of fees from life
   alife.ubal += pct( P100-network_pct, alife.ucb +alife.vcb );
   alife.ucb = 0; alife.vcb = 0;

   // audit distribution of fees from rog
   arog.ubal += pct( P100-network_pct, arog.ucb + arog.vcb );
   arog.ucb = 0; arog.vcb = 0;

   // audit distribution of fees from ann
   alife.ubal += pct( P100-network_pct,      lt_pct,                    aann.ucb+aann.vcb );
   aann.ubal  += pct( P100-network_pct, P100-lt_pct,      aann.ref_pct, aann.ucb+aann.vcb );
   alife.ubal += pct( P100-network_pct, P100-lt_pct, P100-aann.ref_pct, aann.ucb+aann.vcb );
   aann.ucb = 0; aann.vcb = 0;

   // audit distribution of fees from stud
   astud.ubal += pct( P100-network_pct,                                  astud.ucb+astud.vcb );
   astud.ucb = 0; astud.vcb = 0;

   // audit distribution of fees from dumy
   alife.ubal += pct( P100-network_pct,      lt_pct,                     adumy.ucb+adumy.vcb );
   alife.ubal += pct( P100-network_pct, P100-lt_pct,      adumy.ref_pct, adumy.ucb+adumy.vcb );
   arog.ubal  += pct( P100-network_pct, P100-lt_pct, P100-adumy.ref_pct, adumy.ucb+adumy.vcb );
   adumy.ucb = 0; adumy.vcb = 0;

   // audit distribution of fees from scud
   alife.ubal += pct( P100-network_pct,      lt_pct,                     ascud.ucb+ascud.vcb );
   aann.ubal  += pct( P100-network_pct, P100-lt_pct,      ascud.ref_pct, ascud.ucb+ascud.vcb );
   astud.ubal += pct( P100-network_pct, P100-lt_pct, P100-ascud.ref_pct, ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   // audit distribution of fees from pleb
   astud.ubal += pct( P100-network_pct,      lt_pct,                     apleb.ucb+apleb.vcb );
   astud.ubal += pct( P100-network_pct, P100-lt_pct,      apleb.ref_pct, apleb.ucb+apleb.vcb );
   arog.ubal  += pct( P100-network_pct, P100-lt_pct, P100-apleb.ref_pct, apleb.ucb+apleb.vcb );
   apleb.ucb = 0; apleb.vcb = 0;

   CustomAudit();

   BOOST_TEST_MESSAGE("Doing some transfers");

   transfer(stud_id, scud_id, asset(500000));
   astud.bal += -500000-xfer_fee; astud.vcb += xfer_fee; ascud.bal += 500000;
   CustomAudit();

   transfer(scud_id, pleb_id, asset(400000));
   ascud.bal += -400000-xfer_fee; ascud.vcb += xfer_fee; apleb.bal += 400000;
   CustomAudit();

   transfer(pleb_id, dumy_id, asset(300000));
   apleb.bal += -300000-xfer_fee; apleb.vcb += xfer_fee; adumy.bal += 300000;
   CustomAudit();

   transfer(dumy_id, rog_id, asset(200000));
   adumy.bal += -200000-xfer_fee; adumy.vcb += xfer_fee; arog.bal += 200000;
   CustomAudit();

   BOOST_TEST_MESSAGE("Waiting for maintenance time");

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // audit distribution of fees from life
   alife.ubal += pct( P100-network_pct, alife.ucb +alife.vcb );
   alife.ucb = 0; alife.vcb = 0;

   // audit distribution of fees from rog
   arog.ubal += pct( P100-network_pct, arog.ucb + arog.vcb );
   arog.ucb = 0; arog.vcb = 0;

   // audit distribution of fees from ann
   alife.ubal += pct( P100-network_pct,      lt_pct,                    aann.ucb+aann.vcb );
   aann.ubal  += pct( P100-network_pct, P100-lt_pct,      aann.ref_pct, aann.ucb+aann.vcb );
   alife.ubal += pct( P100-network_pct, P100-lt_pct, P100-aann.ref_pct, aann.ucb+aann.vcb );
   aann.ucb = 0; aann.vcb = 0;

   // audit distribution of fees from stud
   astud.ubal += pct( P100-network_pct,                                  astud.ucb+astud.vcb );
   astud.ucb = 0; astud.vcb = 0;

   // audit distribution of fees from dumy
   alife.ubal += pct( P100-network_pct,      lt_pct,                     adumy.ucb+adumy.vcb );
   alife.ubal += pct( P100-network_pct, P100-lt_pct,      adumy.ref_pct, adumy.ucb+adumy.vcb );
   arog.ubal  += pct( P100-network_pct, P100-lt_pct, P100-adumy.ref_pct, adumy.ucb+adumy.vcb );
   adumy.ucb = 0; adumy.vcb = 0;

   // audit distribution of fees from scud
   alife.ubal += pct( P100-network_pct,      lt_pct,                     ascud.ucb+ascud.vcb );
   aann.ubal  += pct( P100-network_pct, P100-lt_pct,      ascud.ref_pct, ascud.ucb+ascud.vcb );
   astud.ubal += pct( P100-network_pct, P100-lt_pct, P100-ascud.ref_pct, ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   // audit distribution of fees from pleb
   astud.ubal += pct( P100-network_pct,      lt_pct,                     apleb.ucb+apleb.vcb );
   astud.ubal += pct( P100-network_pct, P100-lt_pct,      apleb.ref_pct, apleb.ucb+apleb.vcb );
   arog.ubal  += pct( P100-network_pct, P100-lt_pct, P100-apleb.ref_pct, apleb.ucb+apleb.vcb );
   apleb.ucb = 0; apleb.vcb = 0;

   CustomAudit();

   BOOST_TEST_MESSAGE("Waiting for annual membership to expire");

   generate_blocks(ann_id(db).membership_expiration_date);
   generate_block();

   BOOST_TEST_MESSAGE("Transferring from scud to pleb");

   //ann's membership has expired, so scud's fee should go up to life instead.
   transfer(scud_id, pleb_id, asset(10));
   ascud.bal += -10-xfer_fee; ascud.vcb += xfer_fee; apleb.bal += 10;
   CustomAudit();

   BOOST_TEST_MESSAGE("Waiting for maint interval");

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

   // audit distribution of fees from scud
   alife.ubal += pct( P100-network_pct,      lt_pct,                     ascud.ucb+ascud.vcb );
   alife.ubal += pct( P100-network_pct, P100-lt_pct,      ascud.ref_pct, ascud.ucb+ascud.vcb );
   astud.ubal += pct( P100-network_pct, P100-lt_pct, P100-ascud.ref_pct, ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   CustomAudit();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( account_create_fee_scaling )
{ try {
   auto accounts_per_scale = db.get_global_properties().parameters.accounts_per_fee_scale;
   db.modify(global_property_id_type()(db), [](global_property_object& gpo)
   {
      gpo.parameters.current_fees = fee_schedule::get_default();
      gpo.parameters.current_fees->get<account_create_operation>().basic_fee = 1;
   });

   for( int i = db.get_dynamic_global_properties().accounts_registered_this_interval; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<account_create_operation>().basic_fee, 1);
      create_account("shill" + fc::to_string(i));
   }
   for( int i = 0; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<account_create_operation>().basic_fee, 16);
      create_account("moreshills" + fc::to_string(i));
   }
   for( int i = 0; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<account_create_operation>().basic_fee, 256);
      create_account("moarshills" + fc::to_string(i));
   }
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<account_create_operation>().basic_fee, 4096);

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.current_fees->get<account_create_operation>().basic_fee, 1);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( fee_refund_test )
{
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      asset_id_type usd_id = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee ).id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      int64_t order_create_fee = 537;
      int64_t order_cancel_fee = 129;

      uint32_t skip = database::skip_witness_signature
                    | database::skip_transaction_signatures
                    | database::skip_transaction_dupe_check
                    | database::skip_block_size_check
                    | database::skip_tapos_check
                    | database::skip_authority_check
                    | database::skip_merkle_check
                    ;

      generate_block( skip );

      for( int i=0; i<2; i++ )
      {
         if( i == 1 )
         {
            generate_blocks( HARDFORK_445_TIME, true, skip );
            generate_block( skip );
         }

         // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
         // so we have to do it every time we stop generating/popping blocks and start doing tx's
         enable_fees();
         /*
         change_fees({
                       limit_order_create_operation::fee_parameters_type { order_create_fee },
                       limit_order_cancel_operation::fee_parameters_type { order_cancel_fee }
                     });
         */
         // C++ -- The above commented out statement doesn't work, I don't know why
         // so we will use the following rather lengthy initialization instead
         {
            flat_set< fee_parameters > new_fees;
            {
               limit_order_create_operation::fee_parameters_type create_fee_params;
               create_fee_params.fee = order_create_fee;
               new_fees.insert( create_fee_params );
            }
            {
               limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
               cancel_fee_params.fee = order_cancel_fee;
               new_fees.insert( cancel_fee_params );
            }
            change_fees( new_fees );
         }

         // Alice creates order
         // Bob creates order which doesn't match

         // AAAAGGHH create_sell_order reads trx.expiration #469
         set_expiration( db, trx );

         // Check non-overlapping

         limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(1000, usd_id) )->id;
         limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1000) )->id;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - 1000 - order_create_fee );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - order_create_fee );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 - 500 );

         // Bob cancels order
         cancel_limit_order( bo1_id(db) );

         int64_t cancel_net_fee;
         if( db.head_block_time() > HARDFORK_445_TIME )
            cancel_net_fee = order_cancel_fee;
         else
            cancel_net_fee = order_create_fee + order_cancel_fee;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - 1000 - order_create_fee );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - cancel_net_fee );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 );

         // Alice cancels order
         cancel_limit_order( ao1_id(db) );

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - cancel_net_fee );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 );

         // Check partial fill
         const limit_order_object* ao2 = create_sell_order( alice_id, asset(1000), asset(200, usd_id) );
         const limit_order_object* bo2 = create_sell_order(   bob_id, asset(100, usd_id), asset(500) );

         BOOST_CHECK( ao2 != nullptr );
         BOOST_CHECK( bo2 == nullptr );

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee - order_create_fee - 1000 );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 + 100 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ),   bob_b0 - cancel_net_fee - order_create_fee + 500 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ),   bob_b0 - 100 );

         // cancel Alice order, show that entire deferred_fee was consumed by partial match
         cancel_limit_order( *ao2 );

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee - order_create_fee - 500 - order_cancel_fee );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 + 100 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ),   bob_b0 - cancel_net_fee - order_create_fee + 500 );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ),   bob_b0 - 100 );

         // TODO: Check multiple fill
         // there really should be a test case involving Alice creating multiple orders matched by single Bob order
         // but we'll save that for future cleanup

         // undo above tx's and reset
         generate_block( skip );
         db.pop_block();
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( non_core_fee_refund_test )
{
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;
      int64_t pool_0 = 100000, accum_0 = 0;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee );
      asset_id_type usd_id = usd_obj.id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

      int64_t order_create_fee = 537;
      int64_t order_cancel_fee = 129;

      uint32_t skip = database::skip_witness_signature
                    | database::skip_transaction_signatures
                    | database::skip_transaction_dupe_check
                    | database::skip_block_size_check
                    | database::skip_tapos_check
                    | database::skip_authority_check
                    | database::skip_merkle_check
                    ;

      generate_block( skip );

      flat_set< fee_parameters > new_fees;
      {
         limit_order_create_operation::fee_parameters_type create_fee_params;
         create_fee_params.fee = order_create_fee;
         new_fees.insert( create_fee_params );
      }
      {
         limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
         cancel_fee_params.fee = order_cancel_fee;
         new_fees.insert( cancel_fee_params );
      }
      {
         transfer_operation::fee_parameters_type transfer_fee_params;
         transfer_fee_params.fee = 0;
         transfer_fee_params.price_per_kbyte = 0;
         new_fees.insert( transfer_fee_params );
      }

      for( int i=0; i<4; i++ )
      {
         bool expire_order = ( i % 2 != 0 );
         bool before_hardfork_445 = ( i < 2 );
         if( i == 2 )
         {
            generate_blocks( HARDFORK_445_TIME, true, skip );
            generate_block( skip );
         }

         // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
         // so we have to do it every time we stop generating/popping blocks and start doing tx's
         enable_fees();
         change_fees( new_fees );

         // AAAAGGHH create_sell_order reads trx.expiration #469
         set_expiration( db, trx );

         // prepare params
         uint32_t blocks_generated = 0;
         time_point_sec max_exp = time_point_sec::maximum();
         time_point_sec exp = db.head_block_time(); // order will be accepted when pushing trx then expired at current block
         price cer( asset(1), asset(1, usd_id) );
         const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

         // balance data
         int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
         int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
         int64_t pool_b = pool_0, accum_b = accum_0;

         // refund data
         int64_t core_fee_refund_core;
         int64_t core_fee_refund_usd;
         int64_t usd_fee_refund_core;
         int64_t usd_fee_refund_usd;
         if( db.head_block_time() > HARDFORK_445_TIME )
         {
            core_fee_refund_core = order_create_fee;
            core_fee_refund_usd = 0;
            usd_fee_refund_core = order_create_fee;
            usd_fee_refund_usd = 0;
         }
         else
         {
            core_fee_refund_core = 0;
            core_fee_refund_usd = 0;
            usd_fee_refund_core = 0;
            usd_fee_refund_usd = 0;
         }

         // Check non-overlapping
         // Alice creates order
         // Bob creates order which doesn't match
         limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(1000, usd_id) )->id;
         limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1000), exp, cer )->id;

         alice_bc -= order_create_fee;
         alice_bc -= 1000;
         bob_bu -= order_create_fee;
         bob_bu -= 500;
         pool_b -= order_create_fee;
         accum_b += order_create_fee;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob cancels order
         if( !expire_order )
            cancel_limit_order( bo1_id(db) );
         else
         {
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }

         if( !expire_order || !before_hardfork_445 )
            bob_bc -= order_cancel_fee;
         // else do nothing: before hard fork 445, no fee on expired order
         bob_bc += usd_fee_refund_core;
         bob_bu += 500;
         bob_bu += usd_fee_refund_usd;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );


         // Alice cancels order
         cancel_limit_order( ao1_id(db) );

         alice_bc -= order_cancel_fee;
         alice_bc += 1000;
         alice_bc += core_fee_refund_core;
         alice_bu += core_fee_refund_usd;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Check partial fill
         const limit_order_object* ao2 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), exp, cer );
         const limit_order_id_type ao2id = ao2->id;
         const limit_order_object* bo2 = create_sell_order(   bob_id, asset(100, usd_id), asset(500) );

         BOOST_CHECK( db.find<limit_order_object>( ao2id ) != nullptr );
         BOOST_CHECK( bo2 == nullptr );

         // data after order created
         alice_bc -= 1000;
         alice_bu -= order_create_fee;
         pool_b -= order_create_fee;
         accum_b += order_create_fee;
         bob_bc -= order_create_fee;
         bob_bu -= 100;

         // data after order filled
         alice_bu += 100;
         bob_bc += 500;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Alice order, show that entire deferred_fee was consumed by partial match
         if( !expire_order )
            cancel_limit_order( *ao2 );
         else
         {
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }


         if( !expire_order )
            alice_bc -= order_cancel_fee;
         // else do nothing:
         //         before hard fork 445, no fee when order is expired;
         //         after hard fork 445, when partially filled order expired, order cancel fee is capped at 0
         alice_bc += 500;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Check multiple fill
         // Alice creating multiple orders
         const limit_order_object* ao31 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao32 = create_sell_order( alice_id, asset(1000), asset(2000, usd_id), max_exp, cer );
         const limit_order_object* ao33 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao34 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao35 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );

         const limit_order_id_type ao31id = ao31->id;
         const limit_order_id_type ao32id = ao32->id;
         const limit_order_id_type ao33id = ao33->id;
         const limit_order_id_type ao34id = ao34->id;
         const limit_order_id_type ao35id = ao35->id;

         alice_bc -= 1000 * 5;
         alice_bu -= order_create_fee * 5;
         pool_b -= order_create_fee * 5;
         accum_b += order_create_fee * 5;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob creating an order matching multiple Alice's orders
         const limit_order_object* bo31 = create_sell_order(   bob_id, asset(500, usd_id), asset(2500), exp );

         BOOST_CHECK( db.find<limit_order_object>( ao31id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao32id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao33id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao34id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao35id ) != nullptr );
         BOOST_CHECK( bo31 == nullptr );

         // data after order created
         bob_bc -= order_create_fee;
         bob_bu -= 500;

         // data after order filled
         alice_bu += 500;
         bob_bc += 2500;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob creating an order matching multiple Alice's orders
         const limit_order_object* bo32 = create_sell_order(   bob_id, asset(500, usd_id), asset(2500), exp );

         BOOST_CHECK( db.find<limit_order_object>( ao31id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao32id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao33id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao34id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao35id ) == nullptr );
         BOOST_CHECK( bo32 != nullptr );

         // data after order created
         bob_bc -= order_create_fee;
         bob_bu -= 500;

         // data after order filled
         alice_bu += 300;
         bob_bc += 1500;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Bob order, show that entire deferred_fee was consumed by partial match
         if( !expire_order )
            cancel_limit_order( *bo32 );
         else
         {
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }

         if( !expire_order )
            bob_bc -= order_cancel_fee;
         // else do nothing:
         //         before hard fork 445, no fee when order is expired;
         //         after hard fork 445, when partially filled order expired, order cancel fee is capped at 0
         bob_bu += 200;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Alice order, will refund after hard fork 445
         cancel_limit_order( ao32id( db ) );

         alice_bc -= order_cancel_fee;
         alice_bc += 1000;
         alice_bc += usd_fee_refund_core;
         alice_bu += usd_fee_refund_usd;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // undo above tx's and reset
         generate_block( skip );
         ++blocks_generated;
         while( blocks_generated > 0 )
         {
            db.pop_block();
            --blocks_generated;
         }
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( hf445_fee_refund_cross_test )
{ // create orders before hard fork, cancel them after hard fork
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;
      int64_t pool_0 = 100000, accum_0 = 0;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee );
      asset_id_type usd_id = usd_obj.id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

      int64_t order_create_fee = 537;
      int64_t order_cancel_fee = 129;

      uint32_t skip = database::skip_witness_signature
                    | database::skip_transaction_signatures
                    | database::skip_transaction_dupe_check
                    | database::skip_block_size_check
                    | database::skip_tapos_check
                    | database::skip_authority_check
                    | database::skip_merkle_check
                    ;

      generate_block( skip );

      flat_set< fee_parameters > new_fees;
      {
         limit_order_create_operation::fee_parameters_type create_fee_params;
         create_fee_params.fee = order_create_fee;
         new_fees.insert( create_fee_params );
      }
      {
         limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
         cancel_fee_params.fee = order_cancel_fee;
         new_fees.insert( cancel_fee_params );
      }
      {
         transfer_operation::fee_parameters_type transfer_fee_params;
         transfer_fee_params.fee = 0;
         transfer_fee_params.price_per_kbyte = 0;
         new_fees.insert( transfer_fee_params );
      }

      // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
      // so we have to do it every time we stop generating/popping blocks and start doing tx's
      enable_fees();
      change_fees( new_fees );

      // AAAAGGHH create_sell_order reads trx.expiration #469
      set_expiration( db, trx );

      // prepare params
      const chain_parameters& params = db.get_global_properties().parameters;
      time_point_sec max_exp = time_point_sec::maximum();
      time_point_sec exp = HARDFORK_445_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 3 );
      time_point_sec exp2 = HARDFORK_445_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 13 );
      price cer( asset(1), asset(1, usd_id) );
      const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

      // balance data
      int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
      int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
      int64_t pool_b = pool_0, accum_b = accum_0;

      // prepare orders
      BOOST_TEST_MESSAGE( "Creating orders those will never match: ao1, ao2, bo1, bo2 .." );
      // ao1: won't expire, won't match, fee in core
      limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(100000, usd_id) )->id;
      BOOST_CHECK( db.find( ao1_id ) != nullptr );
      // ao2: will expire, won't match, fee in core
      limit_order_id_type ao2_id = create_sell_order( alice_id, asset(800), asset(100000, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao2_id ) != nullptr );
      // bo1: won't expire, won't match, fee in usd
      limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(1000, usd_id), asset(100000), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo1_id ) != nullptr );
      // bo2: will expire, won't match, fee in usd
      limit_order_id_type bo2_id = create_sell_order(   bob_id, asset(800, usd_id), asset(100000), exp, cer )->id;
      BOOST_CHECK( db.find( bo2_id ) != nullptr );

      alice_bc -= order_create_fee * 2;
      alice_bc -= 1000;
      alice_bc -= 800;
      bob_bu -= order_create_fee * 2;
      bob_bu -= 1000;
      bob_bu -= 800;
      pool_b -= order_create_fee * 2;
      accum_b += order_create_fee * 2;
      int64_t ao1_remain = 1000;
      int64_t ao2_remain = 800;
      int64_t bo1_remain = 1000;
      int64_t bo2_remain = 800;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao3: won't expire, partially match before hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao3 .." );
      limit_order_id_type ao3_id = create_sell_order( alice_id, asset(900), asset(2700, usd_id) )->id;
      BOOST_CHECK( db.find( ao3_id ) != nullptr );
      create_sell_order( bob_id, asset(600, usd_id), asset(200) );

      alice_bc -= order_create_fee;
      alice_bc -= 900;
      alice_bu += 600;
      bob_bc -= order_create_fee;
      bob_bu -= 600;
      bob_bc += 200;
      int64_t ao3_remain = 900 - 200;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao4: will expire, will partially match before hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao4 .." );
      limit_order_id_type ao4_id = create_sell_order( alice_id, asset(700), asset(1400, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao4_id ) != nullptr );
      create_sell_order( bob_id, asset(200, usd_id), asset(100) );

      alice_bc -= order_create_fee;
      alice_bc -= 700;
      alice_bu += 200;
      bob_bc -= order_create_fee;
      bob_bu -= 200;
      bob_bc += 100;
      int64_t ao4_remain = 700 - 100;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo3: won't expire, will partially match before hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo3 .." );
      limit_order_id_type bo3_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1500), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo3_id ) != nullptr );
      create_sell_order( alice_id, asset(450), asset(150, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 450;
      alice_bu += 150;
      bob_bu -= order_create_fee;
      bob_bu -= 500;
      bob_bc += 450;
      pool_b -= order_create_fee;
      accum_b += order_create_fee;
      int64_t bo3_remain = 500 - 150;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo4: will expire, will partially match before hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo4 .." );
      limit_order_id_type bo4_id = create_sell_order(   bob_id, asset(300, usd_id), asset(600), exp, cer )->id;
      BOOST_CHECK( db.find( bo4_id ) != nullptr );
      create_sell_order( alice_id, asset(140), asset(70, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 140;
      alice_bu += 70;
      bob_bu -= order_create_fee;
      bob_bu -= 300;
      bob_bc += 140;
      pool_b -= order_create_fee;
      accum_b += order_create_fee;
      int64_t bo4_remain = 300 - 70;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao5: won't expire, partially match after hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao5 .." );
      limit_order_id_type ao5_id = create_sell_order( alice_id, asset(606), asset(909, usd_id) )->id;
      BOOST_CHECK( db.find( ao5_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 606;
      int64_t ao5_remain = 606;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao6: will expire, partially match after hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao6 .." );
      limit_order_id_type ao6_id = create_sell_order( alice_id, asset(333), asset(444, usd_id), exp2 )->id;
      BOOST_CHECK( db.find( ao6_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 333;
      int64_t ao6_remain = 333;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo5: won't expire, partially match after hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo5 .." );
      limit_order_id_type bo5_id = create_sell_order(   bob_id, asset(255, usd_id), asset(408), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo5_id ) != nullptr );

      bob_bu -= order_create_fee;
      bob_bu -= 255;
      pool_b -= order_create_fee;
      accum_b += order_create_fee;
      int64_t bo5_remain = 255;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo6: will expire, partially match after hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo6 .." );
      limit_order_id_type bo6_id = create_sell_order(   bob_id, asset(127, usd_id), asset(127), exp2, cer )->id;
      BOOST_CHECK( db.find( bo6_id ) != nullptr );

      bob_bu -= order_create_fee;
      bob_bu -= 127;
      pool_b -= order_create_fee;
      accum_b += order_create_fee;
      int64_t bo6_remain = 127;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block so the orders will be in db before hard fork
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      // generate blocks util hard fork 445
      generate_blocks( HARDFORK_445_TIME, true, skip );
      generate_block( skip );

      // nothing will change
      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate more blocks, so some orders will expire
      generate_blocks( exp, true, skip );

      // no fee refund for orders created before hard fork 445, but remaining funds will be refunded
      BOOST_TEST_MESSAGE( "Checking expired orders: ao2, ao4, bo2, bo4 .." );
      alice_bc += ao2_remain;
      alice_bc += ao4_remain;
      bob_bu += bo2_remain;
      bob_bu += bo4_remain;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // cancel ao1
      BOOST_TEST_MESSAGE( "Cancel order ao1 .." );
      cancel_limit_order( ao1_id(db) );

      alice_bc += ao1_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel ao3
      BOOST_TEST_MESSAGE( "Cancel order ao3 .." );
      cancel_limit_order( ao3_id(db) );

      alice_bc += ao3_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo1
      BOOST_TEST_MESSAGE( "Cancel order bo1 .." );
      cancel_limit_order( bo1_id(db) );

      bob_bu += bo1_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo3
      BOOST_TEST_MESSAGE( "Cancel order bo3 .." );
      cancel_limit_order( bo3_id(db) );

      bob_bu += bo3_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill ao6
      BOOST_TEST_MESSAGE( "Partially fill ao6 .." );
      create_sell_order( bob_id, asset(88, usd_id), asset(66) );

      alice_bu += 88;
      bob_bc -= order_create_fee;
      bob_bu -= 88;
      bob_bc += 66;
      ao6_remain -= 66;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo6
      BOOST_TEST_MESSAGE( "Partially fill bo6 .." );
      create_sell_order( alice_id, asset(59), asset(59, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 59;
      alice_bu += 59;
      bob_bc += 59;
      bo6_remain -= 59;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate blocks util exp2, so some orders will expire
      generate_blocks( exp2, true, skip );

      // no fee refund for orders created before hard fork 445, but remaining funds will be refunded
      BOOST_TEST_MESSAGE( "Checking expired orders: ao6, bo6 .." );
      alice_bc += ao6_remain;
      bob_bu += bo6_remain;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // partially fill ao5
      BOOST_TEST_MESSAGE( "Partially fill ao5 .." );
      create_sell_order( bob_id, asset(93, usd_id), asset(62) );

      alice_bu += 93;
      bob_bc -= order_create_fee;
      bob_bu -= 93;
      bob_bc += 62;
      ao5_remain -= 62;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo5
      BOOST_TEST_MESSAGE( "Partially fill bo5 .." );
      create_sell_order( alice_id, asset(24), asset(15, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 24;
      alice_bu += 15;
      bob_bc += 24;
      bo5_remain -= 15;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel ao5
      BOOST_TEST_MESSAGE( "Cancel order ao5 .." );
      cancel_limit_order( ao5_id(db) );

      alice_bc += ao5_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo5
      BOOST_TEST_MESSAGE( "Cancel order bo5 .." );
      cancel_limit_order( bo5_id(db) );

      bob_bu += bo5_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( bsip26_fee_refund_test )
{
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;
      int64_t pool_0 = 1000000, accum_0 = 0;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      int64_t cer_core_amount = 1801;
      int64_t cer_usd_amount = 3;
      price tmp_cer( asset( cer_core_amount ), asset( cer_usd_amount, asset_id_type(1) ) );
      const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee, tmp_cer );
      asset_id_type usd_id = usd_obj.id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

      int64_t order_create_fee = 547;
      int64_t order_cancel_fee;
      int64_t order_cancel_fee1 = 139;
      int64_t order_cancel_fee2 = 829;

      uint32_t skip = database::skip_witness_signature
                    | database::skip_transaction_signatures
                    | database::skip_transaction_dupe_check
                    | database::skip_block_size_check
                    | database::skip_tapos_check
                    | database::skip_authority_check
                    | database::skip_merkle_check
                    ;

      generate_block( skip );

      flat_set< fee_parameters > new_fees;
      flat_set< fee_parameters > new_fees1;
      flat_set< fee_parameters > new_fees2;
      {
         limit_order_create_operation::fee_parameters_type create_fee_params;
         create_fee_params.fee = order_create_fee;
         new_fees1.insert( create_fee_params );
         new_fees2.insert( create_fee_params );
      }
      {
         limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
         cancel_fee_params.fee = order_cancel_fee1;
         new_fees1.insert( cancel_fee_params );
      }
      {
         limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
         cancel_fee_params.fee = order_cancel_fee2;
         new_fees2.insert( cancel_fee_params );
      }
      {
         transfer_operation::fee_parameters_type transfer_fee_params;
         transfer_fee_params.fee = 0;
         transfer_fee_params.price_per_kbyte = 0;
         new_fees1.insert( transfer_fee_params );
         new_fees2.insert( transfer_fee_params );
      }

      for( int i=0; i<12; i++ )
      {
         bool expire_order = ( i % 2 != 0 );
         bool high_cancel_fee = ( i % 4 >= 2 );
         bool before_hardfork_445 = ( i < 4 );
         bool after_bsip26 = ( i >= 8 );
         idump( (before_hardfork_445)(after_bsip26)(expire_order)(high_cancel_fee) );
         if( i == 4 )
         {
            BOOST_TEST_MESSAGE( "Hard fork 445" );
            generate_blocks( HARDFORK_445_TIME, true, skip );
            generate_block( skip );
         }
         else if( i == 8 )
         {
            BOOST_TEST_MESSAGE( "Hard fork core-604 (bsip26)" );
            generate_blocks( HARDFORK_CORE_604_TIME, true, skip );
            generate_block( skip );
         }

         if( high_cancel_fee )
         {
            new_fees = new_fees2;
            order_cancel_fee = order_cancel_fee2;
         }
         else
         {
            new_fees = new_fees1;
            order_cancel_fee = order_cancel_fee1;
         }

         int64_t usd_create_fee = order_create_fee * cer_usd_amount / cer_core_amount;
         if( usd_create_fee * cer_core_amount != order_create_fee * cer_usd_amount ) usd_create_fee += 1;
         int64_t usd_cancel_fee = order_cancel_fee * cer_usd_amount / cer_core_amount;
         if( usd_cancel_fee * cer_core_amount != order_cancel_fee * cer_usd_amount ) usd_cancel_fee += 1;
         int64_t core_create_fee = usd_create_fee * cer_core_amount / cer_usd_amount;
         int64_t core_cancel_fee = usd_cancel_fee * cer_core_amount / cer_usd_amount;
         BOOST_CHECK( core_cancel_fee >= order_cancel_fee );

         BOOST_TEST_MESSAGE( "Start" );

         // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
         // so we have to do it every time we stop generating/popping blocks and start doing tx's
         enable_fees();
         change_fees( new_fees );

         // AAAAGGHH create_sell_order reads trx.expiration #469
         set_expiration( db, trx );

         // prepare params
         uint32_t blocks_generated = 0;
         time_point_sec max_exp = time_point_sec::maximum();
         time_point_sec exp = db.head_block_time(); // order will be accepted when pushing trx then expired at current block
         price cer = usd_id( db ).options.core_exchange_rate;
         const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

         // balance data
         int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
         int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
         int64_t pool_b = pool_0, accum_b = accum_0;

         // refund data
         int64_t core_fee_refund_core;
         int64_t core_fee_refund_usd;
         int64_t usd_fee_refund_core;
         int64_t usd_fee_refund_usd;
         int64_t accum_on_new;
         int64_t accum_on_fill;
         int64_t pool_refund;
         if( db.head_block_time() > HARDFORK_CORE_604_TIME )
         {
            core_fee_refund_core = order_create_fee;
            core_fee_refund_usd = 0;
            usd_fee_refund_core = 0;
            usd_fee_refund_usd = usd_create_fee;
            accum_on_new = 0;
            accum_on_fill = usd_create_fee;
            pool_refund = core_create_fee;
         }
         else if( db.head_block_time() > HARDFORK_445_TIME )
         {
            core_fee_refund_core = order_create_fee;
            core_fee_refund_usd = 0;
            usd_fee_refund_core = core_create_fee;
            usd_fee_refund_usd = 0;
            accum_on_new = usd_create_fee;
            accum_on_fill = 0;
            pool_refund = 0;
         }
         else
         {
            core_fee_refund_core = 0;
            core_fee_refund_usd = 0;
            usd_fee_refund_core = 0;
            usd_fee_refund_usd = 0;
            accum_on_new = usd_create_fee;
            accum_on_fill = 0;
            pool_refund = 0;
         }

         // Check non-overlapping
         // Alice creates order
         // Bob creates order which doesn't match
         BOOST_TEST_MESSAGE( "Creating non-overlapping orders" );
         BOOST_TEST_MESSAGE( "Creating ao1" );
         limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(1000, usd_id), exp )->id;

         alice_bc -= order_create_fee;
         alice_bc -= 1000;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Alice cancels order
         if( !expire_order )
         {
            BOOST_TEST_MESSAGE( "Cancel order ao1" );
            cancel_limit_order( ao1_id(db) );
         }
         else
         {
            BOOST_TEST_MESSAGE( "Order ao1 expired" );
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }


         if( !expire_order )
            alice_bc -= order_cancel_fee; // manual cancellation always need a fee
         else if( before_hardfork_445 )
         {  // do nothing: before hard fork 445, no fee on expired order
         }
         else if( !after_bsip26 )
         {
            // charge a cancellation fee in core, capped by deffered_fee which is order_create_fee
            alice_bc -= std::min( order_cancel_fee, order_create_fee );
         }
         else // bsip26
         {
            // charge a cancellation fee in core, capped by deffered_fee which is order_create_fee
            alice_bc -= std::min( order_cancel_fee, order_create_fee );
         }
         alice_bc += 1000;
         alice_bc += core_fee_refund_core;
         alice_bu += core_fee_refund_usd;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         BOOST_TEST_MESSAGE( "Creating bo1" );
         limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1000), exp, cer )->id;

         bob_bu -= usd_create_fee;
         bob_bu -= 500;
         pool_b -= core_create_fee;
         accum_b += accum_on_new;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob cancels order
         if( !expire_order )
         {
            BOOST_TEST_MESSAGE( "Cancel order bo1" );
            cancel_limit_order( bo1_id(db) );
         }
         else
         {
            BOOST_TEST_MESSAGE( "Order bo1 expired" );
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }

         if( !expire_order )
            bob_bc -= order_cancel_fee; // manual cancellation always need a fee
         else if( before_hardfork_445 )
         {  // do nothing: before hard fork 445, no fee on expired order
         }
         else if( !after_bsip26 )
         {
            // charge a cancellation fee in core, capped by deffered_fee which is core_create_fee
            bob_bc -= std::min( order_cancel_fee, core_create_fee );
         }
         else // bsip26
         {
            // when expired, should have core_create_fee in deferred, usd_create_fee in deferred_paid

            // charge a cancellation fee in core from fee_pool, capped by deffered
            int64_t capped_core_cancel_fee = std::min( order_cancel_fee, core_create_fee );
            pool_b -= capped_core_cancel_fee;

            // charge a coresponding cancellation fee in usd from deffered_paid, round up, capped
            int64_t capped_usd_cancel_fee = capped_core_cancel_fee * usd_create_fee / core_create_fee;
            if( capped_usd_cancel_fee * core_create_fee != capped_core_cancel_fee * usd_create_fee )
               capped_usd_cancel_fee += 1;
            if( capped_usd_cancel_fee > usd_create_fee )
               capped_usd_cancel_fee = usd_create_fee;
            bob_bu -= capped_usd_cancel_fee;

            // cancellation fee goes to accumulated fees
            accum_b += capped_usd_cancel_fee;
         }
         bob_bc += usd_fee_refund_core;
         bob_bu += 500;
         bob_bu += usd_fee_refund_usd;
         pool_b += pool_refund; // bo1

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );


         // Check partial fill
         BOOST_TEST_MESSAGE( "Creating ao2, then be partially filled by bo2" );
         const limit_order_object* ao2 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), exp, cer );
         const limit_order_id_type ao2id = ao2->id;
         const limit_order_object* bo2 = create_sell_order(   bob_id, asset(100, usd_id), asset(500) );

         BOOST_CHECK( db.find<limit_order_object>( ao2id ) != nullptr );
         BOOST_CHECK( bo2 == nullptr );

         // data after order created
         alice_bc -= 1000;
         alice_bu -= usd_create_fee;
         pool_b -= core_create_fee;
         accum_b += accum_on_new;
         bob_bc -= order_create_fee;
         bob_bu -= 100;

         // data after order filled
         alice_bu += 100;
         bob_bc += 500;
         accum_b += accum_on_fill; // ao2

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Alice order, show that entire deferred_fee was consumed by partial match
         if( !expire_order )
         {
            BOOST_TEST_MESSAGE( "Cancel order ao2" );
            cancel_limit_order( *ao2 );
         }
         else
         {
            BOOST_TEST_MESSAGE( "Order ao2 expired" );
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }


         if( !expire_order )
            alice_bc -= order_cancel_fee;
         // else do nothing:
         //         before hard fork 445, no fee when order is expired;
         //         after hard fork 445, when partially filled order expired, order cancel fee is capped at 0
         alice_bc += 500;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Check multiple fill
         // Alice creating multiple orders
         BOOST_TEST_MESSAGE( "Creating ao31-ao35" );
         const limit_order_object* ao31 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao32 = create_sell_order( alice_id, asset(1000), asset(2000, usd_id), max_exp, cer );
         const limit_order_object* ao33 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao34 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );
         const limit_order_object* ao35 = create_sell_order( alice_id, asset(1000), asset(200, usd_id), max_exp, cer );

         const limit_order_id_type ao31id = ao31->id;
         const limit_order_id_type ao32id = ao32->id;
         const limit_order_id_type ao33id = ao33->id;
         const limit_order_id_type ao34id = ao34->id;
         const limit_order_id_type ao35id = ao35->id;

         alice_bc -= 1000 * 5;
         alice_bu -= usd_create_fee * 5;
         pool_b -= core_create_fee * 5;
         accum_b += accum_on_new * 5;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob creating an order matching multiple Alice's orders
         BOOST_TEST_MESSAGE( "Creating bo31, completely fill ao31 and ao33, partially fill ao34" );
         const limit_order_object* bo31 = create_sell_order(   bob_id, asset(500, usd_id), asset(2500), exp );

         BOOST_CHECK( db.find<limit_order_object>( ao31id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao32id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao33id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao34id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao35id ) != nullptr );
         BOOST_CHECK( bo31 == nullptr );

         // data after order created
         bob_bc -= order_create_fee;
         bob_bu -= 500;

         // data after order filled
         alice_bu += 500;
         bob_bc += 2500;
         accum_b += accum_on_fill * 3; // ao31, ao33, ao34

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // Bob creating an order matching multiple Alice's orders
         BOOST_TEST_MESSAGE( "Creating bo32, completely fill partially filled ao34 and new ao35, leave on market" );
         const limit_order_object* bo32 = create_sell_order(   bob_id, asset(500, usd_id), asset(2500), exp );

         BOOST_CHECK( db.find<limit_order_object>( ao31id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao32id ) != nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao33id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao34id ) == nullptr );
         BOOST_CHECK( db.find<limit_order_object>( ao35id ) == nullptr );
         BOOST_CHECK( bo32 != nullptr );

         // data after order created
         bob_bc -= order_create_fee;
         bob_bu -= 500;

         // data after order filled
         alice_bu += 300;
         bob_bc += 1500;
         accum_b += accum_on_fill; // ao35

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Bob order, show that entire deferred_fee was consumed by partial match
         if( !expire_order )
         {
            BOOST_TEST_MESSAGE( "Cancel order bo32" );
            cancel_limit_order( *bo32 );
         }
         else
         {
            BOOST_TEST_MESSAGE( "Order bo32 expired" );
            // empty accounts before generate block, to test if it will fail when charging order cancel fee
            transfer( alice_id, account_id_type(), asset(alice_bc, core_id) );
            transfer( alice_id, account_id_type(), asset(alice_bu,  usd_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bc, core_id) );
            transfer(   bob_id, account_id_type(), asset(  bob_bu,  usd_id) );
            // generate a new block so one or more order will expire
            generate_block( skip );
            ++blocks_generated;
            enable_fees();
            change_fees( new_fees );
            set_expiration( db, trx );
            exp = db.head_block_time();
            usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
            // restore account balances
            transfer( account_id_type(), alice_id, asset(alice_bc, core_id) );
            transfer( account_id_type(), alice_id, asset(alice_bu,  usd_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bc, core_id) );
            transfer( account_id_type(),   bob_id, asset(  bob_bu,  usd_id) );
         }

         if( !expire_order )
            bob_bc -= order_cancel_fee;
         // else do nothing:
         //         before hard fork 445, no fee when order is expired;
         //         after hard fork 445, when partially filled order expired, order cancel fee is capped at 0
         bob_bu += 200;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // cancel Alice order, will refund after hard fork 445
         BOOST_TEST_MESSAGE( "Cancel order ao32" );
         cancel_limit_order( ao32id( db ) );

         alice_bc -= order_cancel_fee;
         alice_bc += 1000;
         alice_bc += usd_fee_refund_core;
         alice_bu += usd_fee_refund_usd;
         pool_b += pool_refund;

         BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
         BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
         BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
         BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
         BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
         BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

         // undo above tx's and reset
         BOOST_TEST_MESSAGE( "Clean up" );
         generate_block( skip );
         ++blocks_generated;
         while( blocks_generated > 0 )
         {
            db.pop_block();
            --blocks_generated;
         }
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( bsip26_fee_refund_cross_test )
{ // create orders before hard fork, cancel them after hard fork
   try
   {
      ACTORS((alice)(bob)(izzy));

      int64_t alice_b0 = 1000000, bob_b0 = 1000000;
      int64_t pool_0 = 1000000, accum_0 = 0;

      transfer( account_id_type(), alice_id, asset(alice_b0) );
      transfer( account_id_type(), bob_id, asset(bob_b0) );

      asset_id_type core_id = asset_id_type();
      int64_t cer_core_amount = 1801;
      int64_t cer_usd_amount = 3;
      price tmp_cer( asset( cer_core_amount ), asset( cer_usd_amount, asset_id_type(1) ) );
      const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee, tmp_cer );
      asset_id_type usd_id = usd_obj.id;
      issue_uia( alice_id, asset( alice_b0, usd_id ) );
      issue_uia( bob_id, asset( bob_b0, usd_id ) );

      fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

      int64_t order_create_fee = 547;
      int64_t order_cancel_fee = 139;
      int64_t usd_create_fee = order_create_fee * cer_usd_amount / cer_core_amount;
      if( usd_create_fee * cer_core_amount != order_create_fee * cer_usd_amount ) usd_create_fee += 1;
      int64_t usd_cancel_fee = order_cancel_fee * cer_usd_amount / cer_core_amount;
      if( usd_cancel_fee * cer_core_amount != order_cancel_fee * cer_usd_amount ) usd_cancel_fee += 1;
      int64_t core_create_fee = usd_create_fee * cer_core_amount / cer_usd_amount;
      int64_t core_cancel_fee = usd_cancel_fee * cer_core_amount / cer_usd_amount;
      BOOST_CHECK( core_cancel_fee >= order_cancel_fee );

      uint32_t skip = database::skip_witness_signature
                    | database::skip_transaction_signatures
                    | database::skip_transaction_dupe_check
                    | database::skip_block_size_check
                    | database::skip_tapos_check
                    | database::skip_authority_check
                    | database::skip_merkle_check
                    ;

      generate_block( skip );

      flat_set< fee_parameters > new_fees;
      {
         limit_order_create_operation::fee_parameters_type create_fee_params;
         create_fee_params.fee = order_create_fee;
         new_fees.insert( create_fee_params );
      }
      {
         limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
         cancel_fee_params.fee = order_cancel_fee;
         new_fees.insert( cancel_fee_params );
      }
      {
         transfer_operation::fee_parameters_type transfer_fee_params;
         transfer_fee_params.fee = 0;
         transfer_fee_params.price_per_kbyte = 0;
         new_fees.insert( transfer_fee_params );
      }

      // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
      // so we have to do it every time we stop generating/popping blocks and start doing tx's
      enable_fees();
      change_fees( new_fees );

      // AAAAGGHH create_sell_order reads trx.expiration #469
      set_expiration( db, trx );

      // prepare params
      const chain_parameters& params = db.get_global_properties().parameters;
      time_point_sec max_exp = time_point_sec::maximum();
      time_point_sec exp = HARDFORK_CORE_604_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 3 );
      time_point_sec exp1 = HARDFORK_CORE_604_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 13 );
      time_point_sec exp2 = HARDFORK_CORE_604_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 23 );
      price cer = usd_id( db ).options.core_exchange_rate;
      const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

      // balance data
      int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
      int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
      int64_t pool_b = pool_0, accum_b = accum_0;

      // prepare orders
      BOOST_TEST_MESSAGE( "Creating orders those will never match: ao1, ao2, bo1, bo2 .." );
      // ao1: won't expire, won't match, fee in core
      limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(100000, usd_id) )->id;
      BOOST_CHECK( db.find( ao1_id ) != nullptr );
      // ao2: will expire, won't match, fee in core
      limit_order_id_type ao2_id = create_sell_order( alice_id, asset(800), asset(100000, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao2_id ) != nullptr );
      // bo1: won't expire, won't match, fee in usd
      limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(1000, usd_id), asset(100000), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo1_id ) != nullptr );
      // bo2: will expire, won't match, fee in usd
      limit_order_id_type bo2_id = create_sell_order(   bob_id, asset(800, usd_id), asset(100000), exp, cer )->id;
      BOOST_CHECK( db.find( bo2_id ) != nullptr );

      alice_bc -= order_create_fee * 2;
      alice_bc -= 1000;
      alice_bc -= 800;
      bob_bu -= usd_create_fee * 2;
      bob_bu -= 1000;
      bob_bu -= 800;
      pool_b -= core_create_fee * 2;
      accum_b += usd_create_fee * 2;
      int64_t ao1_remain = 1000;
      int64_t ao2_remain = 800;
      int64_t bo1_remain = 1000;
      int64_t bo2_remain = 800;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao3: won't expire, partially match before hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao3 .." ); // 1:30
      limit_order_id_type ao3_id = create_sell_order( alice_id, asset(900), asset(27000, usd_id) )->id;
      BOOST_CHECK( db.find( ao3_id ) != nullptr );
      create_sell_order( bob_id, asset(6000, usd_id), asset(200) );

      alice_bc -= order_create_fee;
      alice_bc -= 900;
      alice_bu += 6000;
      bob_bc -= order_create_fee;
      bob_bu -= 6000;
      bob_bc += 200;
      int64_t ao3_remain = 900 - 200;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao4: will expire, will partially match before hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao4 .." ); // 1:20
      limit_order_id_type ao4_id = create_sell_order( alice_id, asset(700), asset(14000, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao4_id ) != nullptr );
      create_sell_order( bob_id, asset(2000, usd_id), asset(100) );

      alice_bc -= order_create_fee;
      alice_bc -= 700;
      alice_bu += 2000;
      bob_bc -= order_create_fee;
      bob_bu -= 2000;
      bob_bc += 100;
      int64_t ao4_remain = 700 - 100;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo3: won't expire, will partially match before hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo3 .." ); // 1:30
      limit_order_id_type bo3_id = create_sell_order(   bob_id, asset(500, usd_id), asset(15000), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo3_id ) != nullptr );
      create_sell_order( alice_id, asset(4500), asset(150, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 4500;
      alice_bu += 150;
      bob_bu -= usd_create_fee;
      bob_bu -= 500;
      bob_bc += 4500;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo3_remain = 500 - 150;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo4: will expire, will partially match before hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo4 .." ); // 1:20
      limit_order_id_type bo4_id = create_sell_order(   bob_id, asset(300, usd_id), asset(6000), exp, cer )->id;
      BOOST_CHECK( db.find( bo4_id ) != nullptr );
      create_sell_order( alice_id, asset(1400), asset(70, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 1400;
      alice_bu += 70;
      bob_bu -= usd_create_fee;
      bob_bu -= 300;
      bob_bc += 1400;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo4_remain = 300 - 70;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );


      // ao11: won't expire, partially match after hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao11 .." ); // 1:18
      limit_order_id_type ao11_id = create_sell_order( alice_id, asset(510), asset(9180, usd_id) )->id;
      BOOST_CHECK( db.find( ao11_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 510;
      int64_t ao11_remain = 510;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao12: will expire, partially match after hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao12 .." ); // 1:16
      limit_order_id_type ao12_id = create_sell_order( alice_id, asset(256), asset(4096, usd_id), exp2 )->id;
      BOOST_CHECK( db.find( ao12_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 256;
      int64_t ao12_remain = 256;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo11: won't expire, partially match after hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo11 .." ); // 1:18
      limit_order_id_type bo11_id = create_sell_order(   bob_id, asset(388, usd_id), asset(6984), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo11_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 388;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo11_remain = 388;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo12: will expire, partially match after hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo12 .." ); // 1:17
      limit_order_id_type bo12_id = create_sell_order(   bob_id, asset(213, usd_id), asset(3621), exp2, cer )->id;
      BOOST_CHECK( db.find( bo12_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 213;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo12_remain = 213;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao5: won't expire, partially match after hard fork 445, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao5 .." ); // 1:15
      limit_order_id_type ao5_id = create_sell_order( alice_id, asset(606), asset(9090, usd_id) )->id;
      BOOST_CHECK( db.find( ao5_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 606;
      int64_t ao5_remain = 606;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao6: will expire, partially match after hard fork 445, fee in core
      if( false ) { // only can have either ao5 or ao6, can't have both
      BOOST_TEST_MESSAGE( "Creating order ao6 .." ); // 3:40 = 1:13.33333
      limit_order_id_type ao6_id = create_sell_order( alice_id, asset(333), asset(4440, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao6_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 333;
      // int64_t ao6_remain = 333; // only can have either ao5 or ao6, can't have both

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );
      }

      // bo5: won't expire, partially match after hard fork 445, fee in usd
      if( false ) { // only can have either bo5 or bo6, can't have both
      BOOST_TEST_MESSAGE( "Creating order bo5 .." ); // 1:16
      limit_order_id_type bo5_id = create_sell_order(   bob_id, asset(255, usd_id), asset(4080), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo5_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 255;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      //int64_t bo5_remain = 255; // only can have either bo5 or bo6, can't have both

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );
      }

      // bo6: will expire, partially match after hard fork 445, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo6 .." ); // 1:10
      limit_order_id_type bo6_id = create_sell_order(   bob_id, asset(127, usd_id), asset(1270), exp, cer )->id;
      BOOST_CHECK( db.find( bo6_id ) != nullptr );
      BOOST_CHECK( db.find( bo6_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 127;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo6_remain = 127;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block so the orders will be in db before hard fork 445
      BOOST_TEST_MESSAGE( "Generating blocks, passing hard fork 445 ..." );
      generate_block( skip );

      // generate blocks util hard fork 445
      generate_blocks( HARDFORK_445_TIME, true, skip );
      generate_block( skip );

      // nothing will change
      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // partially fill ao6
      if( false ) { // only can have either ao5 or ao6, can't have both
      BOOST_TEST_MESSAGE( "Partially fill ao6 .." ); // 3:40
      create_sell_order( bob_id, asset(880, usd_id), asset(66) );

      alice_bu += 880;
      bob_bc -= order_create_fee;
      bob_bu -= 880;
      bob_bc += 66;
      //ao6_remain -= 66;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );
      }

      // partially fill bo6
      BOOST_TEST_MESSAGE( "Partially fill bo6 .." ); // 1:10
      create_sell_order( alice_id, asset(590), asset(59, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 590;
      alice_bu += 59;
      bob_bc += 590;
      bo6_remain -= 59;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill ao5
      BOOST_TEST_MESSAGE( "Partially fill ao5 .." ); // 1:15
      create_sell_order( bob_id, asset(930, usd_id), asset(62) );

      alice_bu += 930;
      bob_bc -= order_create_fee;
      bob_bu -= 930;
      bob_bc += 62;
      ao5_remain -= 62;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo5
      if( false ) { // only can have either bo5 or bo6, can't have both
      BOOST_TEST_MESSAGE( "Partially fill bo5 .." ); // 1:16
      create_sell_order( alice_id, asset(240), asset(15, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 240;
      alice_bu += 15;
      bob_bc += 240;
      //bo5_remain -= 15;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );
      }

      // prepare more orders
      BOOST_TEST_MESSAGE( "Creating more orders those will never match: ao7, ao8, bo7, bo8 .." ); // ~ 1:100
      // ao7: won't expire, won't match, fee in core
      limit_order_id_type ao7_id = create_sell_order( alice_id, asset(1003), asset(100000, usd_id) )->id;
      BOOST_CHECK( db.find( ao7_id ) != nullptr );
      // ao8: will expire, won't match, fee in core
      limit_order_id_type ao8_id = create_sell_order( alice_id, asset(803), asset(100000, usd_id), exp1 )->id;
      BOOST_CHECK( db.find( ao8_id ) != nullptr );
      // bo7: won't expire, won't match, fee in usd
      limit_order_id_type bo7_id = create_sell_order(   bob_id, asset(1003, usd_id), asset(100000), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo7_id ) != nullptr );
      // bo8: will expire, won't match, fee in usd
      limit_order_id_type bo8_id = create_sell_order(   bob_id, asset(803, usd_id), asset(100000), exp1, cer )->id;
      BOOST_CHECK( db.find( bo8_id ) != nullptr );

      alice_bc -= order_create_fee * 2;
      alice_bc -= 1003;
      alice_bc -= 803;
      bob_bu -= usd_create_fee * 2;
      bob_bu -= 1003;
      bob_bu -= 803;
      pool_b -= core_create_fee * 2;
      accum_b += usd_create_fee * 2;
      int64_t ao7_remain = 1003;
      int64_t ao8_remain = 803;
      int64_t bo7_remain = 1003;
      int64_t bo8_remain = 803;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao9: won't expire, partially match before hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao9 .." ); // 1:3
      limit_order_id_type ao9_id = create_sell_order( alice_id, asset(909), asset(2727, usd_id) )->id;
      BOOST_CHECK( db.find( ao9_id ) != nullptr );
      create_sell_order( bob_id, asset(606, usd_id), asset(202) );

      alice_bc -= order_create_fee;
      alice_bc -= 909;
      alice_bu += 606;
      bob_bc -= order_create_fee;
      bob_bu -= 606;
      bob_bc += 202;
      int64_t ao9_remain = 909 - 202;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao10: will expire, will partially match before hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao10 .." ); // 1:2
      limit_order_id_type ao10_id = create_sell_order( alice_id, asset(707), asset(1414, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao10_id ) != nullptr );
      create_sell_order( bob_id, asset(202, usd_id), asset(101) );

      alice_bc -= order_create_fee;
      alice_bc -= 707;
      alice_bu += 202;
      bob_bc -= order_create_fee;
      bob_bu -= 202;
      bob_bc += 101;
      int64_t ao10_remain = 707 - 101;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo9: won't expire, will partially match before hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo9 .." ); // 1:3
      limit_order_id_type bo9_id = create_sell_order(   bob_id, asset(505, usd_id), asset(1515), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo9_id ) != nullptr );
      create_sell_order( alice_id, asset(453), asset(151, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 453;
      alice_bu += 151;
      bob_bu -= usd_create_fee;
      bob_bu -= 505;
      bob_bc += 453;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo9_remain = 505 - 151;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo10: will expire, will partially match before hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo10 .." ); // 1:2
      limit_order_id_type bo10_id = create_sell_order(   bob_id, asset(302, usd_id), asset(604), exp, cer )->id;
      BOOST_CHECK( db.find( bo10_id ) != nullptr );
      create_sell_order( alice_id, asset(142), asset(71, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 142;
      alice_bu += 71;
      bob_bu -= usd_create_fee;
      bob_bu -= 302;
      bob_bc += 142;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo10_remain = 302 - 71;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao13: won't expire, partially match after hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao13 .." ); // 1:1.5
      limit_order_id_type ao13_id = create_sell_order( alice_id, asset(424), asset(636, usd_id) )->id;
      BOOST_CHECK( db.find( ao13_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 424;
      int64_t ao13_remain = 424;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // ao14: will expire, partially match after hard fork core-604, fee in core
      BOOST_TEST_MESSAGE( "Creating order ao14 .." ); // 1:1.2
      limit_order_id_type ao14_id = create_sell_order( alice_id, asset(525), asset(630, usd_id), exp )->id;
      BOOST_CHECK( db.find( ao14_id ) != nullptr );

      alice_bc -= order_create_fee;
      alice_bc -= 525;
      int64_t ao14_remain = 525;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo13: won't expire, partially match after hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo13 .." ); // 1:1.5
      limit_order_id_type bo13_id = create_sell_order(   bob_id, asset(364, usd_id), asset(546), max_exp, cer )->id;
      BOOST_CHECK( db.find( bo13_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 364;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo13_remain = 364;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // bo14: will expire, partially match after hard fork core-604, fee in usd
      BOOST_TEST_MESSAGE( "Creating order bo14 .." ); // 1:1.2
      limit_order_id_type bo14_id = create_sell_order(   bob_id, asset(365, usd_id), asset(438), exp, cer )->id;
      BOOST_CHECK( db.find( bo14_id ) != nullptr );

      bob_bu -= usd_create_fee;
      bob_bu -= 365;
      pool_b -= core_create_fee;
      accum_b += usd_create_fee;
      int64_t bo14_remain = 365;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block so the orders will be in db before hard fork core-604
      BOOST_TEST_MESSAGE( "Generating blocks, passing hard fork core-604 ..." );
      generate_block( skip );

      // generate blocks util hard fork core-604
      generate_blocks( HARDFORK_CORE_604_TIME, true, skip );
      generate_block( skip );

      // nothing will change
      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // partially fill ao14
      BOOST_TEST_MESSAGE( "Partially fill ao14 .." ); // 1:1.2
      create_sell_order( bob_id, asset(72, usd_id), asset(60) );

      alice_bu += 72;
      bob_bc -= order_create_fee;
      bob_bu -= 72;
      bob_bc += 60;
      ao14_remain -= 60;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo14
      BOOST_TEST_MESSAGE( "Partially fill bo14 .." ); // 1:1.2
      create_sell_order( alice_id, asset(66), asset(55, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 66;
      alice_bu += 55;
      bob_bc += 66;
      bo14_remain -= 55;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate more blocks, so some orders will expire
      generate_blocks( exp, true, skip );

      // don't refund fee, only refund remaining funds, for:
      // * orders created before hard fork 445 : ao2, ao4, ao6, bo2, bo4, bo6
      // * partially filled orders (cancellation fee capped at 0) : ao10, ao14, bo10, bo14
      BOOST_TEST_MESSAGE( "Checking expired orders: ao2, ao4, ao6, ao10, ao14, bo2, bo4, bo6, bo10, bo14 .." );
      alice_bc += ao2_remain;
      alice_bc += ao4_remain;
      //alice_bc += ao6_remain; // can only have ao5 or ao6 but not both
      alice_bc += ao10_remain;
      alice_bc += ao14_remain;
      bob_bu += bo2_remain;
      bob_bu += bo4_remain;
      bob_bu += bo6_remain; // can only have bo5 or bo6 but not both
      bob_bu += bo10_remain;
      bob_bu += bo14_remain;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // partially fill ao13
      BOOST_TEST_MESSAGE( "Partially fill ao13 .." ); // 1:1.5
      create_sell_order( bob_id, asset(78, usd_id), asset(52) );

      alice_bu += 78;
      bob_bc -= order_create_fee;
      bob_bu -= 78;
      bob_bc += 52;
      ao13_remain -= 52;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo13
      BOOST_TEST_MESSAGE( "Partially fill bo13 .." ); // 1:1.5
      create_sell_order( alice_id, asset(63), asset(42, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 63;
      alice_bu += 42;
      bob_bc += 63;
      bo13_remain -= 42;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // don't refund fee, only refund remaining funds, for manually cancellations with an explicit fee:
      // * orders created before hard fork 445 : ao1, ao3, ao5, bo1, bo3, bo5
      // * partially filled orders (cancellation fee capped at 0) : ao9, ao13, bo9, bo13

      // cancel ao1
      BOOST_TEST_MESSAGE( "Cancel order ao1 .." );
      cancel_limit_order( ao1_id(db) );

      alice_bc += ao1_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo1
      BOOST_TEST_MESSAGE( "Cancel order bo1 .." );
      cancel_limit_order( bo1_id(db) );

      bob_bu += bo1_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel ao3
      BOOST_TEST_MESSAGE( "Cancel order ao3 .." );
      cancel_limit_order( ao3_id(db) );

      alice_bc += ao3_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo3
      BOOST_TEST_MESSAGE( "Cancel order bo3 .." );
      cancel_limit_order( bo3_id(db) );

      bob_bu += bo3_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel ao5
      BOOST_TEST_MESSAGE( "Cancel order ao5 .." );
      cancel_limit_order( ao5_id(db) );

      alice_bc += ao5_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo5
      if( false ) { // can only have bo5 or bo6 but not both
      BOOST_TEST_MESSAGE( "Cancel order bo5 .." );
      //cancel_limit_order( bo5_id(db) );

      //bob_bu += bo5_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );
      }

      // cancel ao9
      BOOST_TEST_MESSAGE( "Cancel order ao9 .." );
      cancel_limit_order( ao9_id(db) );

      alice_bc += ao9_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo9
      BOOST_TEST_MESSAGE( "Cancel order bo9 .." );
      cancel_limit_order( bo9_id(db) );

      bob_bu += bo9_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel ao13
      BOOST_TEST_MESSAGE( "Cancel order ao13 .." );
      cancel_limit_order( ao13_id(db) );

      alice_bc += ao13_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo13
      BOOST_TEST_MESSAGE( "Cancel order bo13 .." );
      cancel_limit_order( bo13_id(db) );

      bob_bu += bo13_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );


      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate blocks util exp1, so some orders will expire
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );
      generate_blocks( exp1, true, skip );

      // orders created after hard fork 445 but before core-604, no partially filled,
      // will refund remaining funds, and will refund create fee in core (minus cancel fee, capped)
      BOOST_TEST_MESSAGE( "Checking expired orders: ao8, bo8 .." );
      alice_bc += ao8_remain;
      alice_bc += std::max(order_create_fee - order_cancel_fee, int64_t(0));
      bob_bu += bo8_remain;
      bob_bc += std::max(core_create_fee - order_cancel_fee, int64_t(0));

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // orders created after hard fork 445 but before core-604, no partially filled,
      // when manually cancelling (with an explicit fee),
      // will refund remaining funds, and will refund create fee in core

      // cancel ao7
      BOOST_TEST_MESSAGE( "Cancel order ao7 .." );
      cancel_limit_order( ao7_id(db) );

      alice_bc += ao7_remain;
      alice_bc -= order_cancel_fee;
      alice_bc += order_create_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo7
      BOOST_TEST_MESSAGE( "Cancel order bo7 .." );
      cancel_limit_order( bo7_id(db) );

      bob_bu += bo7_remain;
      bob_bc -= order_cancel_fee;
      bob_bc += core_create_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill ao12
      BOOST_TEST_MESSAGE( "Partially fill ao12 .." ); // 1:16
      create_sell_order( bob_id, asset(688, usd_id), asset(43) );

      alice_bu += 688;
      bob_bc -= order_create_fee;
      bob_bu -= 688;
      bob_bc += 43;
      ao12_remain -= 43;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo12
      BOOST_TEST_MESSAGE( "Partially fill bo12 .." ); // 1:17
      create_sell_order( alice_id, asset(629), asset(37, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 629;
      alice_bu += 37;
      bob_bc += 629;
      bo12_remain -= 37;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate blocks util exp2, so some orders will expire
      generate_blocks( exp2, true, skip );

      // no fee refund for orders created before hard fork 445, cancellation fee capped at 0
      // remaining funds will be refunded
      BOOST_TEST_MESSAGE( "Checking expired orders: ao12, bo12 .." );
      alice_bc += ao12_remain;
      bob_bu += bo12_remain;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // prepare for new transactions
      enable_fees();
      change_fees( new_fees );
      usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
      set_expiration( db, trx );

      // partially fill ao11
      BOOST_TEST_MESSAGE( "Partially fill ao11 .." ); // 1:18
      create_sell_order( bob_id, asset(1422, usd_id), asset(79) );

      alice_bu += 1422;
      bob_bc -= order_create_fee;
      bob_bu -= 1422;
      bob_bc += 79;
      ao11_remain -= 79;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // partially fill bo11
      BOOST_TEST_MESSAGE( "Partially fill bo11 .." ); // 1:18
      create_sell_order( alice_id, asset(1494), asset(83, usd_id) );

      alice_bc -= order_create_fee;
      alice_bc -= 1494;
      alice_bu += 83;
      bob_bc += 1494;
      bo11_remain -= 83;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // no fee refund for orders created before hard fork 445, if manually cancelled with an explicit fee.
      // remaining funds will be refunded

      // cancel ao11
      BOOST_TEST_MESSAGE( "Cancel order ao11 .." );
      cancel_limit_order( ao11_id(db) );

      alice_bc += ao11_remain;
      alice_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // cancel bo11
      BOOST_TEST_MESSAGE( "Cancel order bo11 .." );
      cancel_limit_order( bo11_id(db) );

      bob_bu += bo11_remain;
      bob_bc -= order_cancel_fee;

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

      // generate block to save the changes
      BOOST_TEST_MESSAGE( "Generating blocks ..." );
      generate_block( skip );

      BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
      BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
      BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
      BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
      BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
      BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_CASE( stealth_fba_test )
{
   try
   {
      ACTORS( (alice)(bob)(chloe)(dan)(izzy)(philbin)(tom) );
      upgrade_to_lifetime_member(philbin_id);

      generate_blocks( HARDFORK_538_TIME );
      generate_blocks( HARDFORK_555_TIME );
      generate_blocks( HARDFORK_563_TIME );
      generate_blocks( HARDFORK_572_TIME );
      generate_blocks( HARDFORK_599_TIME );

      // Philbin (registrar who registers Rex)

      // Izzy (initial issuer of stealth asset, will later transfer to Tom)
      // Alice, Bob, Chloe, Dan (ABCD)
      // Rex (recycler -- buyback account for stealth asset)
      // Tom (owner of stealth asset who will be set as top_n authority)

      // Izzy creates STEALTH
      asset_id_type stealth_id = create_user_issued_asset( "STEALTH", izzy_id(db),
         disable_confidential | transfer_restricted | override_authority | white_list | charge_market_fee ).id;

      /*
      // this is disabled because it doesn't work, our modify() is probably being overwritten by undo

      //
      // Init blockchain with stealth ID's
      // On a real chain, this would be done with #define GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET
      // causing the designated_asset fields of these objects to be set at genesis, but for
      // this test we modify the db directly.
      //
      auto set_fba_asset = [&]( uint64_t fba_acc_id, asset_id_type asset_id )
      {
         db.modify( fba_accumulator_id_type(fba_acc_id)(db), [&]( fba_accumulator_object& fba )
         {
            fba.designated_asset = asset_id;
         } );
      };

      set_fba_asset( fba_accumulator_id_transfer_to_blind  , stealth_id );
      set_fba_asset( fba_accumulator_id_blind_transfer     , stealth_id );
      set_fba_asset( fba_accumulator_id_transfer_from_blind, stealth_id );
      */

      // Izzy kills some permission bits (this somehow happened to the real STEALTH in production)
      {
         asset_update_operation update_op;
         update_op.issuer = izzy_id;
         update_op.asset_to_update = stealth_id;
         asset_options new_options;
         new_options = stealth_id(db).options;
         new_options.issuer_permissions = charge_market_fee;
         new_options.flags = disable_confidential | transfer_restricted | override_authority | white_list | charge_market_fee;
         // after fixing #579 you should be able to delete the following line
         new_options.core_exchange_rate = price( asset( 1, stealth_id ), asset( 1, asset_id_type() ) );
         update_op.new_options = new_options;
         signed_transaction tx;
         tx.operations.push_back( update_op );
         set_expiration( db, tx );
         sign( tx, izzy_private_key );
         PUSH_TX( db, tx );
      }

      // Izzy transfers issuer duty to Tom
      {
         asset_update_operation update_op;
         update_op.issuer = izzy_id;
         update_op.asset_to_update = stealth_id;
         update_op.new_issuer = tom_id;
         // new_options should be optional, but isn't...the following line should be unnecessary #580
         update_op.new_options = stealth_id(db).options;
         signed_transaction tx;
         tx.operations.push_back( update_op );
         set_expiration( db, tx );
         sign( tx, izzy_private_key );
         PUSH_TX( db, tx );
      }

      // Tom re-enables the permission bits to clear the flags, then clears them again
      // Allowed by #572 when current_supply == 0
      {
         asset_update_operation update_op;
         update_op.issuer = tom_id;
         update_op.asset_to_update = stealth_id;
         asset_options new_options;
         new_options = stealth_id(db).options;
         new_options.issuer_permissions = new_options.flags | charge_market_fee;
         update_op.new_options = new_options;
         signed_transaction tx;
         // enable perms is one op
         tx.operations.push_back( update_op );

         new_options.issuer_permissions = charge_market_fee;
         new_options.flags = charge_market_fee;
         update_op.new_options = new_options;
         // reset wrongly set flags and reset permissions can be done in a single op
         tx.operations.push_back( update_op );

         set_expiration( db, tx );
         sign( tx, tom_private_key );
         PUSH_TX( db, tx );
      }

      // Philbin registers Rex who will be the asset's buyback, including sig from the new issuer (Tom)
      account_id_type rex_id;
      {
         buyback_account_options bbo;
         bbo.asset_to_buy = stealth_id;
         bbo.asset_to_buy_issuer = tom_id;
         bbo.markets.emplace( asset_id_type() );
         account_create_operation create_op = make_account( "rex" );
         create_op.registrar = philbin_id;
         create_op.extensions.value.buyback_options = bbo;
         create_op.owner = authority::null_authority();
         create_op.active = authority::null_authority();

         signed_transaction tx;
         tx.operations.push_back( create_op );
         set_expiration( db, tx );
         sign( tx, philbin_private_key );
         sign( tx, tom_private_key );

         processed_transaction ptx = PUSH_TX( db, tx );
         rex_id = ptx.operation_results.back().get< object_id_type >();
      }

      // Tom issues some asset to Alice and Bob
      set_expiration( db, trx );  // #11
      issue_uia( alice_id, asset( 1000, stealth_id ) );
      issue_uia(   bob_id, asset( 1000, stealth_id ) );

      // Tom sets his authority to the top_n of the asset
      {
         top_holders_special_authority top2;
         top2.num_top_holders = 2;
         top2.asset = stealth_id;

         account_update_operation op;
         op.account = tom_id;
         op.extensions.value.active_special_authority = top2;
         op.extensions.value.owner_special_authority = top2;

         signed_transaction tx;
         tx.operations.push_back( op );

         set_expiration( db, tx );
         sign( tx, tom_private_key );

         PUSH_TX( db, tx );
      }

      // Wait until the next maintenance interval for top_n to take effect
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

      // Do a blind op to add some fees to the pool.
      fund( chloe_id(db), asset( 100000, asset_id_type() ) );

      auto create_transfer_to_blind = [&]( account_id_type account, asset amount, const std::string& key ) -> transfer_to_blind_operation
      {
         fc::ecc::private_key blind_key = fc::ecc::private_key::regenerate( fc::sha256::hash( key+"-privkey" ) );
         public_key_type blind_pub = blind_key.get_public_key();

         fc::sha256 secret = fc::sha256::hash( key+"-secret" );
         fc::sha256 nonce = fc::sha256::hash( key+"-nonce" );

         transfer_to_blind_operation op;
         blind_output blind_out;
         blind_out.owner = authority( 1, blind_pub, 1 );
         blind_out.commitment = fc::ecc::blind( secret, amount.amount.value );
         blind_out.range_proof = fc::ecc::range_proof_sign( 0, blind_out.commitment, secret, nonce, 0, 0, amount.amount.value );

         op.amount = amount;
         op.from = account;
         op.blinding_factor = fc::ecc::blind_sum( {secret}, 1 );
         op.outputs = {blind_out};

         return op;
      };

      {
         transfer_to_blind_operation op = create_transfer_to_blind( chloe_id, asset( 5000, asset_id_type() ), "chloe-key" );
         op.fee = asset( 1000, asset_id_type() );

         signed_transaction tx;
         tx.operations.push_back( op );
         set_expiration( db, tx );
         sign( tx, chloe_private_key );

         PUSH_TX( db, tx );
      }

      // wait until next maint interval
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);

      idump( ( get_operation_history( chloe_id ) ) );
      idump( ( get_operation_history( rex_id ) ) );
      idump( ( get_operation_history( tom_id ) ) );
   }
   catch( const fc::exception& e )
   {
      elog( "caught exception ${e}", ("e", e.to_detail_string()) );
      throw;
   }
}

BOOST_AUTO_TEST_CASE( defaults_test )
{ try {
    fee_schedule schedule;
    const limit_order_create_operation::fee_parameters_type default_order_fee {};

    // no fees set yet -> default
    asset fee = schedule.calculate_fee( limit_order_create_operation() );
    BOOST_CHECK_EQUAL( default_order_fee.fee, fee.amount.value );

    limit_order_create_operation::fee_parameters_type new_order_fee; new_order_fee.fee = 123;
    // set fee + check
    schedule.parameters.insert( new_order_fee );
    fee = schedule.calculate_fee( limit_order_create_operation() );
    BOOST_CHECK_EQUAL( new_order_fee.fee, fee.amount.value );

    // bid_collateral fee defaults to call_order_update fee
    // call_order_update fee is unset -> default
    const call_order_update_operation::fee_parameters_type default_short_fee {};
    call_order_update_operation::fee_parameters_type new_short_fee; new_short_fee.fee = 123;
    fee = schedule.calculate_fee( bid_collateral_operation() );
    BOOST_CHECK_EQUAL( default_short_fee.fee, fee.amount.value );

    // set call_order_update fee + check bid_collateral fee
    schedule.parameters.insert( new_short_fee );
    fee = schedule.calculate_fee( bid_collateral_operation() );
    BOOST_CHECK_EQUAL( new_short_fee.fee, fee.amount.value );

    // set bid_collateral fee + check
    bid_collateral_operation::fee_parameters_type new_bid_fee; new_bid_fee.fee = 124;
    schedule.parameters.insert( new_bid_fee );
    fee = schedule.calculate_fee( bid_collateral_operation() );
    BOOST_CHECK_EQUAL( new_bid_fee.fee, fee.amount.value );
  }
  catch( const fc::exception& e )
  {
     elog( "caught exception ${e}", ("e", e.to_detail_string()) );
     throw;
  }
}

BOOST_AUTO_TEST_CASE( issue_429_test )
{
   try
   {
      ACTORS((alice));

      transfer( committee_account, alice_id, asset( 1000000 * asset::scaled_precision( asset_id_type()(db).precision ) ) );

      // make sure the database requires our fee to be nonzero
      enable_fees();

      auto fees_to_pay = db.get_global_properties().parameters.current_fees->get<asset_create_operation>();

      {
         signed_transaction tx;
         asset_create_operation op;
         op.issuer = alice_id;
         op.symbol = "ALICE";
         op.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
         op.fee = asset( (fees_to_pay.long_symbol + fees_to_pay.price_per_kbyte) & (~1) );
         tx.operations.push_back( op );
         set_expiration( db, tx );
         sign( tx, alice_private_key );
         PUSH_TX( db, tx );
      }

      verify_asset_supplies( db );

      {
         signed_transaction tx;
         asset_create_operation op;
         op.issuer = alice_id;
         op.symbol = "ALICE.ODD";
         op.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
         op.fee = asset((fees_to_pay.long_symbol + fees_to_pay.price_per_kbyte) | 1);
         tx.operations.push_back( op );
         set_expiration( db, tx );
         sign( tx, alice_private_key );
         PUSH_TX( db, tx );
      }

      verify_asset_supplies( db );

      generate_blocks( HARDFORK_CORE_429_TIME + 10 );

      {
         signed_transaction tx;
         asset_create_operation op;
         op.issuer = alice_id;
         op.symbol = "ALICE.ODDER";
         op.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
         op.fee = asset((fees_to_pay.long_symbol + fees_to_pay.price_per_kbyte) | 1);
         tx.operations.push_back( op );
         set_expiration( db, tx );
         sign( tx, alice_private_key );
         PUSH_TX( db, tx );
      }

      verify_asset_supplies( db );
   }
   catch( const fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( issue_433_test )
{
   try
   {
      ACTORS((alice));

      auto& core = asset_id_type()(db);

      transfer( committee_account, alice_id, asset( 1000000 * asset::scaled_precision( core.precision ) ) );

      const auto& myusd = create_user_issued_asset( "MYUSD", alice, 0 );
      issue_uia( alice, myusd.amount( 2000000000 ) );

      // make sure the database requires our fee to be nonzero
      enable_fees();

      const auto& fees = *db.get_global_properties().parameters.current_fees;
      const auto asset_create_fees = fees.get<asset_create_operation>();

      fund_fee_pool( alice, myusd, 5*asset_create_fees.long_symbol );

      asset_create_operation op;
      op.issuer = alice_id;
      op.symbol = "ALICE";
      op.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
      op.fee = myusd.amount( ((asset_create_fees.long_symbol + asset_create_fees.price_per_kbyte) & (~1)) );
      signed_transaction tx;
      tx.operations.push_back( op );
      set_expiration( db, tx );
      sign( tx, alice_private_key );
      PUSH_TX( db, tx );

      verify_asset_supplies( db );
   }
   catch( const fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( issue_433_indirect_test )
{
   try
   {
      ACTORS((alice));

      auto& core = asset_id_type()(db);

      transfer( committee_account, alice_id, asset( 1000000 * asset::scaled_precision( core.precision ) ) );

      const auto& myusd = create_user_issued_asset( "MYUSD", alice, 0 );
      issue_uia( alice, myusd.amount( 2000000000 ) );

      // make sure the database requires our fee to be nonzero
      enable_fees();

      const auto& fees = *db.get_global_properties().parameters.current_fees;
      const auto asset_create_fees = fees.get<asset_create_operation>();

      fund_fee_pool( alice, myusd, 5*asset_create_fees.long_symbol );

      asset_create_operation op;
      op.issuer = alice_id;
      op.symbol = "ALICE";
      op.common_options.core_exchange_rate = asset( 1 ) / asset( 1, asset_id_type( 1 ) );
      op.fee = myusd.amount( ((asset_create_fees.long_symbol + asset_create_fees.price_per_kbyte) & (~1)) );

      const auto proposal_create_fees = fees.get<proposal_create_operation>();
      proposal_create_operation prop;
      prop.fee_paying_account = alice_id;
      prop.proposed_ops.emplace_back( op );
      prop.expiration_time =  db.head_block_time() + fc::days(1);
      prop.fee = asset( proposal_create_fees.fee + proposal_create_fees.price_per_kbyte );
      object_id_type proposal_id;
      {
         signed_transaction tx;
         tx.operations.push_back( prop );
         set_expiration( db, tx );
         sign( tx, alice_private_key );
         proposal_id = PUSH_TX( db, tx ).operation_results.front().get<object_id_type>();
      }
      const proposal_object& proposal = db.get<proposal_object>( proposal_id );

      const auto proposal_update_fees = fees.get<proposal_update_operation>();
      proposal_update_operation pup;
      pup.proposal = proposal.id;
      pup.fee_paying_account = alice_id;
      pup.active_approvals_to_add.insert(alice_id);
      pup.fee = asset( proposal_update_fees.fee + proposal_update_fees.price_per_kbyte );
      {
         signed_transaction tx;
         tx.operations.push_back( pup );
         set_expiration( db, tx );
         sign( tx, alice_private_key );
         PUSH_TX( db, tx );
      }

      verify_asset_supplies( db );
   }
   catch( const fc::exception& e )
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
