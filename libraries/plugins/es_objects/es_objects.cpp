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

#include <graphene/es_objects/es_objects.hpp>

#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>

#include <curl/curl.h>
#include <boost/algorithm/string/replace.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <boost/algorithm/string/join.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/market_object.hpp>


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

      graphene::chain::database& database()
      {
         return _self.database();
      }

      es_objects_plugin& _self;
      std::string _es_objects_elasticsearch_url = "http://localhost:9200/";
      uint32_t _es_objects_bulk_replay = 1000;
      uint32_t _es_objects_bulk_sync = 2;
      bool _es_objects_proposals = true;
      bool _es_objects_accounts = true;
      bool _es_objects_assets = true;
      bool _es_objects_balances = true;
      bool _es_objects_limit_orders = true;
      bool _es_objects_logs = true;
      CURL *curl; // curl handler
      vector <string> bulk;
   private:
      void PrepareProposal(const proposal_object* proposal_object);
      void PrepareAccount(const account_object* account_object);
      void PrepareAsset(const asset_object* asset_object);
      void PrepareBalance(const balance_object* balance_object);
      void PrepareLimit(const limit_order_object* limit_object);
      void SendBulk();
      void createBulk(std::string type, std::string data, std::string id);
};

void es_objects_plugin_impl::updateDatabase( const vector<object_id_type>& ids , bool isNew)
{

   graphene::chain::database &db = database();

   auto block_time = db.head_block_time();

   // check if we are in replay or in sync and change number of bulk documents accordingly
   uint32_t limit_documents = 0;
   if((fc::time_point::now() - block_time) < fc::seconds(30))
      limit_documents = _es_objects_bulk_sync;
   else
      limit_documents = _es_objects_bulk_replay;

   if (curl && bulk.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech
      SendBulk();
   }

   for(auto const& value: ids) {
      if(value.is<proposal_object>() && _es_objects_proposals) {
         auto obj = db.find_object(value);
         auto p = static_cast<const proposal_object*>(obj);
         if(p != nullptr)
            PrepareProposal(p);
      }
      else if(value.is<account_object>() && _es_objects_accounts) {
         auto obj = db.find_object(value);
         auto a = static_cast<const account_object*>(obj);
         if(a != nullptr)
            PrepareAccount(a);
      }
      else if(value.is<asset_object>() && _es_objects_assets) {
         auto obj = db.find_object(value);
         auto a = static_cast<const asset_object*>(obj);
         if(a != nullptr)
            PrepareAsset(a);
      }
      else if(value.is<balance_object>() && _es_objects_balances) {
         auto obj = db.find_object(value);
         auto b = static_cast<const balance_object*>(obj);
         if(b != nullptr)
            PrepareBalance(b);
      }
      else if(value.is<limit_order_object>() && _es_objects_limit_orders) {
         auto obj = db.find_object(value);
         auto l = static_cast<const limit_order_object*>(obj);
         if(l != nullptr)
            PrepareLimit(l);
      }
   }
}

void es_objects_plugin_impl::createBulk(std::string type, std::string data, std::string id)
{
   bulk.push_back("{ \"index\" : { \"_index\" : \"bitshares-"+type+"\", \"_type\" : \"data\", \"_id\" : "+id+" } }");
   bulk.push_back(data);
}

void es_objects_plugin_impl::SendBulk()
{
   // curl buffers to read
   std::string readBuffer;
   std::string readBuffer_logs;

   std::string bulking = "";

   bulking = boost::algorithm::join(bulk, "\n");
   bulking = bulking + "\n";
   bulk.clear();

   struct curl_slist *headers = NULL;
   curl_slist_append(headers, "Content-Type: application/json");
   std::string url = _es_objects_elasticsearch_url + "_bulk";
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_POST, true);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bulking.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&readBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   //curl_easy_setopt(curl, CURLOPT_VERBOSE, true);
   curl_easy_perform(curl);

   long http_code = 0;
   curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
   if(http_code == 200) {
      // all good, do nothing
   }
   else if(http_code == 429) {
      // repeat request?
   }
   else {
      // exit everything ?
   }
   if(_es_objects_logs) {
      auto logs = readBuffer;
      // do logs
      std::string url_logs = _es_objects_elasticsearch_url + "objects_logs/data/";
      curl_easy_setopt(curl, CURLOPT_URL, url_logs.c_str());
      curl_easy_setopt(curl, CURLOPT_POST, true);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, logs.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &readBuffer_logs);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
      //curl_easy_setopt(curl, CURLOPT_VERBOSE, true);
      //ilog("log here curl: ${output}", ("output", readBuffer_logs));
      curl_easy_perform(curl);

      http_code = 0;
      curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
      if(http_code == 200) {
         // all good, do nothing
      }
      else if(http_code == 429) {
         // repeat request?
      }
      else {
         // exit everything ?
      }
   }
}

void es_objects_plugin_impl::PrepareProposal(const proposal_object* proposal_object)
{
   proposal_struct prop;
   prop.id = proposal_object->id;
   prop.expiration_time = proposal_object->expiration_time;
   prop.review_period_time = proposal_object->review_period_time;
   prop.proposed_transaction = fc::json::to_string(proposal_object->proposed_transaction);
   prop.required_owner_approvals = fc::json::to_string(proposal_object->required_owner_approvals);
   prop.available_owner_approvals = fc::json::to_string(proposal_object->available_owner_approvals);
   prop.required_active_approvals = fc::json::to_string(proposal_object->required_active_approvals);
   prop.available_key_approvals = fc::json::to_string(proposal_object->available_key_approvals);

   std::string data = fc::json::to_string(prop);

   createBulk("proposal", data, fc::json::to_string(proposal_object->id));
}

void es_objects_plugin_impl::PrepareAccount(const account_object* account_object)
{
   account_struct acct;
   acct.id = account_object->id;
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

   createBulk("account", data, fc::json::to_string(account_object->id));
}

void es_objects_plugin_impl::PrepareAsset(const asset_object* asset_object)
{
   asset_struct asset;
   asset.id = asset_object->id;
   asset.symbol = asset_object->symbol;
   asset.issuer = asset_object->issuer;

   std::string data = fc::json::to_string(asset);

   createBulk("asset", data, fc::json::to_string(asset_object->id));
}

void es_objects_plugin_impl::PrepareBalance(const balance_object* balance_object)
{
   balance_struct balance;
   balance.owner = balance_object->owner;
   balance.asset_id = balance_object->balance.asset_id;
   balance.amount = balance_object->balance.amount;

   std::string data = fc::json::to_string(balance);
   createBulk("balance", data, fc::json::to_string(balance_object->owner));
}

void es_objects_plugin_impl::PrepareLimit(const limit_order_object* limit_object)
{
   limit_order_struct limit;
   limit.expiration = limit_object->expiration;
   limit.seller = limit_object->seller;
   limit.for_sale = limit_object->for_sale;
   limit.sell_price = limit_object->sell_price;
   limit.deferred_fee = limit_object->deferred_fee;

   std::string data = fc::json::to_string(limit);
   createBulk("limitorder", data, fc::json::to_string(limit_object->id));
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
   return "Stores blockchain objects in ES database.";
}

void es_objects_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("es-objects-elasticsearch-url", boost::program_options::value<std::string>(), "Elasticsearch node url")
         ("es-objects-logs", boost::program_options::value<bool>(), "Log bulk events to database")
         ("es-objects-bulk-replay", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on replay(1000)")
         ("es-objects-bulk-sync", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on a syncronied chain(2)")
         ("es-objects-proposals", boost::program_options::value<bool>(), "Store proposal objects")
         ("es-objects-accounts", boost::program_options::value<bool>(), "Store account objects")
         ("es-objects-assets", boost::program_options::value<bool>(), "Store asset objects")
         ("es-objects-balances", boost::program_options::value<bool>(), "Store balances objects")
         ("es-objects-limit-orders", boost::program_options::value<bool>(), "Store limit order objects")

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
}

void es_objects_plugin::plugin_startup()
{
}

} }