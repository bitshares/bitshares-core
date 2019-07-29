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

#include <fc/uint128.hpp>

#include <graphene/chain/hardfork.hpp>

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
    * Fee distribution chains (50/30/20 referral/marketing partner/net split, 20-30 referrer/LTM split)       *
    * life : 50% -> life, 30% -> mp, 20% -> net                                                     *
    * rog: 50% -> rog, 20% -> net                                                        *
    * ann (before upg): 50% -> life, 30% -> mp, 20% -> net                                          *
    * ann (after upg): 75% * 20% -> ann, 25% * 20% + 30% -> life, 20% -> mp, 20% -> net                   *
    * stud (before upg): 50% * 5/8 -> ann, 50% * 3/8 -> life, 20% * 50% -> rog,          *
    *                    30% -> mp, 20% -> net                                                      *
    * stud (after upg): 50% -> stud, 30% -> mp, 20% -> net                                            *
    * dumy : 75% * 20% + 30% -> life, 25% * 20% -> rog, 30% -> mp, 20% -> net                              *
    * scud : 20% * 80% -> ann, 30% -> life, 20% * 20%-> stud, 30% -> mp, 20% -> net           *
    * pleb : 95% * 20% + 30% -> stud, 5% * 20% -> rog, 30% -> mp, 20% -> net                               *
    */

   BOOST_TEST_MESSAGE("Creating actors");

   ACTOR(life);
   ACTOR(rog);
   ACTOR(nathan);
   PREP_ACTOR(ann);
   PREP_ACTOR(scud);
   PREP_ACTOR(dumy);
   PREP_ACTOR(stud);
   PREP_ACTOR(pleb);
   // use ##_public_key vars to silence unused variable warning
   BOOST_CHECK_GT(ann_public_key.key_data.size(), 0u);
   BOOST_CHECK_GT(scud_public_key.key_data.size(), 0u);
   BOOST_CHECK_GT(dumy_public_key.key_data.size(), 0u);
   BOOST_CHECK_GT(stud_public_key.key_data.size(), 0u);
   BOOST_CHECK_GT(pleb_public_key.key_data.size(), 0u);

   account_id_type ann_id, scud_id, dumy_id, stud_id, pleb_id;
   actor_audit alife, arog, anathan, aann, ascud, adumy, astud, apleb;

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
   upgrade_to_lifetime_member(nathan_id);

   BOOST_TEST_MESSAGE("Enable fees");
   enable_fees();

   // Enabling fees sets them to crazy high defaults so let's lower them to
   // something more manageable
   flat_set< fee_parameters > new_fees;
   {
      account_create_operation::fee_parameters_type acc_fee_params;
      acc_fee_params.basic_fee = 100;
      acc_fee_params.premium_fee = 100;
      acc_fee_params.price_per_kbyte = 0;
      new_fees.insert( acc_fee_params );
   }
   {
      transfer_operation::fee_parameters_type transfer_fee_params;
      transfer_fee_params.fee = 200;
      transfer_fee_params.price_per_kbyte = 0;
      new_fees.insert( transfer_fee_params );
   }
   {
      account_upgrade_operation::fee_parameters_type upgrade_fee_params;
      upgrade_fee_params.membership_annual_fee = 300;
      upgrade_fee_params.membership_lifetime_fee = 400;
      new_fees.insert( upgrade_fee_params );
   }
   change_fees( new_fees );

   const auto& fees = db.get_global_properties().parameters.get_current_fees();

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
      op.fee = fees.calculate_fee(op); \
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

   int64_t reg_fee    = fees.get< account_create_operation >().premium_fee;
   int64_t xfer_fee   = fees.get< transfer_operation >().fee;
   int64_t upg_an_fee = fees.get< account_upgrade_operation >().membership_annual_fee;
   int64_t upg_lt_fee = fees.get< account_upgrade_operation >().membership_lifetime_fee;
   // all percentages here are cut from whole pie!
   uint64_t network_pct = 10 * P1;
   uint64_t lt_pct = 30 * P1;
   uint64_t mp_pct = 30 * P1;
   uint64_t ch_pct = 10 * P1;

   BOOST_TEST_MESSAGE("Register and upgrade Ann");
   {
      CustomRegisterActor(ann, life, life, 75);
      alife.vcb += reg_fee; alife.bal += -reg_fee;
      anathan.ubal += pct( mp_pct, aann.ucb );
      anathan.ubal += pct( ch_pct, aann.ucb  );
      anathan.ubal += pct( mp_pct, reg_fee );
      anathan.ubal += pct( ch_pct, reg_fee );
      CustomAudit();

      transfer(life_id, ann_id, asset(aann.b0));
      alife.vcb += xfer_fee; alife.bal += -xfer_fee -aann.b0; aann.bal += aann.b0;
      anathan.ubal += pct( mp_pct, xfer_fee );
      anathan.ubal += pct( ch_pct, xfer_fee );
      CustomAudit();

      upgrade_to_annual_member(ann_id);
      aann.ucb += upg_an_fee; aann.bal += -upg_an_fee;
      anathan.ubal += pct( mp_pct, upg_an_fee );
      anathan.ubal += pct( ch_pct, upg_an_fee );

      // audit distribution of fees from Ann
      alife.ubal += pct( P100-network_pct-mp_pct-ch_pct, aann.ucb );
      alife.bal  += pct( P100-network_pct-mp_pct-ch_pct, aann.vcb );
      aann.ucb = 0; aann.vcb = 0;
      CustomAudit();
   }

   BOOST_TEST_MESSAGE("Register dumy and stud");
   CustomRegisterActor(dumy, rog, life, 75);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   anathan.ubal += pct( mp_pct, reg_fee );
   anathan.ubal += pct( ch_pct, reg_fee );
   CustomAudit();

   CustomRegisterActor(stud, rog, ann, 80);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   anathan.ubal += pct( mp_pct, reg_fee );
   anathan.ubal += pct( ch_pct, reg_fee );
   CustomAudit();

   BOOST_TEST_MESSAGE("Upgrade stud to lifetime member");

   transfer(life_id, stud_id, asset(astud.b0));
   alife.vcb += xfer_fee; alife.bal += -astud.b0 -xfer_fee; astud.bal += astud.b0;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   upgrade_to_lifetime_member(stud_id);
   astud.ucb += upg_lt_fee; astud.bal -= upg_lt_fee;
   anathan.ubal += pct( mp_pct, upg_lt_fee );
   anathan.ubal += pct( ch_pct, upg_lt_fee );

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
   // life gets lifetime amount
   alife.ubal += pct( lt_pct, astud.ucb );
   // ann gets 80% of referral amount from stud's fees
   aann.ubal  += pct( astud.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, astud.ucb );
   // rog gets the remaining 20% of referral amount from stud's fees
   arog.ubal  += pct( P100-astud.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, astud.ucb );
   astud.ucb  = 0;
   CustomAudit();

   BOOST_TEST_MESSAGE("Register pleb and scud");

   CustomRegisterActor(pleb, rog, stud, 95);
   arog.vcb += reg_fee; arog.bal += -reg_fee;
   anathan.ubal += pct( mp_pct, reg_fee );
   anathan.ubal += pct( ch_pct, reg_fee );
   CustomAudit();

   CustomRegisterActor(scud, stud, ann, 80);
   astud.vcb += reg_fee; astud.bal += -reg_fee;
   anathan.ubal += pct( mp_pct, reg_fee );
   anathan.ubal += pct( ch_pct, reg_fee );
   CustomAudit();

   CHECK_BALANCE( nathan, anathan.bal ); // should still be 0
   generate_block();

   BOOST_TEST_MESSAGE("Wait for maintenance interval");

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   anathan.bal += anathan.ubal;
   anathan.ubal = 0;
   CHECK_BALANCE( nathan, anathan.bal ); // should be updated to sum of mp_pct of all fees and ch_pct of all fees
   change_fees( new_fees );

   // audit distribution of fees from life
   alife.ubal += pct( P100-network_pct-mp_pct-ch_pct, alife.ucb +alife.vcb );
   alife.ucb = 0; alife.vcb = 0;

   // audit distribution of fees from rog
   arog.ubal += pct( P100-network_pct-mp_pct-ch_pct, arog.ucb + arog.vcb );
   arog.ucb = 0; arog.vcb = 0;

   // audit distribution of fees from ann
   alife.ubal += pct(lt_pct, aann.ucb+aann.vcb );
   alife.ubal += pct(P100-adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, aann.ucb+aann.vcb );
   aann.ubal  += pct(adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct,  aann.ucb+aann.vcb );
   aann.ucb = 0; aann.vcb = 0;

   // audit distribution of fees from stud
   astud.ubal += pct( P100-network_pct-mp_pct-ch_pct,  astud.ucb+astud.vcb );
   astud.ucb = 0; astud.vcb = 0;

   // audit distribution of fees from dumy
   alife.ubal += pct(adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, adumy.ucb+adumy.vcb );
   alife.ubal += pct(lt_pct, adumy.ucb+adumy.vcb );
   arog.ubal  += pct(P100-adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, adumy.ucb+adumy.vcb );
   adumy.ucb = 0; adumy.vcb = 0;

   // audit distribution of fees from scud
   alife.ubal += pct( lt_pct,  ascud.ucb+ascud.vcb );
   aann.ubal  += pct(ascud.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   astud.ubal += pct(P100-ascud.ref_pct,  P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   // audit distribution of fees from pleb
   astud.ubal += pct(apleb.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, apleb.ucb+apleb.vcb );
   astud.ubal += pct(lt_pct, apleb.ucb+apleb.vcb );
   arog.ubal  += pct(P100-apleb.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, apleb.ucb+apleb.vcb );
   apleb.ucb = 0; apleb.vcb = 0;

   CustomAudit();

   BOOST_TEST_MESSAGE("Doing some transfers");

   transfer(stud_id, scud_id, asset(500000));
   astud.bal += -500000-xfer_fee; astud.vcb += xfer_fee; ascud.bal += 500000;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   transfer(scud_id, pleb_id, asset(400000));
   ascud.bal += -400000-xfer_fee; ascud.vcb += xfer_fee; apleb.bal += 400000;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   transfer(pleb_id, dumy_id, asset(300000));
   apleb.bal += -300000-xfer_fee; apleb.vcb += xfer_fee; adumy.bal += 300000;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   transfer(dumy_id, rog_id, asset(200000));
   adumy.bal += -200000-xfer_fee; adumy.vcb += xfer_fee; arog.bal += 200000;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   CHECK_BALANCE( nathan, anathan.bal );
   generate_block();
   BOOST_TEST_MESSAGE("Waiting for maintenance time");

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   anathan.bal += anathan.ubal;
   anathan.ubal = 0;
   CHECK_BALANCE( nathan, anathan.bal );
   change_fees( new_fees );
   // audit distribution of fees from life
   alife.ubal += pct( P100-network_pct-mp_pct-ch_pct, alife.ucb +alife.vcb );
   alife.ucb = 0; alife.vcb = 0;

   // audit distribution of fees from rog
   arog.ubal += pct( P100-network_pct-mp_pct-ch_pct, arog.ucb + arog.vcb );
   arog.ucb = 0; arog.vcb = 0;

   // audit distribution of fees from ann
   alife.ubal += pct(lt_pct, aann.ucb+aann.vcb );
   alife.ubal += pct(P100-adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, aann.ucb+aann.vcb );
   aann.ubal  += pct(adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct,  aann.ucb+aann.vcb );
   //aann.ubal  += pct( P100-network_pct-mp_pct, P100-lt_pct,      aann.ref_pct, aann.ucb+aann.vcb );
   //alife.ubal += pct( P100-network_pct-mp_pct, P100-lt_pct, P100-aann.ref_pct, aann.ucb+aann.vcb );
   aann.ucb = 0; aann.vcb = 0;

   // audit distribution of fees from stud
   astud.ubal += pct( P100-network_pct-mp_pct-ch_pct,                                  astud.ucb+astud.vcb );
   astud.ucb = 0; astud.vcb = 0;

   // audit distribution of fees from dumy
   alife.ubal += pct(adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, adumy.ucb+adumy.vcb );
   alife.ubal += pct(lt_pct, adumy.ucb+adumy.vcb );
   arog.ubal  += pct(P100-adumy.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, adumy.ucb+adumy.vcb );
   adumy.ucb = 0; adumy.vcb = 0;

   // audit distribution of fees from scud
   alife.ubal += pct( lt_pct,  ascud.ucb+ascud.vcb );
   aann.ubal  += pct(ascud.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   astud.ubal += pct(P100-ascud.ref_pct,  P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   // audit distribution of fees from pleb
   astud.ubal += pct(apleb.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, apleb.ucb+apleb.vcb );
   astud.ubal += pct(lt_pct, apleb.ucb+apleb.vcb );
   arog.ubal  += pct(P100-apleb.ref_pct, P100-network_pct-mp_pct-ch_pct-lt_pct, apleb.ucb+apleb.vcb );
   apleb.ucb = 0; apleb.vcb = 0;

   CustomAudit();

   BOOST_TEST_MESSAGE("Waiting for annual membership to expire");

   generate_blocks(ann_id(db).membership_expiration_date);
   generate_block();
   CHECK_BALANCE( nathan, anathan.bal );
   change_fees( new_fees );
   BOOST_TEST_MESSAGE("Transferring from scud to pleb");

   //ann's membership has expired, so scud's fee should go up to life instead.
   transfer(scud_id, pleb_id, asset(10));
   ascud.bal += -10-xfer_fee; ascud.vcb += xfer_fee; apleb.bal += 10;
   anathan.ubal += pct( mp_pct, xfer_fee );
   anathan.ubal += pct( ch_pct, xfer_fee );
   CustomAudit();

   BOOST_TEST_MESSAGE("Waiting for maint interval");

   CHECK_BALANCE( nathan, anathan.bal );
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   anathan.bal += anathan.ubal;
   anathan.ubal = 0;
   CHECK_BALANCE( nathan, anathan.bal );
   change_fees( new_fees );
   // audit distribution of fees from scud
   alife.ubal += pct( lt_pct,  ascud.ucb+ascud.vcb );
   aann.ubal  += pct(80*P1, P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   astud.ubal += pct(20*P1,  P100-network_pct-mp_pct-ch_pct-lt_pct,  ascud.ucb+ascud.vcb );
   ascud.ucb = 0; ascud.vcb = 0;

   CustomAudit();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( account_create_fee_scaling )
{ try {
   auto accounts_per_scale = db.get_global_properties().parameters.accounts_per_fee_scale;
   db.modify(global_property_id_type()(db), [](global_property_object& gpo)
   {
      gpo.parameters.get_mutable_fees() = fee_schedule::get_default();
      gpo.parameters.get_mutable_fees().get<account_create_operation>().basic_fee = 1;
   });

   for( int i = db.get_dynamic_global_properties().accounts_registered_this_interval; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.get_current_fees().get<account_create_operation>().basic_fee, 1u);
      create_account("shill" + fc::to_string(i));
   }
   for( int i = 0; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.get_current_fees().get<account_create_operation>().basic_fee, 16u);
      create_account("moreshills" + fc::to_string(i));
   }
   for( int i = 0; i < accounts_per_scale; ++i )
   {
      BOOST_CHECK_EQUAL(db.get_global_properties().parameters.get_current_fees().get<account_create_operation>().basic_fee, 256u);
      create_account("moarshills" + fc::to_string(i));
   }
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.get_current_fees().get<account_create_operation>().basic_fee, 4096u);

   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
   BOOST_CHECK_EQUAL(db.get_global_properties().parameters.get_current_fees().get<account_create_operation>().basic_fee, 1u);
} FC_LOG_AND_RETHROW() }

// BOOST_AUTO_TEST_CASE( fee_refund_test )
// {
//    try
//    {
//       ACTORS((alice)(bob)(izzy));

//       int64_t alice_b0 = 1000000, bob_b0 = 1000000;

//       transfer( account_id_type(), alice_id, asset(alice_b0) );
//       transfer( account_id_type(), bob_id, asset(bob_b0) );

//       asset_id_type core_id = asset_id_type();
//       asset_id_type usd_id = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee ).id;
//       issue_uia( alice_id, asset( alice_b0, usd_id ) );
//       issue_uia( bob_id, asset( bob_b0, usd_id ) );

//       int64_t order_create_fee = 537;
//       int64_t order_cancel_fee = 129;

//       uint32_t skip = database::skip_witness_signature
//                     | database::skip_transaction_signatures
//                     | database::skip_transaction_dupe_check
//                     | database::skip_block_size_check
//                     | database::skip_tapos_check
//                     | database::skip_merkle_check
//                     ;

//       generate_block( skip );

//       for( int i=0; i<2; i++ )
//       {
//          if( i == 1 )
//          {
//             generate_blocks( HARDFORK_445_TIME, true, skip );
//             generate_block( skip );
//          }

//          // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
//          // so we have to do it every time we stop generating/popping blocks and start doing tx's
//          enable_fees();
//          /*
//          change_fees({
//                        limit_order_create_operation::fee_parameters_type { order_create_fee },
//                        limit_order_cancel_operation::fee_parameters_type { order_cancel_fee }
//                      });
//          */
//          // C++ -- The above commented out statement doesn't work, I don't know why
//          // so we will use the following rather lengthy initialization instead
//          // {
//          //    flat_set< fee_parameters > new_fees;
//          //    {
//          //       limit_order_create_operation::fee_parameters_type create_fee_params;
//          //       create_fee_params.fee = order_create_fee;
//          //       new_fees.insert( create_fee_params );
//          //    }
//          //    {
//          //       limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
//          //       cancel_fee_params.fee = order_cancel_fee;
//          //       new_fees.insert( cancel_fee_params );
//          //    }
//          //    change_fees( new_fees );
//          // }

//          // Alice creates order
//          // Bob creates order which doesn't match

//          // AAAAGGHH create_sell_order reads trx.expiration #469
//          set_expiration( db, trx );

//          // Check non-overlapping

//          limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(1000, usd_id) )->id;
//          limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1000) )->id;

//          BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - 1000 - order_create_fee );
//          BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - order_create_fee );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 - 500 );

//          // Bob cancels order
//          cancel_limit_order( bo1_id(db) );

//          int64_t cancel_net_fee;
//          if( db.head_block_time() > HARDFORK_445_TIME )
//             cancel_net_fee = order_cancel_fee;
//          else
//             cancel_net_fee = order_create_fee + order_cancel_fee;

//          BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - 1000 - order_create_fee );
//          BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - cancel_net_fee );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 );

//          // Alice cancels order
//          cancel_limit_order( ao1_id(db) );

//          BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee );
//          BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_b0 - cancel_net_fee );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_b0 );

//          // Check partial fill
//          const limit_order_object* ao2 = create_sell_order( alice_id, asset(1000), asset(200, usd_id) );
//          const limit_order_object* bo2 = create_sell_order(   bob_id, asset(100, usd_id), asset(500) );

//          BOOST_CHECK( ao2 != nullptr );
//          BOOST_CHECK( bo2 == nullptr );

//          BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee - order_create_fee - 1000 );
//          BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 + 100 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ),   bob_b0 - cancel_net_fee - order_create_fee + 500 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ),   bob_b0 - 100 );

//          // cancel Alice order, show that entire deferred_fee was consumed by partial match
//          cancel_limit_order( *ao2 );

//          BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_b0 - cancel_net_fee - order_create_fee - 500 - order_cancel_fee );
//          BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_b0 + 100 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ),   bob_b0 - cancel_net_fee - order_create_fee + 500 );
//          BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ),   bob_b0 - 100 );

//          // TODO: Check multiple fill
//          // there really should be a test case involving Alice creating multiple orders matched by single Bob order
//          // but we'll save that for future cleanup

//          // undo above tx's and reset
//          generate_block( skip );
//          db.pop_block();
//       }
//    }
//    FC_LOG_AND_RETHROW()
// }

// BOOST_AUTO_TEST_CASE( hf445_fee_refund_cross_test )
// { // create orders before hard fork, cancel them after hard fork
//    try
//    {
//       ACTORS((alice)(bob)(izzy));

//       int64_t alice_b0 = 1000000, bob_b0 = 1000000;
//       int64_t pool_0 = 100000, accum_0 = 0;

//       transfer( account_id_type(), alice_id, asset(alice_b0) );
//       transfer( account_id_type(), bob_id, asset(bob_b0) );

//       asset_id_type core_id = asset_id_type();
//       const auto& usd_obj = create_user_issued_asset( "IZZYUSD", izzy_id(db), charge_market_fee );
//       asset_id_type usd_id = usd_obj.id;
//       issue_uia( alice_id, asset( alice_b0, usd_id ) );
//       issue_uia( bob_id, asset( bob_b0, usd_id ) );

//       fund_fee_pool( committee_account( db ), usd_obj, pool_0 );

//       int64_t order_create_fee = 537;
//       int64_t order_cancel_fee = 129;

//       uint32_t skip = database::skip_witness_signature
//                     | database::skip_transaction_signatures
//                     | database::skip_transaction_dupe_check
//                     | database::skip_block_size_check
//                     | database::skip_tapos_check
//                     | database::skip_merkle_check
//                     ;

//       generate_block( skip );

//       flat_set< fee_parameters > new_fees;
//       {
//          limit_order_create_operation::fee_parameters_type create_fee_params;
//          create_fee_params.fee = order_create_fee;
//          new_fees.insert( create_fee_params );
//       }
//       {
//          limit_order_cancel_operation::fee_parameters_type cancel_fee_params;
//          cancel_fee_params.fee = order_cancel_fee;
//          new_fees.insert( cancel_fee_params );
//       }
//       {
//          transfer_operation::fee_parameters_type transfer_fee_params;
//          transfer_fee_params.fee = 0;
//          transfer_fee_params.price_per_kbyte = 0;
//          new_fees.insert( transfer_fee_params );
//       }

//       // enable_fees() and change_fees() modifies DB directly, and results will be overwritten by block generation
//       // so we have to do it every time we stop generating/popping blocks and start doing tx's
//       enable_fees();
//       change_fees( new_fees );

//       // AAAAGGHH create_sell_order reads trx.expiration #469
//       set_expiration( db, trx );

//       // prepare params
//       const chain_parameters& params = db.get_global_properties().parameters;
//       time_point_sec max_exp = time_point_sec::maximum();
//       time_point_sec exp = HARDFORK_445_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 3 );
//       time_point_sec exp2 = HARDFORK_445_TIME + fc::seconds( params.block_interval * (params.maintenance_skip_slots + 1) * 13 );
//       price cer( asset(1), asset(1, usd_id) );
//       const auto* usd_stat = &usd_id( db ).dynamic_asset_data_id( db );

//       // balance data
//       int64_t alice_bc = alice_b0, bob_bc = bob_b0; // core balance
//       int64_t alice_bu = alice_b0, bob_bu = bob_b0; // usd balance
//       int64_t pool_b = pool_0, accum_b = accum_0;

//       // prepare orders
//       BOOST_TEST_MESSAGE( "Creating orders those will never match: ao1, ao2, bo1, bo2 .." );
//       // ao1: won't expire, won't match, fee in core
//       limit_order_id_type ao1_id = create_sell_order( alice_id, asset(1000), asset(100000, usd_id) )->id;
//       BOOST_CHECK( db.find( ao1_id ) != nullptr );
//       // ao2: will expire, won't match, fee in core
//       limit_order_id_type ao2_id = create_sell_order( alice_id, asset(800), asset(100000, usd_id), exp )->id;
//       BOOST_CHECK( db.find( ao2_id ) != nullptr );
//       // bo1: won't expire, won't match, fee in usd
//       limit_order_id_type bo1_id = create_sell_order(   bob_id, asset(1000, usd_id), asset(100000), max_exp, cer )->id;
//       BOOST_CHECK( db.find( bo1_id ) != nullptr );
//       // bo2: will expire, won't match, fee in usd
//       limit_order_id_type bo2_id = create_sell_order(   bob_id, asset(800, usd_id), asset(100000), exp, cer )->id;
//       BOOST_CHECK( db.find( bo2_id ) != nullptr );

//       alice_bc -= order_create_fee * 2;
//       alice_bc -= 1000;
//       alice_bc -= 800;
//       bob_bu -= order_create_fee * 2;
//       bob_bu -= 1000;
//       bob_bu -= 800;
//       pool_b -= order_create_fee * 2;
//       accum_b += order_create_fee * 2;
//       int64_t ao1_remain = 1000;
//       int64_t ao2_remain = 800;
//       int64_t bo1_remain = 1000;
//       int64_t bo2_remain = 800;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // ao3: won't expire, partially match before hard fork 445, fee in core
//       BOOST_TEST_MESSAGE( "Creating order ao3 .." );
//       limit_order_id_type ao3_id = create_sell_order( alice_id, asset(900), asset(2700, usd_id) )->id;
//       BOOST_CHECK( db.find( ao3_id ) != nullptr );
//       create_sell_order( bob_id, asset(600, usd_id), asset(200) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 900;
//       alice_bu += 600;
//       bob_bc -= order_create_fee;
//       bob_bu -= 600;
//       bob_bc += 200;
//       int64_t ao3_remain = 900 - 200;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // ao4: will expire, will partially match before hard fork 445, fee in core
//       BOOST_TEST_MESSAGE( "Creating order ao4 .." );
//       limit_order_id_type ao4_id = create_sell_order( alice_id, asset(700), asset(1400, usd_id), exp )->id;
//       BOOST_CHECK( db.find( ao4_id ) != nullptr );
//       create_sell_order( bob_id, asset(200, usd_id), asset(100) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 700;
//       alice_bu += 200;
//       bob_bc -= order_create_fee;
//       bob_bu -= 200;
//       bob_bc += 100;
//       int64_t ao4_remain = 700 - 100;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // bo3: won't expire, will partially match before hard fork 445, fee in usd
//       BOOST_TEST_MESSAGE( "Creating order bo3 .." );
//       limit_order_id_type bo3_id = create_sell_order(   bob_id, asset(500, usd_id), asset(1500), max_exp, cer )->id;
//       BOOST_CHECK( db.find( bo3_id ) != nullptr );
//       create_sell_order( alice_id, asset(450), asset(150, usd_id) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 450;
//       alice_bu += 150;
//       bob_bu -= order_create_fee;
//       bob_bu -= 500;
//       bob_bc += 450;
//       pool_b -= order_create_fee;
//       accum_b += order_create_fee;
//       int64_t bo3_remain = 500 - 150;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // bo4: will expire, will partially match before hard fork 445, fee in usd
//       BOOST_TEST_MESSAGE( "Creating order bo4 .." );
//       limit_order_id_type bo4_id = create_sell_order(   bob_id, asset(300, usd_id), asset(600), exp, cer )->id;
//       BOOST_CHECK( db.find( bo4_id ) != nullptr );
//       create_sell_order( alice_id, asset(140), asset(70, usd_id) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 140;
//       alice_bu += 70;
//       bob_bu -= order_create_fee;
//       bob_bu -= 300;
//       bob_bc += 140;
//       pool_b -= order_create_fee;
//       accum_b += order_create_fee;
//       int64_t bo4_remain = 300 - 70;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // ao5: won't expire, partially match after hard fork 445, fee in core
//       BOOST_TEST_MESSAGE( "Creating order ao5 .." );
//       limit_order_id_type ao5_id = create_sell_order( alice_id, asset(606), asset(909, usd_id) )->id;
//       BOOST_CHECK( db.find( ao5_id ) != nullptr );

//       alice_bc -= order_create_fee;
//       alice_bc -= 606;
//       int64_t ao5_remain = 606;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // ao6: will expire, partially match after hard fork 445, fee in core
//       BOOST_TEST_MESSAGE( "Creating order ao6 .." );
//       limit_order_id_type ao6_id = create_sell_order( alice_id, asset(333), asset(444, usd_id), exp2 )->id;
//       BOOST_CHECK( db.find( ao6_id ) != nullptr );

//       alice_bc -= order_create_fee;
//       alice_bc -= 333;
//       int64_t ao6_remain = 333;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // bo5: won't expire, partially match after hard fork 445, fee in usd
//       BOOST_TEST_MESSAGE( "Creating order bo5 .." );
//       limit_order_id_type bo5_id = create_sell_order(   bob_id, asset(255, usd_id), asset(408), max_exp, cer )->id;
//       BOOST_CHECK( db.find( bo5_id ) != nullptr );

//       bob_bu -= order_create_fee;
//       bob_bu -= 255;
//       pool_b -= order_create_fee;
//       accum_b += order_create_fee;
//       int64_t bo5_remain = 255;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // bo6: will expire, partially match after hard fork 445, fee in usd
//       BOOST_TEST_MESSAGE( "Creating order bo6 .." );
//       limit_order_id_type bo6_id = create_sell_order(   bob_id, asset(127, usd_id), asset(127), exp2, cer )->id;
//       BOOST_CHECK( db.find( bo6_id ) != nullptr );

//       bob_bu -= order_create_fee;
//       bob_bu -= 127;
//       pool_b -= order_create_fee;
//       accum_b += order_create_fee;
//       int64_t bo6_remain = 127;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // generate block so the orders will be in db before hard fork
//       BOOST_TEST_MESSAGE( "Generating blocks ..." );
//       generate_block( skip );

//       // generate blocks util hard fork 445
//       generate_blocks( HARDFORK_445_TIME, true, skip );
//       generate_block( skip );

//       // nothing will change
//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // generate more blocks, so some orders will expire
//       generate_blocks( exp, true, skip );

//       // no fee refund for orders created before hard fork 445, but remaining funds will be refunded
//       BOOST_TEST_MESSAGE( "Checking expired orders: ao2, ao4, bo2, bo4 .." );
//       alice_bc += ao2_remain;
//       alice_bc += ao4_remain;
//       bob_bu += bo2_remain;
//       bob_bu += bo4_remain;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // prepare for new transactions
//       enable_fees();
//       change_fees( new_fees );
//       usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
//       set_expiration( db, trx );

//       // cancel ao1
//       BOOST_TEST_MESSAGE( "Cancel order ao1 .." );
//       cancel_limit_order( ao1_id(db) );

//       alice_bc += ao1_remain;
//       alice_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // cancel ao3
//       BOOST_TEST_MESSAGE( "Cancel order ao3 .." );
//       cancel_limit_order( ao3_id(db) );

//       alice_bc += ao3_remain;
//       alice_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // cancel bo1
//       BOOST_TEST_MESSAGE( "Cancel order bo1 .." );
//       cancel_limit_order( bo1_id(db) );

//       bob_bu += bo1_remain;
//       bob_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // cancel bo3
//       BOOST_TEST_MESSAGE( "Cancel order bo3 .." );
//       cancel_limit_order( bo3_id(db) );

//       bob_bu += bo3_remain;
//       bob_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // partially fill ao6
//       BOOST_TEST_MESSAGE( "Partially fill ao6 .." );
//       create_sell_order( bob_id, asset(88, usd_id), asset(66) );

//       alice_bu += 88;
//       bob_bc -= order_create_fee;
//       bob_bu -= 88;
//       bob_bc += 66;
//       ao6_remain -= 66;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // partially fill bo6
//       BOOST_TEST_MESSAGE( "Partially fill bo6 .." );
//       create_sell_order( alice_id, asset(59), asset(59, usd_id) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 59;
//       alice_bu += 59;
//       bob_bc += 59;
//       bo6_remain -= 59;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // generate block to save the changes
//       BOOST_TEST_MESSAGE( "Generating blocks ..." );
//       generate_block( skip );

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // generate blocks util exp2, so some orders will expire
//       generate_blocks( exp2, true, skip );

//       // no fee refund for orders created before hard fork 445, but remaining funds will be refunded
//       BOOST_TEST_MESSAGE( "Checking expired orders: ao6, bo6 .." );
//       alice_bc += ao6_remain;
//       bob_bu += bo6_remain;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // prepare for new transactions
//       enable_fees();
//       change_fees( new_fees );
//       usd_stat = &usd_id( db ).dynamic_asset_data_id( db );
//       set_expiration( db, trx );

//       // partially fill ao5
//       BOOST_TEST_MESSAGE( "Partially fill ao5 .." );
//       create_sell_order( bob_id, asset(93, usd_id), asset(62) );

//       alice_bu += 93;
//       bob_bc -= order_create_fee;
//       bob_bu -= 93;
//       bob_bc += 62;
//       ao5_remain -= 62;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // partially fill bo5
//       BOOST_TEST_MESSAGE( "Partially fill bo5 .." );
//       create_sell_order( alice_id, asset(24), asset(15, usd_id) );

//       alice_bc -= order_create_fee;
//       alice_bc -= 24;
//       alice_bu += 15;
//       bob_bc += 24;
//       bo5_remain -= 15;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // cancel ao5
//       BOOST_TEST_MESSAGE( "Cancel order ao5 .." );
//       cancel_limit_order( ao5_id(db) );

//       alice_bc += ao5_remain;
//       alice_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // cancel bo5
//       BOOST_TEST_MESSAGE( "Cancel order bo5 .." );
//       cancel_limit_order( bo5_id(db) );

//       bob_bu += bo5_remain;
//       bob_bc -= order_cancel_fee;

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//       // generate block to save the changes
//       BOOST_TEST_MESSAGE( "Generating blocks ..." );
//       generate_block( skip );

//       BOOST_CHECK_EQUAL( get_balance( alice_id, core_id ), alice_bc );
//       BOOST_CHECK_EQUAL( get_balance( alice_id,  usd_id ), alice_bu );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id, core_id ), bob_bc );
//       BOOST_CHECK_EQUAL( get_balance(   bob_id,  usd_id ), bob_bu );
//       BOOST_CHECK_EQUAL( usd_stat->fee_pool.value,          pool_b );
//       BOOST_CHECK_EQUAL( usd_stat->accumulated_fees.value,  accum_b );

//    }
//    FC_LOG_AND_RETHROW()
// }

BOOST_AUTO_TEST_SUITE_END()
