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

#include <graphene/es_objects/es_objects.hpp>

#include <fc/smart_ref_impl.hpp>

#include <curl/curl.h>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <graphene/utilities/elasticsearch.hpp>


namespace graphene { namespace es_objects {

namespace detail
{

class es_objects_plugin_impl
{
   public:
      es_objects_plugin_impl(es_objects_plugin& _plugin)
         : _self( _plugin )
      {  curl = curl_easy_init(); }
      virtual ~es_objects_plugin_impl();

      void updateDatabase( const vector<object_id_type>& ids , bool isNew);

      es_objects_plugin& _self;
      std::string _es_objects_elasticsearch_url = "http://localhost:9200/";
      uint32_t _es_objects_bulk_replay = 5000;
      uint32_t _es_objects_bulk_sync = 10;
      bool _es_objects_proposals = true;
      bool _es_objects_accounts = true;
      bool _es_objects_assets = true;
      bool _es_objects_balances = true;
      bool _es_objects_limit_orders = true;
      bool _es_objects_asset_bitasset = true;
      bool _es_objects_logs = true;
      CURL *curl; // curl handler
      vector <std::string> bulk;
      vector<std::string> prepare;
      map<object_id_type, std::string> bitassets;
      //uint32_t bitasset_seq;
   private:
      void PrepareProposal(const proposal_object* proposal_object, const fc::time_point_sec block_time, uint32_t block_number);
      void PrepareAccount(const account_object* account_object, const fc::time_point_sec block_time, uint32_t block_number);
      void PrepareAsset(const asset_object* asset_object, const fc::time_point_sec block_time, uint32_t block_number);
      void PrepareBalance(const balance_object* balance_object, const fc::time_point_sec block_time, uint32_t block_number);
      void PrepareLimit(const limit_order_object* limit_object, const fc::time_point_sec block_time, uint32_t block_number);
      void PrepareBitAsset(const asset_bitasset_data_object* bitasset_object, const fc::time_point_sec block_time, uint32_t block_number);
};

void es_objects_plugin_impl::updateDatabase( const vector<object_id_type>& ids , bool isNew)
{

   graphene::chain::database &db = _self.database();

   const fc::time_point_sec block_time = db.head_block_time();
   const uint32_t block_number = db.head_block_num();

   // check if we are in replay or in sync and change number of bulk documents accordingly
   uint32_t limit_documents = 0;
   if((fc::time_point::now() - block_time) < fc::seconds(30))
      limit_documents = _es_objects_bulk_sync;
   else
      limit_documents = _es_objects_bulk_replay;

   if (curl && bulk.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech
      if(!graphene::utilities::SendBulk(curl, bulk, _es_objects_elasticsearch_url, _es_objects_logs, "objects_logs"))
         elog("Error sending data to database");
      bulk.clear();
   }

   for(auto const& value: ids) {
      if(value.is<proposal_object>() && _es_objects_proposals) {
         auto obj = db.find_object(value);
         auto p = static_cast<const proposal_object*>(obj);
         if(p != nullptr)
            PrepareProposal(p, block_time, block_number);
      }
      else if(value.is<account_object>() && _es_objects_accounts) {
         auto obj = db.find_object(value);
         auto a = static_cast<const account_object*>(obj);
         if(a != nullptr)
            PrepareAccount(a, block_time, block_number);
      }
      else if(value.is<asset_object>() && _es_objects_assets) {
         auto obj = db.find_object(value);
         auto a = static_cast<const asset_object*>(obj);
         if(a != nullptr)
            PrepareAsset(a, block_time, block_number);
      }
      else if(value.is<balance_object>() && _es_objects_balances) {
         auto obj = db.find_object(value);
         auto b = static_cast<const balance_object*>(obj);
         if(b != nullptr)
            PrepareBalance(b, block_time, block_number);
      }
      else if(value.is<limit_order_object>() && _es_objects_limit_orders) {
         auto obj = db.find_object(value);
         auto l = static_cast<const limit_order_object*>(obj);
         if(l != nullptr)
            PrepareLimit(l, block_time, block_number);
      }
      else if(value.is<asset_bitasset_data_object>() && _es_objects_asset_bitasset) {
         auto obj = db.find_object(value);
         auto ba = static_cast<const asset_bitasset_data_object*>(obj);
         if(ba != nullptr)
            PrepareBitAsset(ba, block_time, block_number);
      }
   }
}

void es_objects_plugin_impl::PrepareProposal(const proposal_object* proposal_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   proposal_struct prop;
   prop.object_id = proposal_object->id;
   prop.block_time = block_time;
   prop.block_number = block_number;
   prop.expiration_time = proposal_object->expiration_time;
   prop.review_period_time = proposal_object->review_period_time;
   prop.proposed_transaction = fc::json::to_string(proposal_object->proposed_transaction);
   prop.required_owner_approvals = fc::json::to_string(proposal_object->required_owner_approvals);
   prop.available_owner_approvals = fc::json::to_string(proposal_object->available_owner_approvals);
   prop.required_active_approvals = fc::json::to_string(proposal_object->required_active_approvals);
   prop.available_key_approvals = fc::json::to_string(proposal_object->available_key_approvals);
   prop.proposer = proposal_object->proposer;

   std::string data = fc::json::to_string(prop);
   prepare = graphene::utilities::createBulk("bitshares-proposal", data, "", 1);
   bulk.insert(bulk.end(), prepare.begin(), prepare.end());
   prepare.clear();
}

void es_objects_plugin_impl::PrepareAccount(const account_object* account_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   account_struct acct;
   acct.object_id = account_object->id;
   acct.block_time = block_time;
   acct.block_number = block_number;
   acct.membership_expiration_date = account_object->membership_expiration_date;
   acct.registrar = account_object->registrar;
   acct.referrer = account_object->referrer;
   acct.lifetime_referrer = account_object->lifetime_referrer;
   acct.network_fee_percentage = account_object->network_fee_percentage;
   acct.lifetime_referrer_fee_percentage = account_object->lifetime_referrer_fee_percentage;
   acct.referrer_rewards_percentage = account_object->referrer_rewards_percentage;
   acct.name = account_object->name;
   acct.owner_account_auths = fc::json::to_string(account_object->owner.account_auths);
   acct.owner_key_auths = fc::json::to_string(account_object->owner.key_auths);
   acct.owner_address_auths = fc::json::to_string(account_object->owner.address_auths);
   acct.active_account_auths = fc::json::to_string(account_object->active.account_auths);
   acct.active_key_auths = fc::json::to_string(account_object->active.key_auths);
   acct.active_address_auths = fc::json::to_string(account_object->active.address_auths);
   acct.voting_account = account_object->options.voting_account;

   std::string data = fc::json::to_string(acct);
   prepare = graphene::utilities::createBulk("bitshares-account", data, "", 1);
   bulk.insert(bulk.end(), prepare.begin(), prepare.end());
   prepare.clear();
}

void es_objects_plugin_impl::PrepareAsset(const asset_object* asset_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   asset_struct _asset;
   _asset.object_id = asset_object->id;
   _asset.block_time = block_time;
   _asset.block_number = block_number;
   _asset.symbol = asset_object->symbol;
   _asset.issuer = asset_object->issuer;
   _asset.is_market_issued = asset_object->is_market_issued();
   _asset.dynamic_asset_data_id = asset_object->dynamic_asset_data_id;
   _asset.bitasset_data_id = asset_object->bitasset_data_id;

   std::string data = fc::json::to_string(_asset);
   prepare = graphene::utilities::createBulk("bitshares-asset", data, fc::json::to_string(asset_object->id), 0);
   bulk.insert(bulk.end(), prepare.begin(), prepare.end());
   prepare.clear();
}

void es_objects_plugin_impl::PrepareBalance(const balance_object* balance_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   balance_struct balance;
   balance.object_id = balance_object->id;
   balance.block_time = block_time;
   balance.block_number = block_number;balance.owner = balance_object->owner;
   balance.asset_id = balance_object->balance.asset_id;
   balance.amount = balance_object->balance.amount;

   std::string data = fc::json::to_string(balance);
   prepare = graphene::utilities::createBulk("bitshares-balance", data, "", 1);
   bulk.insert(bulk.end(), prepare.begin(), prepare.end());
   prepare.clear();
}

void es_objects_plugin_impl::PrepareLimit(const limit_order_object* limit_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   limit_order_struct limit;
   limit.object_id = limit_object->id;
   limit.block_time = block_time;
   limit.block_number = block_number;
   limit.expiration = limit_object->expiration;
   limit.seller = limit_object->seller;
   limit.for_sale = limit_object->for_sale;
   limit.sell_price = limit_object->sell_price;
   limit.deferred_fee = limit_object->deferred_fee;

   std::string data = fc::json::to_string(limit);
   prepare = graphene::utilities::createBulk("bitshares-limitorder", data, "", 1);
   bulk.insert(bulk.end(), prepare.begin(), prepare.end());
   prepare.clear();
}

void es_objects_plugin_impl::PrepareBitAsset(const asset_bitasset_data_object* bitasset_object, const fc::time_point_sec block_time, uint32_t block_number)
{
   if(!bitasset_object->is_prediction_market) {

      auto object_id = bitasset_object->id;
      auto it = bitassets.find(object_id);
      if(it == bitassets.end())
         bitassets[object_id] = fc::json::to_string(bitasset_object->current_feed);
      else {
         if(it->second == fc::json::to_string(bitasset_object->current_feed)) return;
         else bitassets[object_id] = fc::json::to_string(bitasset_object->current_feed);
      }

      bitasset_struct bitasset;

      bitasset.object_id = bitasset_object->id;
      bitasset.block_time = block_time;
      bitasset.block_number = block_number;
      bitasset.current_feed = fc::json::to_string(bitasset_object->current_feed);
      bitasset.current_feed_publication_time = bitasset_object->current_feed_publication_time;

      std::string data = fc::json::to_string(bitasset);
      prepare = graphene::utilities::createBulk("bitshares-bitasset", data, "", 1);
      bulk.insert(bulk.end(), prepare.begin(), prepare.end());
      prepare.clear();
   }
}

es_objects_plugin_impl::~es_objects_plugin_impl()
{
   return;
}


} // end namespace detail

es_objects_plugin::es_objects_plugin() :
   my( new detail::es_objects_plugin_impl(*this) )
{
}

es_objects_plugin::~es_objects_plugin()
{
}

std::string es_objects_plugin::plugin_name()const
{
   return "es_objects";
}
std::string es_objects_plugin::plugin_description()const
{
   return "Stores blockchain objects in ES database. Experimental.";
}

void es_objects_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("es-objects-elasticsearch-url", boost::program_options::value<std::string>(), "Elasticsearch node url")
         ("es-objects-logs", boost::program_options::value<bool>(), "Log bulk events to database")
         ("es-objects-bulk-replay", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on replay(5000)")
         ("es-objects-bulk-sync", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on a syncronied chain(10)")
         ("es-objects-proposals", boost::program_options::value<bool>(), "Store proposal objects")
         ("es-objects-accounts", boost::program_options::value<bool>(), "Store account objects")
         ("es-objects-assets", boost::program_options::value<bool>(), "Store asset objects")
         ("es-objects-balances", boost::program_options::value<bool>(), "Store balances objects")
         ("es-objects-limit-orders", boost::program_options::value<bool>(), "Store limit order objects")
         ("es-objects-asset-bitasset", boost::program_options::value<bool>(), "Store feed data")

         ;
   cfg.add(cli);
}

void es_objects_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().new_objects.connect([&]( const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts ){ my->updateDatabase(ids, 1); });
   database().changed_objects.connect([&]( const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts ){ my->updateDatabase(ids, 0); });

   if (options.count("es-objects-elasticsearch-url")) {
      my->_es_objects_elasticsearch_url = options["es-objects-elasticsearch-url"].as<std::string>();
   }
   if (options.count("es-objects-logs")) {
      my->_es_objects_logs = options["es-objects-logs"].as<bool>();
   }
   if (options.count("es-objects-bulk-replay")) {
      my->_es_objects_bulk_replay = options["es-objects-bulk-replay"].as<uint32_t>();
   }
   if (options.count("es-objects-bulk-sync")) {
      my->_es_objects_bulk_sync = options["es-objects-bulk-sync"].as<uint32_t>();
   }
   if (options.count("es-objects-proposals")) {
      my->_es_objects_proposals = options["es-objects-proposals"].as<bool>();
   }
   if (options.count("es-objects-accounts")) {
      my->_es_objects_accounts = options["es-objects-accounts"].as<bool>();
   }
   if (options.count("es-objects-assets")) {
      my->_es_objects_assets = options["es-objects-assets"].as<bool>();
   }
   if (options.count("es-objects-balances")) {
      my->_es_objects_balances = options["es-objects-balances"].as<bool>();
   }
   if (options.count("es-objects-limit-orders")) {
      my->_es_objects_limit_orders = options["es-objects-limit-orders"].as<bool>();
   }
   if (options.count("es-objects-asset-bitasset")) {
      my->_es_objects_asset_bitasset = options["es-objects-asset-bitasset"].as<bool>();
   }
}

void es_objects_plugin::plugin_startup()
{
   ilog("elasticsearch objects: plugin_startup() begin");
}

} }