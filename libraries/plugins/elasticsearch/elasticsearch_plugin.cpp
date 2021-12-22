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
#include <curl/curl.h>

#include <boost/algorithm/string.hpp>

namespace graphene { namespace elasticsearch {

namespace detail
{

class elasticsearch_plugin_impl
{
   public:
      explicit elasticsearch_plugin_impl(elasticsearch_plugin& _plugin)
         : _self( _plugin )
      {
         curl = curl_easy_init();
         curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
      }
      virtual ~elasticsearch_plugin_impl();

      bool update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      friend class graphene::elasticsearch::elasticsearch_plugin;

   private:
      elasticsearch_plugin& _self;
      primary_index< operation_history_index >* _oho_index;

      std::string _elasticsearch_node_url = "http://localhost:9200/";
      uint32_t _elasticsearch_bulk_replay = 10000;
      uint32_t _elasticsearch_bulk_sync = 100;
      bool _elasticsearch_visitor = false;
      std::string _elasticsearch_basic_auth = "";
      std::string _elasticsearch_index_prefix = "bitshares-";
      bool _elasticsearch_operation_object = true;
      uint32_t _elasticsearch_start_es_after_block = 0;
      bool _elasticsearch_operation_string = false;
      mode _elasticsearch_mode = mode::only_save;
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
      bool is_es_version_7_or_above = true;

      bool add_elasticsearch( const account_id_type account_id, const optional<operation_history_object>& oho,
                              const uint32_t block_number );
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
      void init_program_options(const boost::program_options::variables_map& options);
};

elasticsearch_plugin_impl::~elasticsearch_plugin_impl()
{
   if (curl) {
      curl_easy_cleanup(curl);
      curl = nullptr;
   }
}

static std::string generateIndexName( const fc::time_point_sec& block_date,
                                      const std::string& _elasticsearch_index_prefix )
{
   auto block_date_string = block_date.to_iso_string();
   std::vector<std::string> parts;
   boost::split(parts, block_date_string, boost::is_any_of("-"));
   std::string index_name = _elasticsearch_index_prefix + parts[0] + "-" + parts[1];
   return index_name;
}

bool elasticsearch_plugin_impl::update_account_histories( const signed_block& b )
{
   checkState(b.timestamp);
   index_name = generateIndexName(b.timestamp, _elasticsearch_index_prefix);

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
      // fee_payer is added here
      operation_get_required_authorities( op.op, impacted, impacted, other,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ) );

      if( op.op.is_type< account_create_operation >() )
         impacted.insert( op.result.get<object_id_type>() );

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

      for( auto& a : other )
         for( auto& item : a.account_auths )
            impacted.insert( item.first );

      for( auto& account_id : impacted )
      {
         if(!add_elasticsearch( account_id, oho, b.block_num() ))
         {
            elog( "Error adding data to Elastic Search: block num ${b}, account ${a}, data ${d}",
                  ("b",b.block_num()) ("a",account_id) ("d", oho) );
            return false;
         }
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
         {
            // Note: although called with `std::move()`, `es` is not updated in `SendBulk()`
            elog( "Error sending ${n} lines of bulk data to Elastic Search, the first lines are:",
                  ("n",es.bulk_lines.size()) );
            for( size_t i = 0; i < es.bulk_lines.size() && i < 10; ++i )
            {
               edump( (es.bulk_lines[i]) );
            }
            return false;
         }
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

struct es_data_adaptor {
   enum class data_type {
      static_variant_type,
      map_type,
      array_type // can be simple arrays, object arrays, static_variant arrays, or even nested arrays
   };
   static variant adapt(const variant_object& op)
   {
      fc::mutable_variant_object o(op);

      map<string, data_type> to_string_fields = {
         { "parameters",               data_type::array_type }, // in committee proposals, current_fees.parameters
         { "op",                       data_type::static_variant_type }, // proposal_create_op.proposed_ops[*].op
         { "proposed_ops",             data_type::array_type },
         { "initializer",              data_type::static_variant_type },
         { "policy",                   data_type::static_variant_type },
         { "predicates",               data_type::array_type },
         { "active_special_authority", data_type::static_variant_type },
         { "owner_special_authority",  data_type::static_variant_type },
         { "acceptable_collateral",    data_type::map_type },
         { "acceptable_borrowers",     data_type::map_type }
      };
      map<string, fc::variants> original_arrays;
      vector<string> keys_to_rename;
      for (auto i = o.begin(); i != o.end(); ++i)
      {
         const string& name = (*i).key();
         auto& element = (*i).value();
         if (element.is_object())
         {
            auto& vo = element.get_object();
            if (vo.contains(name.c_str())) // transfer_operation.amount.amount
               keys_to_rename.emplace_back(name);
            element = adapt(vo);
         }
         else if (element.is_array())
         {
            auto& array = element.get_array();
            if( to_string_fields.find(name) != to_string_fields.end() )
            {
               // make a backup and convert to string
               original_arrays[name] = array;
               element = fc::json::to_string(element);
            }
            else
               adapt(array);
         }
      }

      for( const auto& i : keys_to_rename ) // transfer_operation.amount
      {
         string new_name = i + "_";
         o[new_name] = variant(o[i]);
         o.erase(i);
      }

      if( o.find("memo") != o.end() )
      {
         auto& memo = o["memo"];
         if (memo.is_object())
         {
            fc::mutable_variant_object tmp(memo.get_object());
            if (tmp.find("nonce") != tmp.end())
            {
               tmp["nonce"] = tmp["nonce"].as_string();
               o["memo"] = tmp;
            }
         }
      }

      if( o.find("owner") != o.end() && o["owner"].is_string() ) // vesting_balance_*_operation.owner
      {
         o["owner_"] = o["owner"].as_string();
         o.erase("owner");
      }

      for( const auto& pair : original_arrays )
      {
         const auto& name = pair.first;
         auto& value = pair.second;
         auto type = to_string_fields[name];
         o[name + "_object"] = adapt( value, type );
      }

      variant v;
      fc::to_variant(o, v, FC_PACK_MAX_DEPTH);
      return v;
   }

   static variant adapt( const fc::variants& v, data_type type )
   {
      if( data_type::static_variant_type == type )
         return adapt_static_variant(v);

      // map_type or array_type
      fc::variants vs;
      vs.reserve( v.size() );
      for( const auto& item : v )
      {
         if( item.is_array() )
         {
            if( data_type::map_type == type )
               vs.push_back( adapt_map_item( item.get_array() ) );
            else // assume it is a static_variant array
               vs.push_back( adapt_static_variant( item.get_array() ) );
         }
         else if( item.is_object() ) // object array
            vs.push_back( adapt( item.get_object() ) );
         else
            wlog( "Type of item is unexpected: ${item}", ("item", item) );
      }

      variant nv;
      fc::to_variant(vs, nv, FC_PACK_MAX_DEPTH);
      return nv;
   }

   static void extract_data_from_variant( const variant& v, fc::mutable_variant_object& mv, const string& prefix )
   {
      if( v.is_object() )
         mv[prefix + "_object"] = adapt( v.get_object() );
      else if( v.is_int64() || v.is_uint64() )
         mv[prefix + "_int"] = v;
      else if( v.is_bool() )
         mv[prefix + "_bool"] = v;
      else
         mv[prefix + "_string"] = fc::json::to_string( v );
      // Note: we don't use double or array here, and we convert null and blob to string
   }

   static variant adapt_map_item( const fc::variants& v )
   {
      FC_ASSERT( v.size() == 2, "Internal error" );
      fc::mutable_variant_object mv;

      extract_data_from_variant( v[0], mv, "key" );
      extract_data_from_variant( v[1], mv, "data" );

      variant nv;
      fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
      return nv;
   }

   static variant adapt_static_variant( const fc::variants& v )
   {
      FC_ASSERT( v.size() == 2, "Internal error" );
      fc::mutable_variant_object mv;

      mv["which"] = v[0];
      extract_data_from_variant( v[1], mv, "data" );

      variant nv;
      fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
      return nv;
   }

   static void adapt(fc::variants& v)
   {
      for (auto& array_element : v)
      {
         if (array_element.is_object())
            array_element = adapt(array_element.get_object());
         else if (array_element.is_array())
            adapt(array_element.get_array());
         else
            array_element = array_element.as_string();
      }
   }
};

void elasticsearch_plugin_impl::doOperationHistory(const optional <operation_history_object>& oho)
{ try {
   os.trx_in_block = oho->trx_in_block;
   os.op_in_trx = oho->op_in_trx;
   os.operation_result = fc::json::to_string(oho->result);
   os.virtual_op = oho->virtual_op;

   if(_elasticsearch_operation_object) {
      // op
      oho->op.visit(fc::from_static_variant(os.op_object, FC_PACK_MAX_DEPTH));
      os.op_object = es_data_adaptor::adapt(os.op_object.get_object());
      // operation_result
      variant v;
      fc::to_variant( oho->result, v, FC_PACK_MAX_DEPTH );
      os.operation_result_object = es_data_adaptor::adapt_static_variant( v.get_array() );
   }
   if(_elasticsearch_operation_string)
      os.op = fc::json::to_string(oho->op);
} FC_CAPTURE_LOG_AND_RETHROW( (oho) ) }

void elasticsearch_plugin_impl::doBlock(uint32_t trx_in_block, const signed_block& b)
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
   typedef void result_type;

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

bool elasticsearch_plugin_impl::add_elasticsearch( const account_id_type account_id,
                                                   const optional <operation_history_object>& oho,
                                                   const uint32_t block_number)
{
   const auto &stats_obj = getStatsObject(account_id);
   const auto &ath = addNewEntry(stats_obj, account_id, oho);
   growStats(stats_obj, ath);

   if(block_number > _elasticsearch_start_es_after_block)  {
      createBulkLine(ath);
      prepareBulk(ath.id);
   }
   cleanObjects(ath.id, account_id);

   if (curl && bulk_lines.size() >= limit_documents) { // we are in bulk time, ready to add data to elasticsearech
      prepare.clear();
      populateESstruct();
      if(!graphene::utilities::SendBulk(std::move(es)))
      {
         // Note: although called with `std::move()`, `es` is not updated in `SendBulk()`
         elog( "Error sending ${n} lines of bulk data to Elastic Search, the first lines are:",
               ("n",es.bulk_lines.size()) );
         for( size_t i = 0; i < es.bulk_lines.size() && i < 10; ++i )
         {
            edump( (es.bulk_lines[i]) );
         }
         return false;
      }
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

const account_transaction_history_object& elasticsearch_plugin_impl::addNewEntry(
      const account_statistics_object& stats_obj,
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
   if(!is_es_version_7_or_above)
      bulk_header["_type"] = "_doc";
   bulk_header["_id"] = fc::to_string(ath_id.space_id) + "." + fc::to_string(ath_id.type_id) + "."
                      + fc::to_string(ath_id.instance.value);
   prepare = graphene::utilities::createBulk(bulk_header, std::move(bulk_line));
   std::move(prepare.begin(), prepare.end(), std::back_inserter(bulk_lines));
   prepare.clear();
}

void elasticsearch_plugin_impl::cleanObjects( const account_transaction_history_id_type& ath_id,
                                              const account_id_type& account_id )
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
         ("elasticsearch-bulk-replay", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on replay(10000)")
         ("elasticsearch-bulk-sync", boost::program_options::value<uint32_t>(),
               "Number of bulk documents to index on a syncronied chain(100)")
         ("elasticsearch-visitor", boost::program_options::value<bool>(),
               "Use visitor to index additional data(slows down the replay(false))")
         ("elasticsearch-basic-auth", boost::program_options::value<std::string>(),
               "Pass basic auth to elasticsearch database('')")
         ("elasticsearch-index-prefix", boost::program_options::value<std::string>(),
               "Add a prefix to the index(bitshares-)")
         ("elasticsearch-operation-object", boost::program_options::value<bool>(),
               "Save operation as object(true)")
         ("elasticsearch-start-es-after-block", boost::program_options::value<uint32_t>(),
               "Start doing ES job after block(0)")
         ("elasticsearch-operation-string", boost::program_options::value<bool>(),
               "Save operation as string. Needed to serve history api calls(false)")
         ("elasticsearch-mode", boost::program_options::value<uint16_t>(),
               "Mode of operation: only_save(0), only_query(1), all(2) - Default: 0")
         ;
   cfg.add(cli);
}

void detail::elasticsearch_plugin_impl::init_program_options(const boost::program_options::variables_map& options)
{
   if (options.count("elasticsearch-node-url") > 0) {
      _elasticsearch_node_url = options["elasticsearch-node-url"].as<std::string>();
   }
   if (options.count("elasticsearch-bulk-replay") > 0) {
      _elasticsearch_bulk_replay = options["elasticsearch-bulk-replay"].as<uint32_t>();
   }
   if (options.count("elasticsearch-bulk-sync") > 0) {
      _elasticsearch_bulk_sync = options["elasticsearch-bulk-sync"].as<uint32_t>();
   }
   if (options.count("elasticsearch-visitor") > 0) {
      _elasticsearch_visitor = options["elasticsearch-visitor"].as<bool>();
   }
   if (options.count("elasticsearch-basic-auth") > 0) {
      _elasticsearch_basic_auth = options["elasticsearch-basic-auth"].as<std::string>();
   }
   if (options.count("elasticsearch-index-prefix") > 0) {
      _elasticsearch_index_prefix = options["elasticsearch-index-prefix"].as<std::string>();
   }
   if (options.count("elasticsearch-operation-object") > 0) {
      _elasticsearch_operation_object = options["elasticsearch-operation-object"].as<bool>();
   }
   if (options.count("elasticsearch-start-es-after-block") > 0) {
      _elasticsearch_start_es_after_block = options["elasticsearch-start-es-after-block"].as<uint32_t>();
   }
   if (options.count("elasticsearch-operation-string") > 0) {
      _elasticsearch_operation_string = options["elasticsearch-operation-string"].as<bool>();
   }
   if (options.count("elasticsearch-mode") > 0) {
      const auto option_number = options["elasticsearch-mode"].as<uint16_t>();
      if(option_number > mode::all)
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception, "Elasticsearch mode not valid");
      _elasticsearch_mode = static_cast<mode>(options["elasticsearch-mode"].as<uint16_t>());
   }
}

void elasticsearch_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->_oho_index = database().add_index< primary_index< operation_history_index > >();
   database().add_index< primary_index< account_transaction_history_index > >();

   my->init_program_options( options );

   if(my->_elasticsearch_mode != mode::only_query) {
      if (my->_elasticsearch_mode == mode::all && !my->_elasticsearch_operation_string)
         FC_THROW_EXCEPTION(graphene::chain::plugin_exception,
               "If elasticsearch-mode is set to all then elasticsearch-operation-string need to be true");

      database().applied_block.connect([this](const signed_block &b) {
         if (!my->update_account_histories(b))
            FC_THROW_EXCEPTION(graphene::chain::plugin_exception,
                  "Error populating ES database, we are going to keep trying.");
      });
   }

   graphene::utilities::ES es;
   es.curl = my->curl;
   es.elasticsearch_url = my->_elasticsearch_node_url;
   es.auth = my->_elasticsearch_basic_auth;

   if(!graphene::utilities::checkES(es))
      FC_THROW( "ES database is not up in url ${url}", ("url", my->_elasticsearch_node_url) );

   graphene::utilities::checkESVersion7OrAbove(es, my->is_es_version_7_or_above);
}

void elasticsearch_plugin::plugin_startup()
{
   // Nothing to do
}

operation_history_object elasticsearch_plugin::get_operation_by_id(operation_history_id_type id)
{
   const string operation_id_string = std::string(object_id_type(id));

   const string query = R"(
   {
      "query": {
         "match":
         {
            "account_history.operation_id": )" + operation_id_string + R"("
         }
      }
   }
   )";

   auto es = prepareHistoryQuery(query);
   const auto response = graphene::utilities::simpleQuery(es);
   variant variant_response = fc::json::from_string(response);
   const auto source = variant_response["hits"]["hits"][size_t(0)]["_source"];
   return fromEStoOperation(source);
}

vector<operation_history_object> elasticsearch_plugin::get_account_history(
      const account_id_type account_id,
      operation_history_id_type stop = operation_history_id_type(),
      unsigned limit = 100,
      operation_history_id_type start = operation_history_id_type())
{
   const string account_id_string = std::string(object_id_type(account_id));

   const auto stop_number = stop.instance.value;
   const auto start_number = start.instance.value;

   string range = "";
   if(stop_number == 0)
      range = " AND operation_id_num: ["+fc::to_string(stop_number)+" TO "+fc::to_string(start_number)+"]";
   else if(stop_number > 0)
      range = " AND operation_id_num: {"+fc::to_string(stop_number)+" TO "+fc::to_string(start_number)+"]";

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

   auto es = prepareHistoryQuery(query);

   vector<operation_history_object> result;

   if(!graphene::utilities::checkES(es))
      return result;

   const auto response = graphene::utilities::simpleQuery(es);
   variant variant_response = fc::json::from_string(response);

   const auto hits = variant_response["hits"]["total"];
   uint32_t size;
   if( hits.is_object() ) // ES-7 ?
      size = static_cast<uint32_t>(hits["value"].as_uint64());
   else // probably ES-6
      size = static_cast<uint32_t>(hits.as_uint64());
   size = std::min( size, limit );

   for(unsigned i=0; i<size; i++)
   {
      const auto source = variant_response["hits"]["hits"][size_t(i)]["_source"];
      result.push_back(fromEStoOperation(source));
   }
   return result;
}

operation_history_object elasticsearch_plugin::fromEStoOperation(variant source)
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

   return result;
}

graphene::utilities::ES elasticsearch_plugin::prepareHistoryQuery(string query)
{
   CURL *curl;
   curl = curl_easy_init();
   curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

   graphene::utilities::ES es;
   es.curl = curl;
   es.elasticsearch_url = my->_elasticsearch_node_url;
   es.index_prefix = my->_elasticsearch_index_prefix;
   es.endpoint = es.index_prefix + "*/_doc/_search";
   es.query = query;

   return es;
}

mode elasticsearch_plugin::get_running_mode()
{
   return my->_elasticsearch_mode;
}

} }
