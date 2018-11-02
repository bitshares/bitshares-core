/*
 * Copyright (c) 2018 oxarbitrage, and contributors.
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
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   time_point_sec expiration_time;
   optional<time_point_sec> review_period_time;
   string proposed_transaction;
   string required_active_approvals;
   string available_active_approvals;
   string required_owner_approvals;
   string available_owner_approvals;
   string available_key_approvals;
   account_id_type proposer;
};
struct account_struct {
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   time_point_sec membership_expiration_date;
   account_id_type registrar;
   account_id_type referrer;
   account_id_type lifetime_referrer;
   uint16_t network_fee_percentage;
   uint16_t lifetime_referrer_fee_percentage;
   uint16_t referrer_rewards_percentage;
   string name;
   string owner_account_auths;
   string owner_key_auths;
   string owner_address_auths;
   string active_account_auths;
   string active_key_auths;
   string active_address_auths;
   account_id_type voting_account;
   string votes;
};
struct asset_struct {
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   string symbol;
   account_id_type issuer;
   bool is_market_issued;
   asset_dynamic_data_id_type dynamic_asset_data_id;
   optional<asset_bitasset_data_id_type> bitasset_data_id;
};
struct balance_struct {
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   account_id_type owner;
   asset_id_type asset_type;
   share_type balance;
   bool maintenance_flag;
};
struct limit_order_struct {
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   time_point_sec expiration;
   account_id_type seller;
   share_type for_sale;
   price sell_price;
   share_type deferred_fee;
};
struct bitasset_struct {
   object_id_type object_id;
   fc::time_point_sec block_time;
   uint32_t block_number;
   string current_feed;
   time_point_sec current_feed_publication_time;
   time_point_sec feed_expiration_time;
};

} } //graphene::es_objects

FC_REFLECT(
        graphene::es_objects::proposal_struct,
        (object_id)(block_time)(block_number)(expiration_time)(review_period_time)(proposed_transaction)(required_active_approvals)
        (available_active_approvals)(required_owner_approvals)(available_owner_approvals)(available_key_approvals)(proposer)
)
FC_REFLECT(
        graphene::es_objects::account_struct,
        (object_id)(block_time)(block_number)(membership_expiration_date)(registrar)(referrer)(lifetime_referrer)
        (network_fee_percentage)(lifetime_referrer_fee_percentage)(referrer_rewards_percentage)(name)(owner_account_auths)
        (owner_key_auths)(owner_address_auths)(active_account_auths)(active_key_auths)(active_address_auths)(voting_account)(votes)
)
FC_REFLECT(
        graphene::es_objects::asset_struct,
        (object_id)(block_time)(block_number)(symbol)(issuer)(is_market_issued)(dynamic_asset_data_id)(bitasset_data_id)
)
FC_REFLECT(
        graphene::es_objects::balance_struct,
        (object_id)(block_time)(block_number)(owner)(asset_type)(balance)(maintenance_flag)
)
FC_REFLECT(
        graphene::es_objects::limit_order_struct,
        (object_id)(block_time)(block_number)(expiration)(seller)(for_sale)(sell_price)(deferred_fee)
)
FC_REFLECT(
        graphene::es_objects::bitasset_struct,
        (object_id)(block_time)(block_number)(current_feed)(current_feed_publication_time)
)