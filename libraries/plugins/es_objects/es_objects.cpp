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

      bool index_database( const vector<object_id_type>& ids, std::string action);
      void remove_from_database( object_id_type id, std::string index);

      es_objects_plugin& _self;
      std::string _es_objects_elasticsearch_url = "http://localhost:9200/";
      std::string _es_objects_auth = "";
      uint32_t _es_objects_bulk_replay = 10000;
      uint32_t _es_objects_bulk_sync = 100;
      bool _es_objects_proposals = true;
      bool _es_objects_accounts = true;
      bool _es_objects_assets = true;
      bool _es_objects_balances = true;
      bool _es_objects_limit_orders = true;
      bool _es_objects_asset_bitasset = true;
      std::string _es_objects_index_prefix = "objects-";
      CURL *curl; // curl handler
      vector <std::string> bulk;
      vector<std::string> prepare;

      bool _es_objects_keep_only_current = true;

      uint32_t block_number;
      fc::time_point_sec block_time;

   private:
      void prepare_proposal(const proposal_object& proposal_object);
      void prepare_account(const account_object& account_object);
      void prepare_asset(const asset_object& asset_object);
      void prepare_balance(const account_balance_object& account_balance_object);
      void prepare_limit(const limit_order_object& limit_object);
      void prepare_bitasset(const asset_bitasset_data_object& bitasset_object);
};

bool es_objects_plugin_impl::index_database( const vector<object_id_type>& ids, std::string action)
{
   graphene::chain::database &db = _self.database();

   block_time = db.head_block_time();
   block_number = db.head_block_num();

   // check if we are in replay or in sync and change number of bulk documents accordingly
   uint32_t limit_documents = 0;
   if((fc::time_point::now() - block_time) < fc::seconds(30))
      limit_documents = _es_objects_bulk_sync;
   else
      limit_documents = _es_objects_bulk_replay;

   for(auto const& value: ids) {
      if(value.is<proposal_object>() && _es_objects_proposals) {
         auto obj = db.find_object(value);
         auto p = static_cast<const proposal_object*>(obj);
         if(p != nullptr) {
            if(action == "delete")
               remove_from_database(p->id, "proposal");
            else
               prepare_proposal(*p);
         }
      }
      else if(value.is<account_object>() && _es_objects_accounts) {
         auto obj = db.find_object(value);
         auto a = static_cast<const account_object*>(obj);
         if(a != nullptr) {
            if(action == "delete")
               remove_from_database(a->id, "account");
            else
               prepare_account(*a);
         }
      }
      else if(value.is<asset_object>() && _es_objects_assets) {
         auto obj = db.find_object(value);
         auto a = static_cast<const asset_object*>(obj);
         if(a != nullptr) {
            if(action == "delete")
               remove_from_database(a->id, "asset");
            else
               prepare_asset(*a);
         }
      }
      else if(value.is<account_balance_object>() && _es_objects_balances) {
         auto obj = db.find_object(value);
         auto b = static_cast<const account_balance_object*>(obj);
         if(b != nullptr) {
            if(action == "delete")
               remove_from_database(b->id, "balance");
            else
               prepare_balance(*b);
         }
      }
      else if(value.is<limit_order_object>() && _es_objects_limit_orders) {
         auto obj = db.find_object(value);
         auto l = static_cast<const limit_order_object*>(obj);
         if(l != nullptr) {
            if(action == "delete")
               remove_from_database(l->id, "limitorder");
            else
               prepare_limit(*l);
         }
      }
      else if(value.is<asset_bitasset_data_object>() && _es_objects_asset_bitasset) {
         auto obj = db.find_object(value);
         auto ba = static_cast<const asset_bitasset_data_object*>(obj);
         if(ba != nullptr) {
            if(action == "delete")
               remove_from_database(ba->id, "bitasset");
            else
               prepare_bitasset(*ba);
         }
      }
   }

   if (curl && bulk.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech

      graphene::utilities::ES es;
      es.curl = curl;
      es.bulk_lines = bulk;
      es.elasticsearch_url = _es_objects_elasticsearch_url;
      es.auth = _es_objects_auth;

      if(!graphene::utilities::SendBulk(std::move(es)))
         return false;
      else
         bulk.clear();
   }

   return true;
}

void es_objects_plugin_impl::remove_from_database( object_id_type id, std::string index)
{
   if(_es_objects_keep_only_current)
   {
      fc::mutable_variant_object delete_line;
      delete_line["_id"] = string(id);
      delete_line["_index"] = _es_objects_index_prefix + index;
      delete_line["_type"] = "data";
      fc::mutable_variant_object final_delete_line;
      final_delete_line["delete"] = delete_line;
      prepare.push_back(fc::json::to_string(final_delete_line));
      std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
      prepare.clear();
   }
}

void es_objects_plugin_impl::prepare_proposal(const proposal_object& proposal_object)
{
   proposal_struct prop;
   prop.object_id = proposal_object.id;
   prop.block_time = block_time;
   prop.block_number = block_number;
   prop.expiration_time = proposal_object.expiration_time;
   prop.review_period_time = proposal_object.review_period_time;
   prop.proposed_transaction = fc::json::to_string(proposal_object.proposed_transaction);
   prop.required_owner_approvals = fc::json::to_string(proposal_object.required_owner_approvals);
   prop.available_owner_approvals = fc::json::to_string(proposal_object.available_owner_approvals);
   prop.required_active_approvals = fc::json::to_string(proposal_object.required_active_approvals);
   prop.available_key_approvals = fc::json::to_string(proposal_object.available_key_approvals);
   prop.proposer = proposal_object.proposer;

   std::string data = fc::json::to_string(prop);

   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _es_objects_index_prefix + "proposal";
   bulk_header["_type"] = "data";
   if(_es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(prop.object_id);
   }

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();
}

void es_objects_plugin_impl::prepare_account(const account_object& account_object)
{
   account_struct acct;
   acct.object_id = account_object.id;
   acct.block_time = block_time;
   acct.block_number = block_number;
   acct.membership_expiration_date = account_object.membership_expiration_date;
   acct.registrar = account_object.registrar;
   acct.referrer = account_object.referrer;
   acct.lifetime_referrer = account_object.lifetime_referrer;
   acct.network_fee_percentage = account_object.network_fee_percentage;
   acct.lifetime_referrer_fee_percentage = account_object.lifetime_referrer_fee_percentage;
   acct.referrer_rewards_percentage = account_object.referrer_rewards_percentage;
   acct.name = account_object.name;
   acct.owner_account_auths = fc::json::to_string(account_object.owner.account_auths);
   acct.owner_key_auths = fc::json::to_string(account_object.owner.key_auths);
   acct.owner_address_auths = fc::json::to_string(account_object.owner.address_auths);
   acct.active_account_auths = fc::json::to_string(account_object.active.account_auths);
   acct.active_key_auths = fc::json::to_string(account_object.active.key_auths);
   acct.active_address_auths = fc::json::to_string(account_object.active.address_auths);
   acct.voting_account = account_object.options.voting_account;
   acct.votes = fc::json::to_string(account_object.options.votes);

   std::string data = fc::json::to_string(acct);

   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _es_objects_index_prefix + "account";
   bulk_header["_type"] = "data";
   if(_es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(acct.object_id);
   }

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();
}

void es_objects_plugin_impl::prepare_asset(const asset_object& asset_object)
{
   asset_struct asset;
   asset.object_id = asset_object.id;
   asset.block_time = block_time;
   asset.block_number = block_number;
   asset.symbol = asset_object.symbol;
   asset.issuer = asset_object.issuer;
   asset.is_market_issued = asset_object.is_market_issued();
   asset.dynamic_asset_data_id = asset_object.dynamic_asset_data_id;
   asset.bitasset_data_id = asset_object.bitasset_data_id;

   std::string data = fc::json::to_string(asset);

   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _es_objects_index_prefix + "asset";
   bulk_header["_type"] = "data";
   if(_es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(asset.object_id);
   }

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();
}

void es_objects_plugin_impl::prepare_balance(const account_balance_object& account_balance_object)
{
   balance_struct balance;
   balance.object_id = account_balance_object.id;
   balance.block_time = block_time;
   balance.block_number = block_number;
   balance.owner = account_balance_object.owner;
   balance.asset_type = account_balance_object.asset_type;
   balance.balance = account_balance_object.balance;
   balance.maintenance_flag = account_balance_object.maintenance_flag;

   std::string data = fc::json::to_string(balance);

   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _es_objects_index_prefix + "balance";
   bulk_header["_type"] = "data";
   if(_es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(balance.object_id);
   }

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();
}

void es_objects_plugin_impl::prepare_limit(const limit_order_object& limit_object)
{
   limit_order_struct limit;
   limit.object_id = limit_object.id;
   limit.block_time = block_time;
   limit.block_number = block_number;
   limit.expiration = limit_object.expiration;
   limit.seller = limit_object.seller;
   limit.for_sale = limit_object.for_sale;
   limit.sell_price = limit_object.sell_price;
   limit.deferred_fee = limit_object.deferred_fee;

   std::string data = fc::json::to_string(limit);

   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _es_objects_index_prefix + "limitorder";
   bulk_header["_type"] = "data";
   if(_es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(limit.object_id);
   }

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();
}

void es_objects_plugin_impl::prepare_bitasset(const asset_bitasset_data_object& bitasset_object)
{
   if(!bitasset_object.is_prediction_market) {

      bitasset_struct bitasset;
      bitasset.object_id = bitasset_object.id;
      bitasset.block_time = block_time;
      bitasset.block_number = block_number;
      bitasset.current_feed = fc::json::to_string(bitasset_object.current_feed);
      bitasset.current_feed_publication_time = bitasset_object.current_feed_publication_time;

      std::string data = fc::json::to_string(bitasset);

      fc::mutable_variant_object bulk_header;
      bulk_header["_index"] = _es_objects_index_prefix + "bitasset";
      bulk_header["_type"] = "data";
      if(_es_objects_keep_only_current)
      {
         bulk_header["_id"] = string(bitasset.object_id);
      }

      prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
      std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
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
         ("es-objects-elasticsearch-url", boost::program_options::value<std::string>(), "Elasticsearch node url(http://localhost:9200/)")
         ("es-objects-auth", boost::program_options::value<std::string>(), "Basic auth username:password('')")
         ("es-objects-bulk-replay", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on replay(10000)")
         ("es-objects-bulk-sync", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on a synchronized chain(100)")
         ("es-objects-proposals", boost::program_options::value<bool>(), "Store proposal objects(true)")
         ("es-objects-accounts", boost::program_options::value<bool>(), "Store account objects(true)")
         ("es-objects-assets", boost::program_options::value<bool>(), "Store asset objects(true)")
         ("es-objects-balances", boost::program_options::value<bool>(), "Store balances objects(true)")
         ("es-objects-limit-orders", boost::program_options::value<bool>(), "Store limit order objects(true)")
         ("es-objects-asset-bitasset", boost::program_options::value<bool>(), "Store feed data(true)")
         ("es-objects-index-prefix", boost::program_options::value<std::string>(), "Add a prefix to the index(objects-)")
         ("es-objects-keep-only-current", boost::program_options::value<bool>(), "Keep only current state of the objects(true)")
         ;
   cfg.add(cli);
}

void es_objects_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().new_objects.connect([&]( const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts ) {
      if(!my->index_database(ids, "create"))
      {
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Error creating object from ES database, we are going to keep trying.");
      }
   });
   database().changed_objects.connect([&]( const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts ) {
      if(!my->index_database(ids, "update"))
      {
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Error updating object from ES database, we are going to keep trying.");
      }
   });
   database().removed_objects.connect([this](const vector<object_id_type>& ids, const vector<const object*>& objs, const flat_set<account_id_type>& impacted_accounts) {
       if(!my->index_database(ids, "delete"))
       {
          FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Error deleting object from ES database, we are going to keep trying.");
       }
   });


   if (options.count("es-objects-elasticsearch-url")) {
      my->_es_objects_elasticsearch_url = options["es-objects-elasticsearch-url"].as<std::string>();
   }
   if (options.count("es-objects-auth")) {
      my->_es_objects_auth = options["es-objects-auth"].as<std::string>();
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
   if (options.count("es-objects-index-prefix")) {
      my->_es_objects_index_prefix = options["es-objects-index-prefix"].as<std::string>();
   }
   if (options.count("es-objects-keep-only-current")) {
      my->_es_objects_keep_only_current = options["es-objects-keep-only-current"].as<bool>();
   }
}

void es_objects_plugin::plugin_startup()
{
   graphene::utilities::ES es;
   es.curl = my->curl;
   es.elasticsearch_url = my->_es_objects_elasticsearch_url;
   es.auth = my->_es_objects_auth;
   es.auth = my->_es_objects_index_prefix;

   if(!graphene::utilities::checkES(es))
      FC_THROW_EXCEPTION(fc::exception, "ES database is not up in url ${url}", ("url", my->_es_objects_elasticsearch_url));
   ilog("elasticsearch OBJECTS: plugin_startup() begin");
}

} }