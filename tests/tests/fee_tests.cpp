/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fc/uint128.hpp>
#include <graphene/chain/vesting_balance_object.hpp>

#include <boost/test/unit_test.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( fee_tests, database_fixture )

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

BOOST_AUTO_TEST_SUITE_END()
