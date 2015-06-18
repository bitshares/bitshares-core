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

#include <graphene/chain/vesting_balance_object.hpp>

#include <boost/test/unit_test.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_SUITE( fee_tests, database_fixture )

BOOST_AUTO_TEST_CASE( cashback_test )
{ try {
   /*                        Account Structure used in this test                         *
    *                                                                                    *
    *               /-----------------\       /-------------------\                      *
    *               | life (Lifetime) |       | reggie (Lifetime) |                      *
    *               \-----------------/       \-------------------/                      *
    *                  | Refers &   | Refers     | Registers  | Registers                *
    *                  v Registers  v            v            |                          *
    *  /----------------\         /----------------\          |                          *
    *  |  ann (Annual)  |         |  dumy (basic)  |          |                          *
    *  \----------------/         \----------------/          |-------------.            *
    *    | Refers      L--------------------------------.     |             |            *
    *    v                     Refers                   v     v             |            *
    *  /----------------\                         /----------------\        |            *
    *  |  scud (basic)  |<------------------------|  stud (basic)  |        |            *
    *  \----------------/      Registers          | (Upgrades to   |        |            *
    *                                             |   Lifetime)    |        v            *
    *                                             \----------------/   /--------------\  *
    *                                                         L------->| pleb (Basic) |  *
    *                                                          Refers  \--------------/  *
    *                                                                                    *
    */
   ACTOR(life);
   ACTOR(reggie);
   PREP_ACTOR(ann);
   PREP_ACTOR(scud);
   PREP_ACTOR(dumy);
   PREP_ACTOR(stud);
   PREP_ACTOR(pleb);
   transfer(account_id_type(), life_id, asset(100000000));
   transfer(account_id_type(), reggie_id, asset(100000000));
   upgrade_to_lifetime_member(life_id);
   upgrade_to_lifetime_member(reggie_id);
   enable_fees(10000);
   const auto& fees = db.get_global_properties().parameters.current_fees;

#define CustomRegisterActor(actor_name, registrar_name, referrer_name, referrer_rate) \
   account_id_type actor_name ## _id; \
   { \
      account_create_operation op; \
      op.registrar = registrar_name ## _id; \
      op.referrer = referrer_name ## _id; \
      op.referrer_percent = referrer_rate*GRAPHENE_1_PERCENT; \
      op.name = BOOST_PP_STRINGIZE(actor_name); \
      op.options.memo_key = actor_name ## _key_id; \
      op.active = authority(1, actor_name ## _key_id, 1); \
      op.owner = op.active; \
      op.fee = op.calculate_fee(fees); \
      trx.operations = {op}; \
      trx.sign(registrar_name ## _key_id, registrar_name ## _private_key); \
      actor_name ## _id = PUSH_TX( db, trx ).operation_results.front().get<object_id_type>(); \
      trx.clear(); \
   }

   CustomRegisterActor(ann, life, life, 75);

   transfer(life_id, ann_id, asset(1000000));
   upgrade_to_annual_member(ann_id);

   CustomRegisterActor(dumy, reggie, life, 75);
   CustomRegisterActor(stud, reggie, ann, 80);

   transfer(life_id, stud_id, asset(1000000));
   upgrade_to_lifetime_member(stud_id);

   CustomRegisterActor(pleb, reggie, stud, 95);
   CustomRegisterActor(scud, stud, ann, 80);

   generate_block();
   generate_blocks(db.get_dynamic_global_properties().next_maintenance_time, true);

   BOOST_CHECK_EQUAL(life_id(db).cashback_balance(db).balance.amount.value, 78000);
   BOOST_CHECK_EQUAL(reggie_id(db).cashback_balance(db).balance.amount.value, 34000);
   BOOST_CHECK_EQUAL(ann_id(db).cashback_balance(db).balance.amount.value, 40000);
   BOOST_CHECK_EQUAL(stud_id(db).cashback_balance(db).balance.amount.value, 8000);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
