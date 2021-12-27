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

#include <curl/curl.h>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>

#include <graphene/utilities/elasticsearch.hpp>

namespace graphene { namespace es_objects {

namespace detail
{

class es_objects_plugin_impl
{
   public:
      explicit es_objects_plugin_impl(es_objects_plugin& _plugin)
         : _self( _plugin )
      {
         curl = curl_easy_init();
         curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
      }
      virtual ~es_objects_plugin_impl();

   private:
      friend class graphene::es_objects::es_objects_plugin;
      friend struct genesis_inserter;

      enum class action_type
      {
         insertion,
         update,
         deletion
      };

      void on_objects_create(const vector<object_id_type>& ids)
      { index_database( ids, action_type::insertion ); }

      void on_objects_update(const vector<object_id_type>& ids)
      { index_database( ids, action_type::update ); }

      void on_objects_delete(const vector<object_id_type>& ids)
      { index_database( ids, action_type::deletion ); }

      void index_database(const vector<object_id_type>& ids, action_type action);
      void genesis();
      void remove_from_database(const object_id_type& id, const std::string& index);

      struct plugin_options
      {
         std::string _es_objects_elasticsearch_url = "http://localhost:9200/";
         std::string _es_objects_auth = "";
         uint32_t _es_objects_bulk_replay = 10000;
         uint32_t _es_objects_bulk_sync = 100;
         bool _es_objects_proposals = true;
         bool _es_objects_accounts = true;
         bool _es_objects_assets = true;
         bool _es_objects_balances = true;
         bool _es_objects_limit_orders = false;
         bool _es_objects_asset_bitasset = true;
         std::string _es_objects_index_prefix = "objects-";
         uint32_t _es_objects_start_es_after_block = 0;
         bool _es_objects_keep_only_current = true;

         void init(const boost::program_options::variables_map& options);
      };

      es_objects_plugin& _self;
      plugin_options _options;

      uint32_t limit_documents = _options._es_objects_bulk_replay;

      CURL *curl; // curl handler
      vector<std::string> bulk;
      vector<std::string> prepare;


      uint32_t block_number;
      fc::time_point_sec block_time;
      bool is_es_version_7_or_above = true;

      template<typename T>
      void prepareTemplate(const T& blockchain_object, const string& index_name);

      void init_program_options(const boost::program_options::variables_map& options);

      void send_bulk_if_ready();
};

struct genesis_inserter
{
   es_objects_plugin_impl* my;
   graphene::chain::database &db;

   explicit genesis_inserter( es_objects_plugin_impl* _my )
   : my(_my), db( my->_self.database() )
   { // Nothing to do
   }

   template<typename ObjType>
   void insert( bool b, const string& prefix )
   {
      if( !b )
         return;

      db.get_index( ObjType::space_id, ObjType::type_id ).inspect_all_objects(
            [this, &prefix](const graphene::db::object &o) {
         my->prepareTemplate( static_cast<const ObjType&>(o), prefix);
      });
   }
};

void es_objects_plugin_impl::genesis()
{
   ilog("elasticsearch OBJECTS: inserting data from genesis");

   graphene::chain::database &db = _self.database();

   block_number = db.head_block_num();
   block_time = db.head_block_time();

   genesis_inserter inserter( this );

   inserter.insert<account_object             >( _options._es_objects_accounts,       "account"  );
   inserter.insert<asset_object               >( _options._es_objects_assets,         "asset"    );
   inserter.insert<asset_bitasset_data_object >( _options._es_objects_asset_bitasset, "bitasset" );
   inserter.insert<account_balance_object     >( _options._es_objects_balances,       "balance"  );
}

void es_objects_plugin_impl::index_database(const vector<object_id_type>& ids, action_type action)
{
   graphene::chain::database &db = _self.database();

   block_number = db.head_block_num();

   if( block_number <= _options._es_objects_start_es_after_block )
      return;

   block_time = db.head_block_time();

   // check if we are in replay or in sync and change number of bulk documents accordingly
   if( (fc::time_point::now() - block_time) < fc::seconds(30) )
      limit_documents = _options._es_objects_bulk_sync;
   else
      limit_documents = _options._es_objects_bulk_replay;

   for( auto const &value: ids )
   {
      if( value.is<account_balance_object>() && _options._es_objects_balances ) {
         if( action_type::deletion == action )
            remove_from_database( value, "balance" );
         else
            prepareTemplate( account_balance_id_type(value)(db), "balance" );
      } else if( value.is<limit_order_object>() && _options._es_objects_limit_orders ) {
         if( action_type::deletion == action )
            remove_from_database( value, "limitorder" );
         else
            prepareTemplate( limit_order_id_type(value)(db), "limitorder" );
      } else if( value.is<asset_bitasset_data_object>() && _options._es_objects_asset_bitasset) {
         if( action_type::deletion == action )
            remove_from_database( value, "bitasset" );
         else
            prepareTemplate( asset_bitasset_data_id_type(value)(db), "bitasset" );
      } else if( value.is<asset_object>() && _options._es_objects_assets ) {
         if( action_type::deletion == action )
            remove_from_database( value, "asset" );
         else
            prepareTemplate( asset_id_type(value)(db), "asset" );
      } else if( value.is<account_object>() && _options._es_objects_accounts ) {
         if( action_type::deletion == action )
            remove_from_database( value, "account" );
         else
            prepareTemplate( account_id_type(value)(db), "account" );
      } else if( value.is<proposal_object>() && _options._es_objects_proposals ) {
         if( action_type::deletion == action )
            remove_from_database( value, "proposal" );
         else
            prepareTemplate( proposal_id_type(value)(db), "proposal" );
      }
   }

}

void es_objects_plugin_impl::remove_from_database( const object_id_type& id, const std::string& index)
{
   if(_options._es_objects_keep_only_current)
   {
      fc::mutable_variant_object delete_line;
      delete_line["_id"] = string(id);
      delete_line["_index"] = _options._es_objects_index_prefix + index;
      if(!is_es_version_7_or_above)
         delete_line["_type"] = "_doc";
      fc::mutable_variant_object final_delete_line;
      final_delete_line["delete"] = delete_line;
      prepare.push_back(fc::json::to_string(final_delete_line));
      std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
      prepare.clear();

      send_bulk_if_ready();
   }
}

template<typename T>
void es_objects_plugin_impl::prepareTemplate(const T& blockchain_object, const string& index_name)
{
   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _options._es_objects_index_prefix + index_name;
   if(!is_es_version_7_or_above)
      bulk_header["_type"] = "_doc";
   if(_options._es_objects_keep_only_current)
   {
      bulk_header["_id"] = string(blockchain_object.id);
   }

   fc::variant blockchain_object_variant;
   fc::to_variant( blockchain_object, blockchain_object_variant, GRAPHENE_NET_MAX_NESTED_OBJECTS );
   fc::mutable_variant_object o( utilities::es_data_adaptor::adapt( blockchain_object_variant.get_object() ) );

   o["object_id"] = string(blockchain_object.id);
   o["block_time"] = block_time;
   o["block_number"] = block_number;

   string data = fc::json::to_string(o, fc::json::legacy_generator);

   prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk));
   prepare.clear();

   send_bulk_if_ready();
}

void es_objects_plugin_impl::send_bulk_if_ready()
{
   if( curl && bulk.size() >= limit_documents ) // send data to elasticsearch when bulk is too large
   {
      graphene::utilities::ES es;
      es.curl = curl;
      es.bulk_lines = bulk;
      es.elasticsearch_url = _options._es_objects_elasticsearch_url;
      es.auth = _options._es_objects_auth;
      if (!graphene::utilities::SendBulk(std::move(es)))
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Error sending bulk data.");
      else
         bulk.clear();
   }
}

es_objects_plugin_impl::~es_objects_plugin_impl()
{
   if (curl) {
      curl_easy_cleanup(curl);
      curl = nullptr;
   }
}

} // end namespace detail

es_objects_plugin::es_objects_plugin(graphene::app::application& app) :
   plugin(app),
   my( std::make_unique<detail::es_objects_plugin_impl>(*this) )
{
   // Nothing else to do
}

es_objects_plugin::~es_objects_plugin() = default;

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
         ("es-objects-elasticsearch-url", boost::program_options::value<std::string>(),
               "Elasticsearch node url(http://localhost:9200/)")
         ("es-objects-auth", boost::program_options::value<std::string>(), "Basic auth username:password('')")
         ("es-objects-bulk-replay", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on replay(10000)")
         ("es-objects-bulk-sync", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on a synchronized chain(100)")
         ("es-objects-proposals", boost::program_options::value<bool>(), "Store proposal objects(true)")
         ("es-objects-accounts", boost::program_options::value<bool>(), "Store account objects(true)")
         ("es-objects-assets", boost::program_options::value<bool>(), "Store asset objects(true)")
         ("es-objects-balances", boost::program_options::value<bool>(), "Store balances objects(true)")
         ("es-objects-limit-orders", boost::program_options::value<bool>(), "Store limit order objects(false)")
         ("es-objects-asset-bitasset", boost::program_options::value<bool>(), "Store feed data(true)")
         ("es-objects-index-prefix", boost::program_options::value<std::string>(),
               "Add a prefix to the index(objects-)")
         ("es-objects-keep-only-current", boost::program_options::value<bool>(),
               "Keep only current state of the objects(true)")
         ("es-objects-start-es-after-block", boost::program_options::value<uint32_t>(),
               "Start doing ES job after block(0)")
         ;
   cfg.add(cli);
}

void detail::es_objects_plugin_impl::init_program_options(const boost::program_options::variables_map& options)
{
   _options.init( options );
}

void detail::es_objects_plugin_impl::plugin_options::init(const boost::program_options::variables_map& options)
{
   if (options.count("es-objects-elasticsearch-url") > 0) {
      _es_objects_elasticsearch_url = options["es-objects-elasticsearch-url"].as<std::string>();
   }
   if (options.count("es-objects-auth") > 0) {
      _es_objects_auth = options["es-objects-auth"].as<std::string>();
   }
   if (options.count("es-objects-bulk-replay") > 0) {
      _es_objects_bulk_replay = options["es-objects-bulk-replay"].as<uint32_t>();
   }
   if (options.count("es-objects-bulk-sync") > 0) {
      _es_objects_bulk_sync = options["es-objects-bulk-sync"].as<uint32_t>();
   }
   if (options.count("es-objects-proposals") > 0) {
      _es_objects_proposals = options["es-objects-proposals"].as<bool>();
   }
   if (options.count("es-objects-accounts") > 0) {
      _es_objects_accounts = options["es-objects-accounts"].as<bool>();
   }
   if (options.count("es-objects-assets") > 0) {
      _es_objects_assets = options["es-objects-assets"].as<bool>();
   }
   if (options.count("es-objects-balances") > 0) {
      _es_objects_balances = options["es-objects-balances"].as<bool>();
   }
   if (options.count("es-objects-limit-orders") > 0) {
      _es_objects_limit_orders = options["es-objects-limit-orders"].as<bool>();
   }
   if (options.count("es-objects-asset-bitasset") > 0) {
      _es_objects_asset_bitasset = options["es-objects-asset-bitasset"].as<bool>();
   }
   if (options.count("es-objects-index-prefix") > 0) {
      _es_objects_index_prefix = options["es-objects-index-prefix"].as<std::string>();
   }
   if (options.count("es-objects-keep-only-current") > 0) {
      _es_objects_keep_only_current = options["es-objects-keep-only-current"].as<bool>();
   }
   if (options.count("es-objects-start-es-after-block") > 0) {
      _es_objects_start_es_after_block = options["es-objects-start-es-after-block"].as<uint32_t>();
   }
}

void es_objects_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->init_program_options( options );

   database().applied_block.connect([this](const signed_block &b) {
      if( 1U == b.block_num() && 0 == my->_options._es_objects_start_es_after_block ) {
         my->genesis();
      }
   });
   database().new_objects.connect([this]( const vector<object_id_type>& ids,
         const flat_set<account_id_type>& ) {
      my->on_objects_create( ids );
   });
   database().changed_objects.connect([this]( const vector<object_id_type>& ids,
         const flat_set<account_id_type>& ) {
      my->on_objects_update( ids );
   });
   database().removed_objects.connect([this](const vector<object_id_type>& ids,
         const vector<const object*>&, const flat_set<account_id_type>& ) {
      my->on_objects_delete( ids );
   });

   graphene::utilities::ES es;
   es.curl = my->curl;
   es.elasticsearch_url = my->_options._es_objects_elasticsearch_url;
   es.auth = my->_options._es_objects_auth;
   es.auth = my->_options._es_objects_index_prefix;

   if(!graphene::utilities::checkES(es))
      FC_THROW( "ES database is not up in url ${url}", ("url", my->_options._es_objects_elasticsearch_url) );

   graphene::utilities::checkESVersion7OrAbove(es, my->is_es_version_7_or_above);
}

void es_objects_plugin::plugin_startup()
{
   // Nothing to do
}

} }
