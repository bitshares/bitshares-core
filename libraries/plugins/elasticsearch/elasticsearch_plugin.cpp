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
#include <fc/smart_ref_impl.hpp>
#include <curl/curl.h>
#include <graphene/utilities/elasticsearch.hpp>

namespace graphene { namespace elasticsearch {

namespace detail
{

class elasticsearch_plugin_impl
{
   public:
      elasticsearch_plugin_impl(elasticsearch_plugin& _plugin)
         : _self( _plugin )
      {  curl = curl_easy_init(); }
      virtual ~elasticsearch_plugin_impl();

      bool update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      elasticsearch_plugin& _self;
      primary_index< operation_history_index >* _oho_index;

      std::string _elasticsearch_node_url = "http://localhost:9200/";
      uint32_t _elasticsearch_bulk_replay = 10000;
      uint32_t _elasticsearch_bulk_sync = 100;
      bool _elasticsearch_visitor = false;
      std::string _elasticsearch_basic_auth = "";
      std::string _elasticsearch_index_prefix = "bitshares-";
      bool _elasticsearch_operation_object = false;
      CURL *curl; // curl handler
      vector <string> bulk_lines; //  vector of op lines
      vector<std::string> prepare;

      graphene::utilities::ES es;
      uint32_t limit_documents;
      int16_t op_type;
      operation_history_struct os;
      block_struct bs;
      visitor_struct vs;
      bulk_struct bulk_line_struct;
      std::string bulk_line;
      std::string index_name;
      bool is_sync = false;
   private:
      bool add_elasticsearch( const account_id_type account_id, const optional<operation_history_object>& oho );
      const account_transaction_history_object& addNewEntry(const account_statistics_object& stats_obj,
                                                            const account_id_type& account_id,
                                                            const optional <operation_history_object>& oho);
      const account_statistics_object& getStatsObject(const account_id_type& account_id);
      void growStats(const account_statistics_object& stats_obj, const account_transaction_history_object& ath);
      void getOperationType(const optional <operation_history_object>& oho);
      void doOperationHistory(const optional <operation_history_object>& oho);
      void doBlock(uint32_t trx_in_block, const signed_block& b);
      void doVisitor(const optional <operation_history_object>& oho);
      void checkState(const fc::time_point_sec& block_time);
      void cleanObjects(const account_transaction_history_id_type& ath, const account_id_type& account_id);
      void createBulkLine(const account_transaction_history_object& ath);
      void prepareBulk(const account_transaction_history_id_type& ath_id);
      void populateESstruct();
};

elasticsearch_plugin_impl::~elasticsearch_plugin_impl()
{
   return;
}

bool elasticsearch_plugin_impl::update_account_histories( const signed_block& b )
{
   checkState(b.timestamp);
   index_name = graphene::utilities::generateIndexName(b.timestamp, _elasticsearch_index_prefix);

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
                  }
               }));
      };

      if( !o_op.valid() ) {
         skip_oho_id();
         continue;
      }
      oho = create_oho();

      // populate what we can before impacted loop
      getOperationType(oho);
      doOperationHistory(oho);
      doBlock(oho->trx_in_block, b);
      if(_elasticsearch_visitor)
         doVisitor(oho);

      const operation_history_object& op = *o_op;

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      vector<authority> other;
      operation_get_required_authorities( op.op, impacted, impacted, other ); // fee_payer is added here

      if( op.op.which() == operation::tag< account_create_operation >::value )
         impacted.insert( op.result.get<object_id_type>() );
      else
         graphene::chain::operation_get_impacted_accounts( op.op, impacted );

      for( auto& a : other )
         for( auto& item : a.account_auths )
            impacted.insert( item.first );

      for( auto& account_id : impacted )
      {
         if(!add_elasticsearch( account_id, oho ))
            return false;
      }
   }
   // we send bulk at end of block when we are in sync for better real time client experience
   if(is_sync)
   {
      populateESstruct();
      if(es.bulk_lines.size() > 0)
      {
         prepare.clear();
         if(!graphene::utilities::SendBulk(std::move(es)))
            return false;
         else
            bulk_lines.clear();
      }
   }

   if(bulk_lines.size() != limit_documents)
      bulk_lines.reserve(limit_documents);

   return true;
}

void elasticsearch_plugin_impl::checkState(const fc::time_point_sec& block_time)
{
   if((fc::time_point::now() - block_time) < fc::seconds(30))
   {
      limit_documents = _elasticsearch_bulk_sync;
      is_sync = true;
   }
   else
   {
      limit_documents = _elasticsearch_bulk_replay;
      is_sync = false;
   }
}

void elasticsearch_plugin_impl::getOperationType(const optional <operation_history_object>& oho)
{
   if (!oho->id.is_null())
      op_type = oho->op.which();
}

void elasticsearch_plugin_impl::doOperationHistory(const optional <operation_history_object>& oho)
{
   os.trx_in_block = oho->trx_in_block;
   os.op_in_trx = oho->op_in_trx;
   os.operation_result = fc::json::to_string(oho->result);
   os.virtual_op = oho->virtual_op;

   if(_elasticsearch_operation_object) {
      oho->op.visit(fc::from_static_variant(os.op_object, FC_PACK_MAX_DEPTH));
      adaptor_struct adaptor;
      os.op_object = adaptor.adapt(os.op_object.get_object());
   }
   else
      os.op = fc::json::to_string(oho->op);

}

void elasticsearch_plugin_impl::doBlock(uint32_t trx_in_block, const signed_block& b)
{
   std::string trx_id = "";
   if(trx_in_block < b.transactions.size())
      trx_id = b.transactions[trx_in_block].id().str();
   bs.block_num = b.block_num();
   bs.block_time = b.timestamp;
   bs.trx_id = trx_id;
}

void elasticsearch_plugin_impl::doVisitor(const optional <operation_history_object>& oho)
{
   graphene::chain::database& db = database();

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
   vs.transfer_data.amount_units = (o_v.transfer_amount.value)/(double)asset::scaled_precision(transfer_asset.precision).value;
   vs.transfer_data.from = o_v.transfer_from;
   vs.transfer_data.to = o_v.transfer_to;

   auto fill_pays_asset = o_v.fill_pays_asset_id(db);
   auto fill_receives_asset = o_v.fill_receives_asset_id(db);
   vs.fill_data.order_id = o_v.fill_order_id;
   vs.fill_data.account_id = o_v.fill_account_id;
   vs.fill_data.pays_asset_id = o_v.fill_pays_asset_id;
   vs.fill_data.pays_asset_name = fill_pays_asset.symbol;
   vs.fill_data.pays_amount = o_v.fill_pays_amount;
   vs.fill_data.pays_amount_units = (o_v.fill_pays_amount.value)/(double)asset::scaled_precision(fill_pays_asset.precision).value;
   vs.fill_data.receives_asset_id = o_v.fill_receives_asset_id;
   vs.fill_data.receives_asset_name = fill_receives_asset.symbol;
   vs.fill_data.receives_amount = o_v.fill_receives_amount;
   vs.fill_data.receives_amount_units = (o_v.fill_receives_amount.value)/(double)asset::scaled_precision(fill_receives_asset.precision).value;

   auto fill_price = (o_v.fill_receives_amount.value/(double)asset::scaled_precision(fill_receives_asset.precision).value) /
           (o_v.fill_pays_amount.value/(double)asset::scaled_precision(fill_pays_asset.precision).value);
   vs.fill_data.fill_price_units = fill_price;
   vs.fill_data.fill_price = o_v.fill_fill_price;
   vs.fill_data.is_maker = o_v.fill_is_maker;
}

bool elasticsearch_plugin_impl::add_elasticsearch( const account_id_type account_id,
                                                   const optional <operation_history_object>& oho)
{
   const auto &stats_obj = getStatsObject(account_id);
   const auto &ath = addNewEntry(stats_obj, account_id, oho);
   growStats(stats_obj, ath);
   createBulkLine(ath);
   prepareBulk(ath.id);
   cleanObjects(ath.id, account_id);

   if (curl && bulk_lines.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech
      prepare.clear();
      populateESstruct();
      if(!graphene::utilities::SendBulk(std::move(es)))
         return false;
      else
         bulk_lines.clear();
   }

   return true;
}

const account_statistics_object& elasticsearch_plugin_impl::getStatsObject(const account_id_type& account_id)
{
   graphene::chain::database& db = database();
   const auto &stats_obj = db.get_account_stats_by_owner(account_id);

   return stats_obj;
}

const account_transaction_history_object& elasticsearch_plugin_impl::addNewEntry(const account_statistics_object& stats_obj,
                                                                                 const account_id_type& account_id,
                                                                                 const optional <operation_history_object>& oho)
{
   graphene::chain::database& db = database();
   const auto &ath = db.create<account_transaction_history_object>([&](account_transaction_history_object &obj) {
      obj.operation_id = oho->id;
      obj.account = account_id;
      obj.sequence = stats_obj.total_ops + 1;
      obj.next = stats_obj.most_recent_op;
   });

   return ath;
}

void elasticsearch_plugin_impl::growStats(const account_statistics_object& stats_obj,
                                          const account_transaction_history_object& ath)
{
   graphene::chain::database& db = database();
   db.modify(stats_obj, [&](account_statistics_object &obj) {
      obj.most_recent_op = ath.id;
      obj.total_ops = ath.sequence;
   });
}

void elasticsearch_plugin_impl::createBulkLine(const account_transaction_history_object& ath)
{
   bulk_line_struct.account_history = ath;
   bulk_line_struct.operation_history = os;
   bulk_line_struct.operation_type = op_type;
   bulk_line_struct.operation_id_num = ath.operation_id.instance.value;
   bulk_line_struct.block_data = bs;
   if(_elasticsearch_visitor)
      bulk_line_struct.additional_data = vs;
   bulk_line = fc::json::to_string(bulk_line_struct, fc::json::legacy_generator);
}

void elasticsearch_plugin_impl::prepareBulk(const account_transaction_history_id_type& ath_id)
{
   const std::string _id = fc::json::to_string(ath_id);
   fc::mutable_variant_object bulk_header;
   bulk_header["_index"] = index_name;
   bulk_header["_type"] = "data";
   bulk_header["_id"] = fc::to_string(ath_id.space_id) + "." + fc::to_string(ath_id.type_id) + "."
                      + fc::to_string(ath_id.instance.value);
   prepare = graphene::utilities::createBulk(bulk_header, std::move(bulk_line));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk_lines));
   prepare.clear();
}

void elasticsearch_plugin_impl::cleanObjects(const account_transaction_history_id_type& ath_id, const account_id_type& account_id)
{
   graphene::chain::database& db = database();
   // remove everything except current object from ath
   const auto &his_idx = db.get_index_type<account_transaction_history_index>();
   const auto &by_seq_idx = his_idx.indices().get<by_seq>();
   auto itr = by_seq_idx.lower_bound(boost::make_tuple(account_id, 0));
   if (itr != by_seq_idx.end() && itr->account == account_id && itr->id != ath_id) {
      // if found, remove the entry
      const auto remove_op_id = itr->operation_id;
      const auto itr_remove = itr;
      ++itr;
      db.remove( *itr_remove );
      // modify previous node's next pointer
      // this should be always true, but just have a check here
      if( itr != by_seq_idx.end() && itr->account == account_id )
      {
         db.modify( *itr, [&]( account_transaction_history_object& obj ){
            obj.next = account_transaction_history_id_type();
         });
      }
      // do the same on oho
      const auto &by_opid_idx = his_idx.indices().get<by_opid>();
      if (by_opid_idx.find(remove_op_id) == by_opid_idx.end()) {
         db.remove(remove_op_id(db));
      }
   }
}

void elasticsearch_plugin_impl::populateESstruct()
{
   es.curl = curl;
   es.bulk_lines = std::move(bulk_lines);
   es.elasticsearch_url = _elasticsearch_node_url;
   es.auth = _elasticsearch_basic_auth;
   es.index_prefix = _elasticsearch_index_prefix;
   es.endpoint = "";
   es.query = "";
}

} // end namespace detail

elasticsearch_plugin::elasticsearch_plugin() :
   my( new detail::elasticsearch_plugin_impl(*this) )
{
}

elasticsearch_plugin::~elasticsearch_plugin()
{
}

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
         ("elasticsearch-node-url", boost::program_options::value<std::string>(), "Elastic Search database node url(http://localhost:9200/)")
         ("elasticsearch-bulk-replay", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on replay(10000)")
         ("elasticsearch-bulk-sync", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on a syncronied chain(100)")
         ("elasticsearch-visitor", boost::program_options::value<bool>(), "Use visitor to index additional data(slows down the replay(false))")
         ("elasticsearch-basic-auth", boost::program_options::value<std::string>(), "Pass basic auth to elasticsearch database('')")
         ("elasticsearch-index-prefix", boost::program_options::value<std::string>(), "Add a prefix to the index(bitshares-)")
         ("elasticsearch-operation-object", boost::program_options::value<bool>(), "Save operation as object(false)")
         ;
   cfg.add(cli);
}

void elasticsearch_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b) {
      if(!my->update_account_histories(b))
      {
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Error populating ES database, we are going to keep trying.");
      }
   } );
   my->_oho_index = database().add_index< primary_index< operation_history_index > >();
   database().add_index< primary_index< account_transaction_history_index > >();

   if (options.count("elasticsearch-node-url")) {
      my->_elasticsearch_node_url = options["elasticsearch-node-url"].as<std::string>();
   }
   if (options.count("elasticsearch-bulk-replay")) {
      my->_elasticsearch_bulk_replay = options["elasticsearch-bulk-replay"].as<uint32_t>();
   }
   if (options.count("elasticsearch-bulk-sync")) {
      my->_elasticsearch_bulk_sync = options["elasticsearch-bulk-sync"].as<uint32_t>();
   }
   if (options.count("elasticsearch-visitor")) {
      my->_elasticsearch_visitor = options["elasticsearch-visitor"].as<bool>();
   }
   if (options.count("elasticsearch-basic-auth")) {
      my->_elasticsearch_basic_auth = options["elasticsearch-basic-auth"].as<std::string>();
   }
   if (options.count("elasticsearch-index-prefix")) {
      my->_elasticsearch_index_prefix = options["elasticsearch-index-prefix"].as<std::string>();
   }
   if (options.count("elasticsearch-operation-object")) {
      my->_elasticsearch_operation_object = options["elasticsearch-operation-object"].as<bool>();
   }
}

void elasticsearch_plugin::plugin_startup()
{
   graphene::utilities::ES es;
   es.curl = my->curl;
   es.elasticsearch_url = my->_elasticsearch_node_url;
   es.auth = my->_elasticsearch_basic_auth;

   if(!graphene::utilities::checkES(es))
      FC_THROW_EXCEPTION(fc::exception, "ES database is not up in url ${url}", ("url", my->_elasticsearch_node_url));
   ilog("elasticsearch ACCOUNT HISTORY: plugin_startup() begin");
}

} }
