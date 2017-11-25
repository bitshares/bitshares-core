/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace es_objects {

using namespace chain;


static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

namespace detail
{
    class es_objects_plugin_impl;
}

class es_objects_plugin : public graphene::app::plugin
{
   public:
      es_objects_plugin();
      virtual ~es_objects_plugin();

      std::string plugin_name()const override;
      std::string plugin_description()const override;
      virtual void plugin_set_program_options(
         boost::program_options::options_description& cli,
         boost::program_options::options_description& cfg) override;
      virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
      virtual void plugin_startup() override;

      friend class detail::es_objects_plugin_impl;
      std::unique_ptr<detail::es_objects_plugin_impl> my;
};

struct proposal_struct {
   proposal_id_type id;
   time_point_sec expiration_time;
   optional<time_point_sec> review_period_time;
   std::string proposed_transaction;
};

struct account_struct {
   account_id_type id;
   time_point_sec membership_expiration_date;
   account_id_type registrar;
   account_id_type referrer;
   account_id_type lifetime_referrer;
   uint16_t network_fee_percentage;
   uint16_t lifetime_referrer_fee_percentage;
   uint16_t referrer_rewards_percentage;
   string name;
   string owner;
   string active;
   account_id_type voting_account;
};
struct asset_struct {
   asset_id_type id;
   std::string symbol;
   account_id_type issuer;
};
struct balance_struct {
   balance_id_type id;
   address owner;
   asset_id_type asset_id;
   share_type amount;
};


} } //graphene::es_objects

FC_REFLECT( graphene::es_objects::proposal_struct, (id)(expiration_time)(review_period_time)(proposed_transaction) )
FC_REFLECT( graphene::es_objects::account_struct, (id)(membership_expiration_date)(registrar)(referrer)(lifetime_referrer)(network_fee_percentage)(lifetime_referrer_fee_percentage)(referrer_rewards_percentage)(name)(owner)(active)(voting_account) )
FC_REFLECT( graphene::es_objects::asset_struct, (id)(symbol)(issuer) )
FC_REFLECT( graphene::es_objects::balance_struct, (id)(owner)(asset_id)(amount) )


