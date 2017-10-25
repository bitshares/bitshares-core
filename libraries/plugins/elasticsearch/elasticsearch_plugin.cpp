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

#include <graphene/app/impacted.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>

#include <curl/curl.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/find.hpp>
#include <regex>

namespace graphene { namespace elasticsearch {

CURL *curl; // global curl handler
vector <string> bulk; // global vector of op lines

namespace detail
{


class elasticsearch_plugin_impl
{
   public:
      elasticsearch_plugin_impl(elasticsearch_plugin& _plugin)
         : _self( _plugin )
      { }
      virtual ~elasticsearch_plugin_impl();


      /** this method is called as a callback after a block is applied
       * and will process/index all operations that were applied in the block.
       */
      void update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      elasticsearch_plugin& _self;
      //flat_set<account_id_type> _tracked_accounts;
      //bool _partial_operations = false;
      primary_index< simple_index< operation_history_object > >* _oho_index;
      //uint32_t _max_ops_per_account = -1;
      std::string _elasticsearch_node_url = "http://localhost:9200/";
      uint32_t _elasticsearch_bulk_replay = 10000;
      uint32_t _elasticsearch_bulk_sync = 100;
      bool _elasticsearch_logs = true;
      bool _elasticsearch_visitor = false;
   private:
      /** add one history record, then check and remove the earliest history record */
      void add_elasticsearch( const account_id_type account_id, const operation_history_id_type op_id );

};

elasticsearch_plugin_impl::~elasticsearch_plugin_impl()
{
   return;
}

void elasticsearch_plugin_impl::update_account_histories( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   for( const optional< operation_history_object >& o_op : hist ) {
      optional <operation_history_object> oho;

      auto create_oho = [&]() {
         return optional<operation_history_object>(
               db.create<operation_history_object>([&](operation_history_object &h) {
                  if (o_op.valid())
                     h = *o_op;
               }));
      };

      if( !o_op.valid() ) {
         _oho_index->use_next_id();
         continue;
      }
      oho = create_oho();

      const operation_history_object& op = *o_op;

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      vector<authority> other;
      operation_get_required_authorities( op.op, impacted, impacted, other ); // fee_payer is added here

      if( op.op.which() == operation::tag< account_create_operation >::value )
         impacted.insert( op.result.get<object_id_type>() );
      else
         graphene::app::operation_get_impacted_accounts( op.op, impacted );

      for( auto& a : other )
         for( auto& item : a.account_auths )
            impacted.insert( item.first );

      for( auto& account_id : impacted )
      {
         add_elasticsearch( account_id, oho->id );
      }
   }
}

void elasticsearch_plugin_impl::add_elasticsearch( const account_id_type account_id, const operation_history_id_type op_id )
{
   graphene::chain::database& db = database();
   const auto &stats_obj = account_id(db).statistics(db);

   // add new entry
   const auto &ath = db.create<account_transaction_history_object>([&](account_transaction_history_object &obj) {
      obj.operation_id = op_id;
      obj.account = account_id;
      obj.sequence = stats_obj.total_ops + 1;
      obj.next = stats_obj.most_recent_op;
   });

   // keep stats growing as no op will be removed
   db.modify(stats_obj, [&](account_statistics_object &obj) {
      obj.most_recent_op = ath.id;
      obj.total_ops = ath.sequence;
   });

   // curl buffers to read
   std::string readBuffer;
   std::string readBuffer_logs;

   // ath data
   std::string account_transaction = fc::json::to_string(ath.to_variant());

   // operation history data
   std::string trx_in_block = fc::json::to_string(op_id(db).trx_in_block);
   std::string op_in_trx = fc::json::to_string(op_id(db).op_in_trx);

   std::string operation_result = fc::json::to_string(op_id(db).result);
   boost::replace_all(operation_result, "\"", "'");

   std::string virtual_op = fc::json::to_string(op_id(db).virtual_op);

   std::string op = fc::json::to_string(op_id(db).op);
   boost::replace_all(op, "\"", "'");
   boost::replace_all(op, "'''", "?");

   std::string op_type = "";
   // using the which() segfuault around block 261319 - update it not here
   if (!op_id(db).id.is_null())
      op_type = fc::json::to_string(op_id(db).op.which());

   std::string operation_history = "{\"trx_in_block\":" + trx_in_block + ",\"op_in_trx\":" + op_in_trx +
                            ",\"operation_results\":\"" + operation_result + "\",\"virtual_op\":" + virtual_op +
                            ",\"op\":\"" + op + "\"}";


   // visitor data
   std::string visitor_data = "";
   if(_elasticsearch_visitor) {
      operation_visitor o_v;
      op_id(db).op.visit(o_v);

      std::string fee_asset = fc::json::to_string(o_v.fee_asset);
      std::string fee_amount = fc::json::to_string(o_v.fee_amount);
      boost::replace_all(fee_amount, "\"", "");

      std::string fee_data = "{\"fee_asset\":" + fee_asset + ", \"fee_amount\":\"" + fee_amount + "\"}";

      std::string transfer_asset = fc::json::to_string(o_v.transfer_asset_id);
      std::string transfer_amount = fc::json::to_string(o_v.transfer_amount);
      boost::replace_all(transfer_amount, "\"", "");

      std::string transfer_data = "{\"transfer_asset_id\":" + transfer_asset + ", \"transfer_amount\":\"" + transfer_amount + "\"}";


      visitor_data = ",\"fee_data\": " + fee_data + ", \"transfer_data\": " + transfer_data;
   }

   // block data
   auto block = db.fetch_block_by_number(op_id(db).block_num);
   std::string block_num = std::to_string(block->block_num());
   std::string block_time = block->timestamp.to_iso_string();
   std::string trx_id = "";
   // removing by segfault at blok 261319,- static variant wich thing.
   //if(!block->transactions.empty() && block->transactions[op_id(db).trx_in_block].id().data_size() > 0 && block->block_num() != 261319 ) {
   //   trx_id = block->transactions[op_id(db).trx_in_block].id().str();
   //}

   std::string block_data = "{\"block_num\":" + block_num + ",\"block_time\":\"" + block_time +
                             "\",\"trx_id\":\"" + trx_id + "\"}";


   // check if we are in replay or in sync and change number of bulk documents accordingly
   uint32_t limit_documents = 0;
   if((fc::time_point::now() - block->timestamp) < fc::seconds(30))
      limit_documents = _elasticsearch_bulk_sync;
   else
      limit_documents = _elasticsearch_bulk_replay;

   //wlog((account_transaction));
   //wlog((operation_history));
   //wlog((fee_data));
   //wlog((transfer_data));
   //wlog((block_data));


   if(bulk.size() < limit_documents) { // we have everything, creating bulk array

      // put alltogether in 1 line of bulk
      std::string alltogether = "{\"account_history\": " + account_transaction + ", \"operation_history_\": " + operation_history +
                         ",\"operation_type\": " + op_type + ", \"block_data\": " + block_data + visitor_data + "}";


      // bulk header before each line, op_type = create to avoid dups, index id will be ath id(2.9.X).
      std::string _id = fc::json::to_string(ath.id);
      bulk.push_back("{ \"index\" : { \"_index\" : \"graphene\", \"_type\" : \"data\", \"op_type\" : \"create\", \"_id\" : "+_id+" } }");
      bulk.push_back(alltogether);
   }
   std::string bulking = "";
   if (curl && bulk.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech

      bulking = boost::algorithm::join(bulk, "\n");
      bulking = bulking + "\n";
      bulk.clear();

      //wlog((bulking));

      struct curl_slist *headers = NULL;

      curl_slist_append(headers, "Content-Type: application/json");

      std::string url = _elasticsearch_node_url + "_bulk";

      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_POST, true);
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bulking.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&readBuffer);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
      //curl_easy_setopt(curl, CURLOPT_VERBOSE, true);
      curl_easy_perform(curl);

      //ilog("log here curl: ${output}", ("output", readBuffer));
      if(_elasticsearch_logs) {
         auto logs = readBuffer;

         // do logs
         std::string url_logs = _elasticsearch_node_url + "logs/data/";

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
      }
   }
   else {
      //   wdump(("no curl!"));
   }

   // remove everything expect this from ath
   const auto &his_idx = db.get_index_type<account_transaction_history_index>();
   const auto &by_seq_idx = his_idx.indices().get<by_seq>();
   auto itr = by_seq_idx.lower_bound(boost::make_tuple(account_id, 0));
   if (itr != by_seq_idx.end() && itr->account == account_id && itr->id != ath.id) {
      // if found, remove the entry, and adjust account stats object
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

} // end namespace detail

elasticsearch_plugin::elasticsearch_plugin() :
   my( new detail::elasticsearch_plugin_impl(*this) )
{
   curl = curl_easy_init();
}

elasticsearch_plugin::~elasticsearch_plugin()
{
}

std::string elasticsearch_plugin::plugin_name()const
{
   return "elasticsearch";
}

void elasticsearch_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("elasticsearch-node-url", boost::program_options::value<std::string>(), "Elastic Search database node url")
         ("elasticsearch-bulk-replay", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on replay(5000)")
         ("elasticsearch-bulk-sync", boost::program_options::value<uint32_t>(), "Number of bulk documents to index on a syncronied chain(10)")
         ("elasticsearch-logs", boost::program_options::value<bool>(), "Log bulk events to database")
         ("elasticsearch-visitor", boost::program_options::value<bool>(), "Use visitor to index additional data(slows down the replay)")
         ;
   cfg.add(cli);
}

void elasticsearch_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b){ my->update_account_histories(b); } );
   my->_oho_index = database().add_index< primary_index< simple_index< operation_history_object > > >();
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
   if (options.count("elasticsearch-logs")) {
      my->_elasticsearch_logs = options["elasticsearch-logs"].as<bool>();
   }
   if (options.count("elasticsearch-visitor")) {
      my->_elasticsearch_visitor = options["elasticsearch-visitor"].as<bool>();
   }
}

void elasticsearch_plugin::plugin_startup()
{
}


} }
