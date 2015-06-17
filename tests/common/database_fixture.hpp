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
#pragma once

#include <graphene/app/application.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/key_object.hpp>
#include <fc/io/json.hpp>

using namespace graphene::db;

#define PUSH_TX \
   graphene::chain::test::_push_transaction

#define PUSH_BLOCK \
   graphene::chain::test::_push_block

// See below
#define REQUIRE_OP_VALIDATION_SUCCESS( op, field, value ) \
{ \
   const auto temp = op.field; \
   op.field = value; \
   op.validate(); \
   op.field = temp; \
}
#define REQUIRE_OP_VALIDATION_FAILURE( op, field, value ) \
{ \
   const auto temp = op.field; \
   op.field = value; \
   BOOST_REQUIRE_THROW( op.validate(), fc::exception ); \
   op.field = temp; \
}
#define REQUIRE_OP_EVALUATION_SUCCESS( op, field, value ) \
{ \
   const auto temp = op.field; \
   op.field = value; \
   trx.operations.back() = op; \
   op.field = temp; \
   db.push_transaction( trx, ~0 ); \
}
///Shortcut to require an exception when processing a transaction with an operation containing an expected bad value
/// Uses require insteach of check, because these transactions are expected to fail. If they don't, subsequent tests
/// may spuriously succeed or fail due to unexpected database state.
#define REQUIRE_THROW_WITH_VALUE(op, field, value) \
{ \
   auto bak = op.field; \
   op.field = value; \
   trx.operations.back() = op; \
   op.field = bak; \
   BOOST_REQUIRE_THROW(db.push_transaction(trx, ~0), fc::exception); \
}
///This simply resets v back to its default-constructed value. Requires v to have a working assingment operator and
/// default constructor.
#define RESET(v) v = decltype(v)()
///This allows me to build consecutive test cases. It's pretty ugly, but it works well enough for unit tests.
/// i.e. This allows a test on update_account to begin with the database at the end state of create_account.
#define INVOKE(test) ((struct test*)this)->test_method(); trx.clear()

#define PREP_ACTOR(name) \
   fc::ecc::private_key name ## _private_key = generate_private_key(BOOST_PP_STRINGIZE(name)); \
   key_id_type name ## _key_id = register_key(name ## _private_key.get_public_key()).get_id();
#define ACTOR(name) \
   PREP_ACTOR(name) \
   account_id_type name ## _id = create_account(BOOST_PP_STRINGIZE(name), name ## _key_id).id;
#define GET_ACTOR(name) \
   fc::ecc::private_key name ## _private_key = generate_private_key(BOOST_PP_STRINGIZE(name)); \
   account_id_type name ## _id = get_account(BOOST_PP_STRINGIZE(name)).id; \
   key_id_type name ## _key_id = name ## _id(db).active.auths.begin()->first;

#define ACTORS_IMPL(r, data, elem) ACTOR(elem)
#define ACTORS(names) BOOST_PP_SEQ_FOR_EACH(ACTORS_IMPL, ~, names)

namespace graphene { namespace chain {

struct database_fixture {
   // the reason we use an app is to exercise the indexes of built-in
   //   plugins
   graphene::app::application app;
   chain::database &db;
   signed_transaction trx;
   key_id_type genesis_key;
   account_id_type genesis_account;
   fc::ecc::private_key private_key = fc::ecc::private_key::generate();
   fc::ecc::private_key delegate_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")) );
   fc::time_point_sec genesis_time = fc::time_point_sec( GRAPHENE_GENESIS_TIMESTAMP );
   fc::time_point_sec now          = fc::time_point_sec( GRAPHENE_GENESIS_TIMESTAMP );
   const key_object* key1= nullptr;
   const key_object* key2= nullptr;
   const key_object* key3= nullptr;
   optional<fc::temp_directory> data_dir;
   bool skip_key_index_test = false;
   uint32_t anon_acct_count;

   database_fixture();
   ~database_fixture();

   static fc::ecc::private_key generate_private_key(string seed);
   string generate_anon_acct_name();
   void verify_asset_supplies( )const;
   void verify_account_history_plugin_index( )const;
   void open_database();
   signed_block generate_block(uint32_t skip = ~0,
                               const fc::ecc::private_key& key = generate_private_key("genesis"),
                               int miss_blocks = 0);

   /**
    * @brief Generates block_count blocks
    * @param block_count number of blocks to generate
    */
   void generate_blocks(uint32_t block_count);

   /**
    * @brief Generates blocks until the head block time matches or exceeds timestamp
    * @param timestamp target time to generate blocks until
    */
   void generate_blocks(fc::time_point_sec timestamp, bool miss_intermediate_blocks = true);

   account_create_operation make_account(
      const std::string& name = "nathan",
      key_id_type key = key_id_type()
      );

   account_create_operation make_account(
      const std::string& name,
      const account_object& registrar,
      const account_object& referrer,
      uint8_t referrer_percent = 100,
      key_id_type key = key_id_type()
      );

   const asset_object& get_asset( const string& symbol )const;
   const account_object& get_account( const string& name )const;
   const asset_object& create_bitasset(const string& name,
                                       account_id_type issuer = account_id_type(1),
                                       uint16_t market_fee_percent = 100 /*1%*/,
                                       uint16_t flags = charge_market_fee);
   const asset_object& create_user_issued_asset( const string& name );
   void issue_uia( const account_object& recipient, asset amount );

   const short_order_object* create_short(
      account_id_type seller,
      const asset& amount_to_sell,
      const asset& collateral_provided,
      uint16_t initial_collateral_ratio = 2000,
      uint16_t maintenance_collateral_ratio = 1750
      );
   const short_order_object* create_short(
      const account_object& seller,
      const asset& amount_to_sell,
      const asset& collateral_provided,
      uint16_t initial_collateral_ratio = 2000,
      uint16_t maintenance_collateral_ratio = 1750
      );

   const account_object& create_account(
      const string& name,
      const key_id_type& key = key_id_type()
      );

   const account_object& create_account(
      const string& name,
      const account_object& registrar,
      const account_object& referrer,
      uint8_t referrer_percent = 100,
      const key_id_type& key = key_id_type()
      );

   const account_object& create_account(
      const string& name,
      const private_key_type& key,
      const account_id_type& registrar_id = account_id_type(),
      const account_id_type& referrer_id = account_id_type(),
      uint8_t referrer_percent = 100
      );

   const delegate_object& create_delegate( const account_object& owner );
   const witness_object& create_witness(account_id_type owner,
                                        key_id_type signing_key = key_id_type(),
                                        const fc::ecc::private_key& signing_private_key = generate_private_key("genesis"));
   const witness_object& create_witness(const account_object& owner,
                                        key_id_type signing_key = key_id_type(),
                                        const fc::ecc::private_key& signing_private_key = generate_private_key("genesis"));
   const key_object& register_key( const public_key_type& key );
   const key_object& register_address( const address& addr );
   uint64_t fund( const account_object& account, const asset& amount = asset(500000) );
   void sign( signed_transaction& trx, key_id_type key_id, const fc::ecc::private_key& key );
   const limit_order_object* create_sell_order( account_id_type user, const asset& amount, const asset& recv );
   const limit_order_object* create_sell_order( const account_object& user, const asset& amount, const asset& recv );
   asset cancel_limit_order( const limit_order_object& order );
   asset cancel_short_order( const short_order_object& order );
   void transfer( account_id_type from, account_id_type to, const asset& amount, const asset& fee = asset() );
   void transfer( const account_object& from, const account_object& to, const asset& amount, const asset& fee = asset() );
   void fund_fee_pool( const account_object& from, const asset_object& asset_to_fund, const share_type amount );
   void enable_fees( share_type fee = GRAPHENE_BLOCKCHAIN_PRECISION );
   void upgrade_to_lifetime_member( account_id_type account );
   void upgrade_to_lifetime_member( const account_object& account );
   void upgrade_to_annual_member( account_id_type account );
   void upgrade_to_annual_member( const account_object& account );
   void print_market( const string& syma, const string& symb )const;
   string pretty( const asset& a )const;
   void print_short_order( const short_order_object& cur )const;
   void print_limit_order( const limit_order_object& cur )const;
   void print_call_orders( )const;
   void print_joint_market( const string& syma, const string& symb )const;
   void print_short_market( const string& syma, const string& symb )const;
   int64_t get_balance( account_id_type account, asset_id_type a )const;
   int64_t get_balance( const account_object& account, const asset_object& a )const;
};

namespace test {
bool _push_block( database& db, const signed_block& b, uint32_t skip_flags = 0 );
processed_transaction _push_transaction( database& db, const signed_transaction& tx, uint32_t skip_flags = 0 );
}

} }
