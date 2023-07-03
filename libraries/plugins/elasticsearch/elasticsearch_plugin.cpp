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

#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/chain/impacted.hpp>
#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/hardfork.hpp>

#include <boost/algorithm/string.hpp>

#include <graphene/utilities/boost_program_options.hpp>

namespace graphene { namespace elasticsearch {

namespace detail
{

class elasticsearch_plugin_impl
{
   public:
      explicit elasticsearch_plugin_impl(elasticsearch_plugin& _plugin)
         : _self( _plugin )
      { }

   private:
      friend class graphene::elasticsearch::elasticsearch_plugin;

      struct plugin_options
      {
         std::string elasticsearch_url = "http://localhost:9200/";
         std::string auth = "";
         uint32_t bulk_replay = 10000;
         uint32_t bulk_sync = 100;

         std::string index_prefix = "bitshares-";

         /// For the "index.mapping.depth.limit" setting in ES. The default value is 20.
         uint16_t max_mapping_depth = 20;

         uint32_t start_es_after_block = 0;

         bool visitor = false;
         bool operation_object = true;
         bool operation_string = false;

         mode elasticsearch_mode = mode::only_save;

         void init(const boost::program_options::variables_map& options);
      };

      void update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      elasticsearch_plugin& _self;
      plugin_options _options;

      primary_index< operation_history_index >* _oho_index;

      uint32_t limit_documents = _options.bulk_replay;

      std::unique_ptr<graphene::utilities::es_client> es;

      vector <string> bulk_lines; //  vector of op lines
      size_t approximate_bulk_size = 0;

      bulk_struct bulk_line_struct;

      std::string index_name;
      bool is_sync = false;
      bool is_es_version_7_or_above = true;

      void add_elasticsearch( const account_id_type& account_id, const optional<operation_history_object>& oho,
                              uint32_t block_number );
      void send_bulk( uint32_t block_num );

      void doOperationHistory(const optional <operation_history_object>& oho, operation_history_struct& os) const;
      void doBlock(uint32_t trx_in_block, const signed_block& b, block_struct& bs) const;
      void doVisitor(const optional <operation_history_object>& oho, visitor_struct& vs) const;
      void checkState(const fc::time_point_sec& block_time);
      void cleanObjects(const account_history_object& ath, const account_id_type& account_id);

      void init_program_options(const boost::program_options::variables_map& options);
};

static std::string generateIndexName( const fc::time_point_sec& block_date,
                                      const std::string& index_prefix )
{
   auto block_date_string = block_date.to_iso_string();
   std::vector<std::string> parts;
   boost::split(parts, block_date_string, boost::is_any_of("-"));
   std::string index_name = index_prefix + parts[0] + "-" + parts[1];
   return index_name;
}

void elasticsearch_plugin_impl::update_account_histories( const signed_block& b )
{
   checkState(b.timestamp);
   index_name = generateIndexName(b.timestamp, _options.index_prefix);

   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   bool is_first = true;
   auto skip_oho_id = [&is_first,&db,this]() {
      if( is_first && db._undo_db.enabled() ) // this ensures that the current id is rolled back on undo
      {
         db.remove( db.create<operation_history_object>( []( operation_history_object& obj) {} ) );
         is_first = false;
      }
      else
         _oho_index->use_next_id();
   };
   for( const optional< operation_history_object >& o_op : hist ) {
      optional <operation_history_object> oho;

      auto create_oho = [&]() {
         is_first = false;
         return optional<operation_history_object>(
               db.create<operation_history_object>([&](operation_history_object &h) {
                  if (o_op.valid())
                  {
                     h.op           = o_op->op;
                     h.result       = o_op->result;
                     h.block_num    = o_op->block_num;
                     h.trx_in_block = o_op->trx_in_block;
                     h.op_in_trx    = o_op->op_in_trx;
                     h.virtual_op   = o_op->virtual_op;
                     h.is_virtual   = o_op->is_virtual;
                     h.block_time   = o_op->block_time;
                  }
               }));
      };

      if( !o_op.valid() ) {
         skip_oho_id();
         continue;
      }
      oho = create_oho();

      // populate what we can before impacted loop
      if( o_op->block_num > _options.start_es_after_block )
      {
         bulk_line_struct.operation_type = oho->op.which();
         bulk_line_struct.operation_id_num = oho->id.instance();
         doOperationHistory( oho, bulk_line_struct.operation_history );
         doBlock( oho->trx_in_block, b, bulk_line_struct.block_data );
         if( _options.visitor )
            doVisitor( oho, *bulk_line_struct.additional_data );
      }

      const operation_history_object& op = *o_op;

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      vector<authority> other;
      // fee_payer is added here
      operation_get_required_authorities( op.op, impacted, impacted, other,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ) );

      if( op.op.is_type< account_create_operation >() )
         impacted.insert( account_id_type( op.result.get<object_id_type>() ) );

      // https://github.com/bitshares/bitshares-core/issues/265
      if( HARDFORK_CORE_265_PASSED(b.timestamp) || !op.op.is_type< account_create_operation >() )
      {
         operation_get_impacted_accounts( op.op, impacted,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ) );
      }

      if( op.result.is_type<extendable_operation_result>() )
      {
         const auto& op_result = op.result.get<extendable_operation_result>();
         if( op_result.value.impacted_accounts.valid() )
         {
            for( const auto& a : *op_result.value.impacted_accounts )
               impacted.insert( a );
         }
      }

      for( const auto& a : other )
         for( const auto& item : a.account_auths )
            impacted.insert( item.first );

      for( const auto& account_id : impacted )
      {
         // Note: we send bulk if there are too many items in bulk_lines
         add_elasticsearch( account_id, oho, b.block_num() );
      }

   }

   // we send bulk at end of block when we are in sync for better real time client experience
   if( is_sync && !bulk_lines.empty() )
      send_bulk( b.block_num() );

}

void elasticsearch_plugin_impl::send_bulk( uint32_t block_num )
{
   ilog( "Sending ${n} lines of bulk data to ElasticSearch at block ${b}, approximate size ${s}",
         ("n",bulk_lines.size())("b",block_num)("s",approximate_bulk_size) );
   if( !es->send_bulk( bulk_lines ) )
   {
      elog( "Error sending ${n} lines of bulk data to ElasticSearch, the first lines are:",
            ("n",bulk_lines.size()) );
      const auto log_max = std::min( bulk_lines.size(), size_t(10) );
      for( size_t i = 0; i < log_max; ++i )
      {
         edump( (bulk_lines[i]) );
      }
      FC_THROW_EXCEPTION( graphene::chain::plugin_exception,
            "Error populating ES database, we are going to keep trying." );
   }
   bulk_lines.clear();
   approximate_bulk_size = 0;
   bulk_lines.reserve(limit_documents);
}

void elasticsearch_plugin_impl::checkState(const fc::time_point_sec& block_time)
{
   if((fc::time_point::now() - block_time) < fc::seconds(30))
   {
      limit_documents = _options.bulk_sync;
      is_sync = true;
   }
   else
   {
      limit_documents = _options.bulk_replay;
      is_sync = false;
   }
   bulk_lines.reserve(limit_documents);
}

struct get_fee_payer_visitor
{
   using result_type = account_id_type;

   template<typename OpType>
   account_id_type operator()(const OpType& op) const
   {
      return op.fee_payer();
   }
};

void elasticsearch_plugin_impl::doOperationHistory( const optional <operation_history_object>& oho,
                                                    operation_history_struct& os ) const
{ try {
   os.trx_in_block = oho->trx_in_block;
   os.op_in_trx = oho->op_in_trx;
   os.virtual_op = oho->virtual_op;
   os.is_virtual = oho->is_virtual;
   os.fee_payer = oho->op.visit( get_fee_payer_visitor() );

   if(_options.operation_string)
      os.op = fc::json::to_string(oho->op);

   os.operation_result = fc::json::to_string(oho->result);

   if(_options.operation_object) {
      constexpr uint16_t current_depth = 2;
      // op
      oho->op.visit(fc::from_static_variant(os.op_object, FC_PACK_MAX_DEPTH));
      os.op_object = graphene::utilities::es_data_adaptor::adapt( os.op_object.get_object(),
                                                                  _options.max_mapping_depth - current_depth );
      // operation_result
      variant v;
      fc::to_variant( oho->result, v, FC_PACK_MAX_DEPTH );
      os.operation_result_object = graphene::utilities::es_data_adaptor::adapt_static_variant( v.get_array(),
                                         _options.max_mapping_depth - current_depth );
   }
} FC_CAPTURE_LOG_AND_RETHROW( (oho) ) } // GCOVR_EXCL_LINE

void elasticsearch_plugin_impl::doBlock(uint32_t trx_in_block, const signed_block& b, block_struct& bs) const
{
   std::string trx_id = "";
   if(trx_in_block < b.transactions.size())
      trx_id = b.transactions[trx_in_block].id().str();
   bs.block_num = b.block_num();
   bs.block_time = b.timestamp;
   bs.trx_id = trx_id;
}

struct operation_visitor
{
   using result_type = void;

   share_type fee_amount;
   asset_id_type fee_asset;

   asset_id_type transfer_asset_id;
   share_type transfer_amount;
   account_id_type transfer_from;
   account_id_type transfer_to;

   void operator()( const graphene::chain::transfer_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      transfer_asset_id = o.amount.asset_id;
      transfer_amount = o.amount.amount;
      transfer_from = o.from;
      transfer_to = o.to;
   }

   object_id_type      fill_order_id;
   account_id_type     fill_account_id;
   asset_id_type       fill_pays_asset_id;
   share_type          fill_pays_amount;
   asset_id_type       fill_receives_asset_id;
   share_type          fill_receives_amount;
   double              fill_fill_price;
   bool                fill_is_maker;

   void operator()( const graphene::chain::fill_order_operation& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;

      fill_order_id = o.order_id;
      fill_account_id = o.account_id;
      fill_pays_asset_id = o.pays.asset_id;
      fill_pays_amount = o.pays.amount;
      fill_receives_asset_id = o.receives.asset_id;
      fill_receives_amount = o.receives.amount;
      fill_fill_price = o.fill_price.to_real();
      fill_is_maker = o.is_maker;
   }

   template<typename T>
   void operator()( const T& o )
   {
      fee_asset = o.fee.asset_id;
      fee_amount = o.fee.amount;
   }
};

void elasticsearch_plugin_impl::doVisitor(const optional <operation_history_object>& oho, visitor_struct& vs) const
{
   const graphene::chain::database& db = _self.database();

   operation_visitor o_v;
   oho->op.visit(o_v);

   auto fee_asset = o_v.fee_asset(db);
   vs.fee_data.asset = o_v.fee_asset;
   vs.fee_data.asset_name = fee_asset.symbol;
   vs.fee_data.amount = o_v.fee_amount;
   vs.fee_data.amount_units = (o_v.fee_amount.value)/(double)asset::scaled_precision(fee_asset.precision).value;

   auto transfer_asset = o_v.transfer_asset_id(db);
   vs.transfer_data.asset = o_v.transfer_asset_id;
   vs.transfer_data.asset_name = transfer_asset.symbol;
   vs.transfer_data.amount = o_v.transfer_amount;
   vs.transfer_data.amount_units = (o_v.transfer_amount.value)
                                 / (double)asset::scaled_precision(transfer_asset.precision).value;
   vs.transfer_data.from = o_v.transfer_from;
   vs.transfer_data.to = o_v.transfer_to;

   auto fill_pays_asset = o_v.fill_pays_asset_id(db);
   auto fill_receives_asset = o_v.fill_receives_asset_id(db);
   vs.fill_data.order_id = o_v.fill_order_id;
   vs.fill_data.account_id = o_v.fill_account_id;
   vs.fill_data.pays_asset_id = o_v.fill_pays_asset_id;
   vs.fill_data.pays_asset_name = fill_pays_asset.symbol;
   vs.fill_data.pays_amount = o_v.fill_pays_amount;
   vs.fill_data.pays_amount_units = (o_v.fill_pays_amount.value)
                                  / (double)asset::scaled_precision(fill_pays_asset.precision).value;
   vs.fill_data.receives_asset_id = o_v.fill_receives_asset_id;
   vs.fill_data.receives_asset_name = fill_receives_asset.symbol;
   vs.fill_data.receives_amount = o_v.fill_receives_amount;
   vs.fill_data.receives_amount_units = (o_v.fill_receives_amount.value)
                                      / (double)asset::scaled_precision(fill_receives_asset.precision).value;

   auto fill_price = (o_v.fill_receives_amount.value
                      / (double)asset::scaled_precision(fill_receives_asset.precision).value)
                   / (o_v.fill_pays_amount.value
                      / (double)asset::scaled_precision(fill_pays_asset.precision).value);
   vs.fill_data.fill_price_units = fill_price;
   vs.fill_data.fill_price = o_v.fill_fill_price;
   vs.fill_data.is_maker = o_v.fill_is_maker;
}

void elasticsearch_plugin_impl::add_elasticsearch( const account_id_type& account_id,
                                                   const optional<operation_history_object>& oho,
                                                   uint32_t block_number )
{
   graphene::chain::database& db = database();

   const auto &stats_obj = db.get_account_stats_by_owner( account_id );

   const auto &ath = db.create<account_history_object>(
         [&oho,&account_id,&stats_obj]( account_history_object &obj ) {
      obj.operation_id = oho->id;
      obj.account = account_id;
      obj.sequence = stats_obj.total_ops + 1;
      obj.next = stats_obj.most_recent_op;
   });

   db.modify( stats_obj, [&ath]( account_statistics_object &obj ) {
      obj.most_recent_op = ath.id;
      obj.total_ops = ath.sequence;
   });

   if( block_number > _options.start_es_after_block )
   {
      bulk_line_struct.account_history = ath;

      auto bulk_line = fc::json::to_string(bulk_line_struct, fc::json::legacy_generator);

      fc::mutable_variant_object bulk_header;
      bulk_header["_index"] = index_name;
      if( !is_es_version_7_or_above )
         bulk_header["_type"] = "_doc";
      bulk_header["_id"] = std::string( ath.id );
      auto prepare = graphene::utilities::createBulk(bulk_header, std::move(bulk_line));
      std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk_lines));

      approximate_bulk_size += bulk_lines.back().size();

      if( bulk_lines.size() >= limit_documents
            || approximate_bulk_size >= graphene::utilities::es_client::request_size_threshold )
         send_bulk( block_number );
   }
   cleanObjects(ath, account_id);
}

void elasticsearch_plugin_impl::cleanObjects( const account_history_object& ath,
                                              const account_id_type& account_id )
{
   graphene::chain::database& db = database();
   // remove everything except current object from ath
   const auto &his_idx = db.get_index_type<account_history_index>();
   const auto &by_seq_idx = his_idx.indices().get<by_seq>();
   auto itr = by_seq_idx.lower_bound(boost::make_tuple(account_id, 0));
   if (itr != by_seq_idx.end() && itr->account == account_id && itr->id != ath.id) {
      // if found, remove the entry
      const auto remove_op_id = itr->operation_id;
      const auto itr_remove = itr;
      ++itr;
      db.remove( *itr_remove );
      // modify previous node's next pointer
      // this should be always true, but just have a check here
      if( itr != by_seq_idx.end() && itr->account == account_id )
      {
         db.modify( *itr, []( account_history_object& obj ){
            obj.next = account_history_id_type();
         });
      }
      // do the same on oho
      const auto &by_opid_idx = his_idx.indices().get<by_opid>();
      if (by_opid_idx.find(remove_op_id) == by_opid_idx.end()) {
         db.remove(remove_op_id(db));
      }
   }
}

} // end namespace detail

elasticsearch_plugin::elasticsearch_plugin(graphene::app::application& app) :
   plugin(app),
   my( std::make_unique<detail::elasticsearch_plugin_impl>(*this) )
{
   // Nothing else to do
}

elasticsearch_plugin::~elasticsearch_plugin() = default;

std::string elasticsearch_plugin::plugin_name()const
{
   return "elasticsearch";
}
std::string elasticsearch_plugin::plugin_description()const
{
   return "Stores account history data in elasticsearch database(EXPERIMENTAL).";
}

void elasticsearch_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("elasticsearch-node-url", boost::program_options::value<std::string>(),
               "Elastic Search database node url(http://localhost:9200/)")
         ("elasticsearch-basic-auth", boost::program_options::value<std::string>(),
               "Pass basic auth to elasticsearch database('')")
         ("elasticsearch-bulk-replay", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on replay(10000)")
         ("elasticsearch-bulk-sync", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on a syncronied chain(100)")
         ("elasticsearch-index-prefix", boost::program_options::value<std::string>(),
               "Add a prefix to the index(bitshares-)")
         ("elasticsearch-max-mapping-depth", boost::program_options::value<uint16_t>(),
               "The maximum index mapping depth (index.mapping.depth.limit) setting in ES, "
               "should be >=2. (20)")
         ("elasticsearch-start-es-after-block", boost::program_options::value<uint32_t>(),
               "Start doing ES job after block(0)")
         ("elasticsearch-visitor", boost::program_options::value<bool>(),
               "Use visitor to index additional data(slows down the replay(false))")
         ("elasticsearch-operation-object", boost::program_options::value<bool>(),
               "Save operation as object(true)")
         ("elasticsearch-operation-string", boost::program_options::value<bool>(),
               "Save operation as string. Needed to serve history api calls(false)")
         ("elasticsearch-mode", boost::program_options::value<uint16_t>(),
               "Mode of operation: only_save(0), only_query(1), all(2) - Default: 0")
         ;
   cfg.add(cli);
}

void detail::elasticsearch_plugin_impl::init_program_options(const boost::program_options::variables_map& options)
{
   _options.init( options );

   if( _options.visitor )
      bulk_line_struct.additional_data = visitor_struct();

   es = std::make_unique<graphene::utilities::es_client>( _options.elasticsearch_url, _options.auth );

   FC_ASSERT( es->check_status(), "ES database is not up in url ${url}", ("url", _options.elasticsearch_url) );

   es->check_version_7_or_above( is_es_version_7_or_above );
}

void detail::elasticsearch_plugin_impl::plugin_options::init(const boost::program_options::variables_map& options)
{
   utilities::get_program_option( options, "elasticsearch-node-url",     elasticsearch_url );
   utilities::get_program_option( options, "elasticsearch-basic-auth",   auth );
   utilities::get_program_option( options, "elasticsearch-bulk-replay",  bulk_replay );
   utilities::get_program_option( options, "elasticsearch-bulk-sync",    bulk_sync );
   utilities::get_program_option( options, "elasticsearch-index-prefix",         index_prefix );
   utilities::get_program_option( options, "elasticsearch-max-mapping-depth",    max_mapping_depth );
   utilities::get_program_option( options, "elasticsearch-start-es-after-block", start_es_after_block );
   utilities::get_program_option( options, "elasticsearch-visitor",          visitor );
   utilities::get_program_option( options, "elasticsearch-operation-object", operation_object );
   utilities::get_program_option( options, "elasticsearch-operation-string", operation_string );

   FC_ASSERT( max_mapping_depth >= 2, "The minimum value of elasticsearch-max-mapping-depth is 2" );

   auto es_mode = static_cast<uint16_t>( elasticsearch_mode );
   utilities::get_program_option( options, "elasticsearch-mode", es_mode );
   if( es_mode > static_cast<uint16_t>( mode::all ) )
      FC_THROW_EXCEPTION( graphene::chain::plugin_exception, "Elasticsearch mode not valid" );
   elasticsearch_mode = static_cast<mode>( es_mode );

   if( mode::all == elasticsearch_mode && !operation_string )
   {
      FC_THROW_EXCEPTION( graphene::chain::plugin_exception,
            "If elasticsearch-mode is set to all then elasticsearch-operation-string need to be true");
   }
}

void elasticsearch_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->init_program_options( options );

   my->_oho_index = database().add_index< primary_index< operation_history_index > >();
   database().add_index< primary_index< account_history_index > >();

   if( my->_options.elasticsearch_mode != mode::only_query )
   {
      // connect with group 0 to process before some special steps (e.g. snapshot or next_object_id)
      database().applied_block.connect( 0, [this](const signed_block &b) {
         my->update_account_histories(b);
      });
   }
}

void elasticsearch_plugin::plugin_startup()
{
   // Nothing to do
}

static operation_history_object fromEStoOperation(const variant& source)
{
   operation_history_object result;

   const auto operation_id = source["account_history"]["operation_id"];
   fc::from_variant( operation_id, result.id, GRAPHENE_MAX_NESTED_OBJECTS );

   const auto op = fc::json::from_string(source["operation_history"]["op"].as_string());
   fc::from_variant( op, result.op, GRAPHENE_MAX_NESTED_OBJECTS );

   const auto operation_result = fc::json::from_string(source["operation_history"]["operation_result"].as_string());
   fc::from_variant( operation_result, result.result, GRAPHENE_MAX_NESTED_OBJECTS );

   result.block_num = source["block_data"]["block_num"].as_uint64();
   result.trx_in_block = source["operation_history"]["trx_in_block"].as_uint64();
   result.op_in_trx = source["operation_history"]["op_in_trx"].as_uint64();
   result.trx_in_block = source["operation_history"]["virtual_op"].as_uint64();
   result.is_virtual = source["operation_history"]["is_virtual"].as_bool();

   result.block_time = fc::time_point_sec::from_iso_string( source["block_data"]["block_time"].as_string() );

   return result;
}

operation_history_object elasticsearch_plugin::get_operation_by_id( const operation_history_id_type& id ) const
{
   const string operation_id_string = std::string(object_id_type(id));

   const string query = R"(
   {
      "query": {
         "match":
         {
            "account_history.operation_id": ")" + operation_id_string + R"("
         }
      }
   }
   )";

   const auto uri = my->_options.index_prefix + ( my->is_es_version_7_or_above ? "*/_search" : "*/_doc/_search" );
   const auto response = my->es->query( uri, query );
   variant variant_response = fc::json::from_string(response);
   const auto source = variant_response["hits"]["hits"][size_t(0)]["_source"];
   return fromEStoOperation(source);
}

vector<operation_history_object> elasticsearch_plugin::get_account_history(
      const account_id_type& account_id,
      const operation_history_id_type& stop,
      uint64_t limit,
      const operation_history_id_type& start ) const
{
   const auto account_id_string = std::string( account_id );

   const auto stop_number = stop.instance.value;
   const auto start_number = start.instance.value;

   string range = "";
   if(stop_number == 0)
      range = " AND operation_id_num: ["+fc::to_string(stop_number)+" TO "+fc::to_string(start_number)+"]";
   else if(stop_number > 0)
      range = " AND operation_id_num: {"+fc::to_string(stop_number)+" TO "+fc::to_string(start_number)+"]";
   // FIXME the code above is either redundant or buggy

   const string query = R"(
   {
      "size": )" + fc::to_string(limit) + R"(,
      "sort" : [{ "operation_id_num" : {"order" : "desc"}}],
      "query": {
         "bool": {
            "must": [
            {
               "query_string": {
                  "query": "account_history.account: )" + account_id_string +  range + R"("
               }
            }
            ]
         }
      }
   }
   )";

   vector<operation_history_object> result;

   if( !my->es->check_status() )
      return result;

   const auto uri = my->_options.index_prefix + ( my->is_es_version_7_or_above ? "*/_search" : "*/_doc/_search" );
   const auto response = my->es->query( uri, query );

   variant variant_response = fc::json::from_string(response);

   const auto hits = variant_response["hits"]["total"];
   size_t size;
   if( hits.is_object() ) // ES-7 ?
      size = hits["value"].as_uint64();
   else // probably ES-6
      size = hits.as_uint64();
   size = std::min( size, size_t(limit) );

   const auto& data = variant_response["hits"]["hits"];
   for( size_t i=0; i<size; ++i )
   {
      const auto& source = data[i]["_source"];
      result.push_back(fromEStoOperation(source));
   }
   return result;
}

mode elasticsearch_plugin::get_running_mode() const
{
   return my->_options.elasticsearch_mode;
}

} }
