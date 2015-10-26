/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace market_history {
using namespace chain;

//
// Plugins should #define their SPACE_ID's so plugins with
// conflicting SPACE_ID assignments can be compiled into the
// same binary (by simply re-assigning some of the conflicting #defined
// SPACE_ID's in a build script).
//
// Assignment of SPACE_ID's cannot be done at run-time because
// various template automagic depends on them being known at compile
// time.
//
#ifndef ACCOUNT_HISTORY_SPACE_ID
#define ACCOUNT_HISTORY_SPACE_ID 5
#endif

struct bucket_key
{
   bucket_key( asset_id_type a, asset_id_type b, uint32_t s, fc::time_point_sec o )
   :base(a),quote(b),seconds(s),open(o){}
   bucket_key(){}

   asset_id_type      base;
   asset_id_type      quote;
   uint32_t           seconds = 0;
   fc::time_point_sec open;

   friend bool operator < ( const bucket_key& a, const bucket_key& b )
   {
      return std::tie( a.base, a.quote, a.seconds, a.open ) < std::tie( b.base, b.quote, b.seconds, b.open );
   }
   friend bool operator == ( const bucket_key& a, const bucket_key& b )
   {
      return std::tie( a.base, a.quote, b.seconds, a.open ) == std::tie( b.base, b.quote, b.seconds, b.open );
   }
};

struct bucket_object : public abstract_object<bucket_object>
{
   static const uint8_t space_id = ACCOUNT_HISTORY_SPACE_ID;
   static const uint8_t type_id  = 1; // market_history_plugin type, referenced from account_history_plugin.hpp

   price high()const { return asset( high_base, key.base ) / asset( high_quote, key.quote ); }
   price low()const { return asset( low_base, key.base ) / asset( low_quote, key.quote ); }

   bucket_key          key;
   share_type          high_base;
   share_type          high_quote;
   share_type          low_base;
   share_type          low_quote;
   share_type          open_base;
   share_type          open_quote;
   share_type          close_base;
   share_type          close_quote;
   share_type          base_volume;
   share_type          quote_volume;
};

struct history_key {
  asset_id_type        base;
  asset_id_type        quote;
  int64_t              sequence = 0;

  friend bool operator < ( const history_key& a, const history_key& b ) {
    return std::tie( a.base, a.quote, a.sequence ) < std::tie( b.base, b.quote, b.sequence );
  }
  friend bool operator == ( const history_key& a, const history_key& b ) {
    return std::tie( a.base, a.quote, a.sequence ) == std::tie( b.base, b.quote, b.sequence );
  }
};
struct order_history_object : public abstract_object<order_history_object>
{
  history_key          key; 
  fc::time_point_sec   time;
  fill_order_operation op;
};

struct by_key;
typedef multi_index_container<
   bucket_object,
   indexed_by<
      hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_key>, member< bucket_object, bucket_key, &bucket_object::key > >
   >
> bucket_object_multi_index_type;

typedef multi_index_container<
   order_history_object,
   indexed_by<
      hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_key>, member< order_history_object, history_key, &order_history_object::key > >
   >
> order_history_multi_index_type;


typedef generic_index<bucket_object, bucket_object_multi_index_type> bucket_index;
typedef generic_index<order_history_object, order_history_multi_index_type> history_index;


namespace detail
{
    class market_history_plugin_impl;
}

/**
 *  The market history plugin can be configured to track any number of intervals via its configuration.  Once per block it
 *  will scan the virtual operations and look for fill_order_operations and then adjust the appropriate bucket objects for
 *  each fill order.
 */
class market_history_plugin : public graphene::app::plugin
{
   public:
      market_history_plugin();
      virtual ~market_history_plugin();

      std::string plugin_name()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(
         const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      uint32_t                    max_history()const;
      const flat_set<uint32_t>&   tracked_buckets()const;

   private:
      friend class detail::market_history_plugin_impl;
      std::unique_ptr<detail::market_history_plugin_impl> my;
};

} } //graphene::market_history

FC_REFLECT( graphene::market_history::history_key, (base)(quote)(sequence) )
FC_REFLECT_DERIVED( graphene::market_history::order_history_object, (graphene::db::object), (key)(time)(op) )
FC_REFLECT( graphene::market_history::bucket_key, (base)(quote)(seconds)(open) )
FC_REFLECT_DERIVED( graphene::market_history::bucket_object, (graphene::db::object), 
                    (key)
                    (high_base)(high_quote)
                    (low_base)(low_quote)
                    (open_base)(open_quote)
                    (close_base)(close_quote)
                    (base_volume)(quote_volume) )

