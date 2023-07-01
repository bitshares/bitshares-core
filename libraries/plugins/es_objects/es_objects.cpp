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

#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/budget_record_object.hpp>

#include <graphene/utilities/elasticsearch.hpp>
#include <graphene/utilities/boost_program_options.hpp>

namespace graphene { namespace db {
   template<uint8_t SpaceID, uint8_t TypeID>
   constexpr uint16_t object_id<SpaceID, TypeID>::space_type;
} };

namespace graphene { namespace es_objects {

namespace detail
{

class es_objects_plugin_impl
{
   public:
      explicit es_objects_plugin_impl(es_objects_plugin& _plugin)
         : _self( _plugin )
      { }

   private:
      friend class graphene::es_objects::es_objects_plugin;
      friend struct data_loader;

      struct plugin_options
      {
         struct object_options
         {
            object_options( bool e, bool su, bool nd, const string& in )
            : enabled(e), store_updates(su), no_delete(nd), index_name(in)
            {}

            bool enabled = true;
            bool store_updates = false;
            bool no_delete = false;
            string index_name = "";
         };
         std::string elasticsearch_url = "http://localhost:9200/";
         std::string auth = "";
         uint32_t bulk_replay = 10000;
         uint32_t bulk_sync = 100;

         object_options proposals      { true, false, true,  "proposal"   };
         object_options accounts       { true, false, true,  "account"    };
         object_options assets         { true, false, true,  "asset"      };
         object_options balances       { true, false, true,  "balance"    };
         object_options limit_orders   { true, false, false, "limitorder" };
         object_options asset_bitasset { true, false, true,  "bitasset"   };
         object_options budget         { true, false, true,  "budget"     };

         std::string index_prefix = "objects-";

         /// For the "index.mapping.depth.limit" setting in ES whose default value is 20,
         /// and need to be even smaller to not trigger the index.mapping.total_fields.limit error
         uint16_t max_mapping_depth = 10;

         uint32_t start_es_after_block = 0;
         bool sync_db_on_startup = false;

         void init(const boost::program_options::variables_map& options);
      };

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
      /// Load all data from the object database into ES
      void sync_db( bool delete_before_load = false );
      /// Delete one object from ES
      void delete_from_database( const object_id_type& id, const plugin_options::object_options& opt );
      /// Delete all objects of the specified type from ES
      void delete_all_from_database( const plugin_options::object_options& opt ) const;

      es_objects_plugin& _self;
      plugin_options _options;

      uint32_t limit_documents = _options.bulk_replay;

      uint64_t docs_sent_batch = 0;
      uint64_t docs_sent_total = 0;

      std::unique_ptr<graphene::utilities::es_client> es;

      vector<std::string> bulk_lines;
      size_t approximate_bulk_size = 0;

      uint32_t block_number = 0;
      fc::time_point_sec block_time;
      bool is_es_version_7_or_above = true;

      template<typename T>
      void prepareTemplate( const T& blockchain_object, const plugin_options::object_options& opt );

      void init_program_options(const boost::program_options::variables_map& options);

      void send_bulk_if_ready( bool force = false );
};

struct data_loader
{
   es_objects_plugin_impl* my;
   graphene::chain::database &db;

   explicit data_loader( es_objects_plugin_impl* _my )
   : my(_my), db( my->_self.database() )
   { // Nothing to do
   }

   template<typename ObjType>
   void load( const es_objects_plugin_impl::plugin_options::object_options& opt,
              bool force_delete = false )
   {
      if( !opt.enabled )
         return;

      // If no_delete or store_updates is true, do not delete
      if( force_delete || !( opt.no_delete || opt.store_updates ) )
      {
         ilog( "Deleting all data in index " + my->_options.index_prefix + opt.index_name );
         my->delete_all_from_database( opt );
      }

      ilog( "Loading data into index " + my->_options.index_prefix + opt.index_name );
      db.get_index( ObjType::space_id, ObjType::type_id ).inspect_all_objects(
            [this, &opt](const graphene::db::object &o) {
         my->prepareTemplate( static_cast<const ObjType&>(o), opt );
      });
      my->send_bulk_if_ready(true);
      my->docs_sent_batch = 0;
   }
};

void es_objects_plugin_impl::sync_db( bool delete_before_load )
{
   ilog("elasticsearch OBJECTS: loading data from the object database (chain state)");

   graphene::chain::database &db = _self.database();

   block_number = db.head_block_num();
   block_time = db.head_block_time();

   data_loader loader( this );

   loader.load<account_object             >( _options.accounts,       delete_before_load );
   loader.load<asset_object               >( _options.assets,         delete_before_load );
   loader.load<asset_bitasset_data_object >( _options.asset_bitasset, delete_before_load );
   loader.load<account_balance_object     >( _options.balances,       delete_before_load );
   loader.load<proposal_object            >( _options.proposals,      delete_before_load );
   loader.load<limit_order_object         >( _options.limit_orders,   delete_before_load );
   loader.load<budget_record_object       >( _options.budget,         delete_before_load );

   ilog("elasticsearch OBJECTS: done loading data from the object database (chain state)");
}

void es_objects_plugin_impl::index_database(const vector<object_id_type>& ids, action_type action)
{
   graphene::chain::database &db = _self.database();

   block_number = db.head_block_num();

   if( block_number <= _options.start_es_after_block )
      return;

   block_time = db.head_block_time();

   // check if we are in replay or in sync and change number of bulk documents accordingly
   if( (fc::time_point::now() - block_time) < fc::seconds(30) )
      limit_documents = _options.bulk_sync;
   else
      limit_documents = _options.bulk_replay;

   bulk_lines.reserve(limit_documents);

   static const unordered_map<uint16_t,plugin_options::object_options&> data_type_map = {
      { account_id_type::space_type,             _options.accounts       },
      { account_balance_id_type::space_type,     _options.balances       },
      { asset_id_type::space_type,               _options.assets         },
      { asset_bitasset_data_id_type::space_type, _options.asset_bitasset },
      { limit_order_id_type::space_type,         _options.limit_orders   },
      { proposal_id_type::space_type,            _options.proposals      },
      { budget_record_id_type::space_type,       _options.budget         }
   };

   for( const auto& value: ids )
   {
      const auto itr = data_type_map.find( value.space_type() );
      if( itr == data_type_map.end() || !(itr->second.enabled) )
         continue;
      const auto& opt = itr->second;
      if( action_type::deletion == action )
         delete_from_database( value, opt );
      else
      {
         switch( itr->first )
         {
         case account_id_type::space_type:
            prepareTemplate( db.get<account_object>(value), opt );
            break;
         case account_balance_id_type::space_type:
            prepareTemplate( db.get<account_balance_object>(value), opt );
            break;
         case asset_id_type::space_type:
            prepareTemplate( db.get<asset_object>(value), opt );
            break;
         case asset_bitasset_data_id_type::space_type:
            prepareTemplate( db.get<asset_bitasset_data_object>(value), opt );
            break;
         case limit_order_id_type::space_type:
            prepareTemplate( db.get<limit_order_object>(value), opt );
            break;
         case proposal_id_type::space_type:
            prepareTemplate( db.get<proposal_object>(value), opt );
            break;
         case budget_record_id_type::space_type:
            prepareTemplate( db.get<budget_record_object>(value), opt );
            break;
         default:
            break;
         }
      }
   }

}

void es_objects_plugin_impl::delete_from_database(
      const object_id_type& id, const es_objects_plugin_impl::plugin_options::object_options& opt )
{
   if( opt.no_delete )
      return;

   fc::mutable_variant_object delete_line;
   delete_line["_id"] = string(id); // Note: this does not work if `store_updates` is true
   delete_line["_index"] = _options.index_prefix + opt.index_name;
   if( !is_es_version_7_or_above )
      delete_line["_type"] = "_doc";
   fc::mutable_variant_object final_delete_line;
   final_delete_line["delete"] = std::move( delete_line );

   bulk_lines.push_back( fc::json::to_string(final_delete_line) );

   approximate_bulk_size += bulk_lines.back().size();

   send_bulk_if_ready();
}

void es_objects_plugin_impl::delete_all_from_database( const plugin_options::object_options& opt ) const
{
   // Note:
   // 1. The _delete_by_query API deletes the data but keeps the index mapping, so the function is OK.
   //    Simply deleting the index is probably faster, but it requires the "delete_index" permission, and
   //    may probably mess up the index mapping and other existing settings.
   //    Don't know if there is a good way to only delete objects that do not exist in the object database.
   // 2. We don't check the return value here, it's probably OK
   es->query( _options.index_prefix + opt.index_name + "/_delete_by_query", R"({"query":{"match_all":{}}})" );
}

template<typename T>
void es_objects_plugin_impl::prepareTemplate(
      const T& blockchain_object, const es_objects_plugin_impl::plugin_options::object_options& opt )
{
   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = _options.index_prefix + opt.index_name;
   if( !is_es_version_7_or_above )
      bulk_header["_type"] = "_doc";
   if( !opt.store_updates )
   {
      bulk_header["_id"] = string(blockchain_object.id);
   }

   fc::variant blockchain_object_variant;
   fc::to_variant( blockchain_object, blockchain_object_variant, GRAPHENE_NET_MAX_NESTED_OBJECTS );
   fc::mutable_variant_object o( utilities::es_data_adaptor::adapt( blockchain_object_variant.get_object(),
                                                                    _options.max_mapping_depth ) );

   o["object_id"] = string(blockchain_object.id);
   o["block_time"] = block_time;
   o["block_number"] = block_number;

   string data = fc::json::to_string(o, fc::json::legacy_generator);

   auto prepare = graphene::utilities::createBulk(bulk_header, std::move(data));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk_lines));

   approximate_bulk_size += bulk_lines.back().size();

   send_bulk_if_ready();
}

void es_objects_plugin_impl::send_bulk_if_ready( bool force )
{
   if( bulk_lines.empty() )
      return;
   if( !force && bulk_lines.size() < limit_documents
         && approximate_bulk_size < graphene::utilities::es_client::request_size_threshold )
      return;
   constexpr uint32_t log_count_threshold = 20000; // lines
   constexpr uint32_t log_time_threshold = 3600; // seconds
   static uint64_t next_log_count = log_count_threshold;
   static fc::time_point next_log_time = fc::time_point::now() + fc::seconds(log_time_threshold);
   docs_sent_batch += bulk_lines.size();
   docs_sent_total += bulk_lines.size();
   bool log_by_next = ( docs_sent_total >= next_log_count || fc::time_point::now() >= next_log_time );
   if( log_by_next || limit_documents == _options.bulk_replay || force )
   {
      ilog( "Sending ${n} lines of bulk data to ElasticSearch at block ${blk}, "
            "this batch ${b}, total ${t}, approximate size ${s}",
            ("n",bulk_lines.size())("blk",block_number)
            ("b",docs_sent_batch)("t",docs_sent_total)("s",approximate_bulk_size) );
      next_log_count = docs_sent_total + log_count_threshold;
      next_log_time = fc::time_point::now() + fc::seconds(log_time_threshold);
   }
   // send data to elasticsearch when being forced or bulk is too large
   if( !es->send_bulk( bulk_lines ) )
   {
      elog( "Error sending ${n} lines of bulk data to ElasticSearch, the first lines are:", // GCOVR_EXCL_LINE
            ("n",bulk_lines.size()) ); // GCOVR_EXCL_LINE
      const auto log_max = std::min( bulk_lines.size(), size_t(10) );
      for( size_t i = 0; i < log_max; ++i )
      {
         edump( (bulk_lines[i]) ); // GCOVR_EXCL_LINE
      }
      FC_THROW_EXCEPTION( graphene::chain::plugin_exception,
            "Error populating ES database, we are going to keep trying." );
   }
   bulk_lines.clear();
   bulk_lines.reserve(limit_documents);
   approximate_bulk_size = 0;
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

         ("es-objects-proposals", boost::program_options::value<bool>(), "Store proposal objects (true)")
         ("es-objects-proposals-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the proposal objects (false)")
         ("es-objects-proposals-no-delete", boost::program_options::value<bool>(),
               "Do not delete a proposal from ES even if it is deleted from chain state. "
               "It is implicitly true and can not be set to false if es-objects-proposals-store-updates is true. "
               "(true)")

         ("es-objects-accounts", boost::program_options::value<bool>(), "Store account objects (true)")
         ("es-objects-accounts-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the account objects (false)")

         ("es-objects-assets", boost::program_options::value<bool>(), "Store asset objects (true)")
         ("es-objects-assets-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the asset objects (false)")

         ("es-objects-balances", boost::program_options::value<bool>(), "Store account balances (true)")
         ("es-objects-balances-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the account balances (false)")

         ("es-objects-limit-orders", boost::program_options::value<bool>(), "Store limit order objects (true)")
         ("es-objects-limit-orders-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the limit orders (false)")
         ("es-objects-limit-orders-no-delete", boost::program_options::value<bool>(),
               "Do not delete a limit order object from ES even if it is deleted from chain state. "
               "It is implicitly true and can not be set to false if es-objects-limit-orders-store-updates is true. "
               "(false)")

         ("es-objects-asset-bitasset", boost::program_options::value<bool>(),
               "Store bitasset data, including price feeds (true)")
         ("es-objects-asset-bitasset-store-updates", boost::program_options::value<bool>(),
               "Store all updates to the bitasset data (false)")

         ("es-objects-budget-records", boost::program_options::value<bool>(), "Store budget records (true)")

         ("es-objects-index-prefix", boost::program_options::value<std::string>(),
               "Add a prefix to the index(objects-)")
         ("es-objects-max-mapping-depth", boost::program_options::value<uint16_t>(),
               "Can not exceed the maximum index mapping depth (index.mapping.depth.limit) setting in ES, "
               "and need to be even smaller to not trigger the index.mapping.total_fields.limit error (10)")
         ("es-objects-keep-only-current", boost::program_options::value<bool>(),
               "Deprecated. Please use the store-updates or no-delete options. "
               "Keep only current state of the objects(true)")
         ("es-objects-start-es-after-block", boost::program_options::value<uint32_t>(),
               "Start doing ES job after block(0)")
         ("es-objects-sync-db-on-startup", boost::program_options::value<bool>(),
               "Copy all applicable objects from the object database (chain state) to ES on program startup (false)")
         ;
   cfg.add(cli);
}

void detail::es_objects_plugin_impl::init_program_options(const boost::program_options::variables_map& options)
{
   _options.init( options );

   es = std::make_unique<graphene::utilities::es_client>( _options.elasticsearch_url, _options.auth );

   FC_ASSERT( es->check_status(), "ES database is not up in url ${url}", ("url", _options.elasticsearch_url) );

   es->check_version_7_or_above( is_es_version_7_or_above );
}

void detail::es_objects_plugin_impl::plugin_options::init(const boost::program_options::variables_map& options)
{
   utilities::get_program_option( options, "es-objects-elasticsearch-url", elasticsearch_url );
   utilities::get_program_option( options, "es-objects-auth",              auth );
   utilities::get_program_option( options, "es-objects-bulk-replay",       bulk_replay );
   utilities::get_program_option( options, "es-objects-bulk-sync",         bulk_sync );
   utilities::get_program_option( options, "es-objects-proposals",                    proposals.enabled );
   utilities::get_program_option( options, "es-objects-proposals-store-updates",      proposals.store_updates );
   utilities::get_program_option( options, "es-objects-proposals-no-delete",          proposals.no_delete );
   utilities::get_program_option( options, "es-objects-accounts",                     accounts.enabled );
   utilities::get_program_option( options, "es-objects-accounts-store-updates",       accounts.store_updates );
   utilities::get_program_option( options, "es-objects-assets",                       assets.enabled );
   utilities::get_program_option( options, "es-objects-assets-store-updates",         assets.store_updates );
   utilities::get_program_option( options, "es-objects-balances",                     balances.enabled );
   utilities::get_program_option( options, "es-objects-balances-store-updates",       balances.store_updates );
   utilities::get_program_option( options, "es-objects-limit-orders",                 limit_orders.enabled );
   utilities::get_program_option( options, "es-objects-limit-orders-store-updates",   limit_orders.store_updates );
   utilities::get_program_option( options, "es-objects-limit-orders-no-delete",       limit_orders.no_delete );
   utilities::get_program_option( options, "es-objects-asset-bitasset",               asset_bitasset.enabled );
   utilities::get_program_option( options, "es-objects-asset-bitasset-store-updates", asset_bitasset.store_updates );
   utilities::get_program_option( options, "es-objects-budget-records",               budget.enabled );
   utilities::get_program_option( options, "es-objects-index-prefix",         index_prefix );
   utilities::get_program_option( options, "es-objects-max-mapping-depth",    max_mapping_depth );
   utilities::get_program_option( options, "es-objects-start-es-after-block", start_es_after_block );
   utilities::get_program_option( options, "es-objects-sync-db-on-startup",   sync_db_on_startup );
}

void es_objects_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->init_program_options( options );

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

}

void es_objects_plugin::plugin_startup()
{
   if( 0 == database().head_block_num() )
      my->sync_db( true );
   else if( my->_options.sync_db_on_startup )
      my->sync_db();
}

void es_objects_plugin::plugin_shutdown()
{
   my->send_bulk_if_ready(true); // flush
}

} }
