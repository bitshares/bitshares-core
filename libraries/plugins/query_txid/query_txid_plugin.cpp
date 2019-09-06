/*
 * Copyright (c) 2019 GXChain and zhaoxiangfei、bijianing97 .
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
#include <graphene/query_txid/query_txid_plugin.hpp>
#include <fc/io/fstream.hpp>
#include <graphene/query_txid_object/transaction_entry_object.hpp>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <fc/signals.hpp>

namespace graphene{ namespace query_txid{

namespace detail
{
using namespace leveldb;
class query_txid_plugin_impl
{
   public:
      query_txid_plugin_impl(query_txid_plugin &_plugin)
         : _self(_plugin)
      {
      }
      ~query_txid_plugin_impl()
      {
      }

      void collect_txid_index(const signed_block &b);

      graphene::chain::database &database()
      {
       return _self.database();
      }
      void init();
      static optional<trx_entry_object> query_trx_by_id(std::string txid);
      std::string db_path = "trx_entry.db";
      uint64_t limit_batch = 1000; 
   private:
      query_txid_plugin &_self;
      fc::signal<void(const uint64_t)> sig_remove;

      static leveldb::DB *leveldb;
      void consume_block();                         
      void remove_trx_index(const uint64_t trx_entry_id); 
};
leveldb::DB *query_txid_plugin_impl::leveldb = nullptr;

void query_txid_plugin_impl::init()
{
   try {
      leveldb::Options options;
      options.create_if_missing = true;
      db_path = database().get_data_dir().string() + db_path;
      leveldb::Status s = leveldb::DB::Open(options, db_path, &leveldb);
      sig_remove.connect([this](const uint64_t trx_entry_id) { remove_trx_index(trx_entry_id); });
   }
   FC_LOG_AND_RETHROW()
}
optional<trx_entry_object> query_txid_plugin_impl::query_trx_by_id(std::string txid)
{
   try {
      if (leveldb == nullptr) return optional<trx_entry_object>();
      std::string value;
      leveldb::Status s = leveldb->Get(leveldb::ReadOptions(), txid, &value);
      if (!s.ok()) return optional<trx_entry_object>();
      std::vector<char> data(value.begin(), value.end());
      return fc::raw::unpack<trx_entry_object>(data);
   }
   FC_LOG_AND_RETHROW()
}
void query_txid_plugin_impl::collect_txid_index(const signed_block &b)
{
   try {
      graphene::chain::database &db = database();
      for (unsigned int idx = 0; idx < b.transactions.size(); idx++) {
         db.create<trx_entry_object>([&b,&idx](trx_entry_object &obj) {
            obj.txid = b.transactions[idx].id();
            obj.block_num = b.block_num();
            obj.trx_in_block = idx;
        });
      }
      consume_block();
   }
   FC_LOG_AND_RETHROW()
}
void query_txid_plugin_impl::consume_block()
{
   try {
      graphene::chain::database &db = database();
      const auto &dpo = db.get_dynamic_global_properties();
      uint64_t irr_num = dpo.last_irreversible_block_num;

      const auto &trx_idx = db.get_index_type<trx_entry_index>().indices();
      const auto &trx_bn_idx = trx_idx.get<by_blocknum>();
      if (trx_idx.begin() == trx_idx.end()) return;
      auto itor_begin = trx_bn_idx.begin();
      auto itor_end = trx_bn_idx.lower_bound(irr_num);
      uint64_t number = std::distance(itor_begin,itor_end);
      uint64_t backupnum = number;
      auto put_index = itor_begin->id.instance();
      while (number > limit_batch) {
         leveldb::WriteBatch batch;
         auto itor_backup = itor_begin;
         for (uint64_t idx = 0; idx < limit_batch; idx++) {
            auto serialize = fc::raw::pack(*itor_begin);
            std::string txid(itor_begin->txid);
            batch.Put(txid, {serialize.data(), serialize.size()});
            put_index = itor_begin->id.instance();
            itor_begin++;
            if (itor_begin == itor_end) break;
         }
         leveldb::WriteOptions write_options;
         write_options.sync = true;
         Status s = leveldb->Write(write_options, &batch);
         if (!s.ok()) {
            itor_begin = itor_backup;
            put_index = itor_begin->id.instance();
            break;
         }
         number -= limit_batch;
      }
      if (backupnum > limit_batch)
         sig_remove(put_index);
   }
   FC_LOG_AND_RETHROW()
}
void query_txid_plugin_impl::remove_trx_index(const uint64_t trx_entry_id)
{
   try {
      graphene::chain::database &db = database();
      const auto &trx_idx = db.get_index_type<trx_entry_index>().indices();
      for (auto itor = trx_idx.begin(); itor != trx_idx.end();) {
         auto backup_itr = itor;
         ++itor;
         if (itor->id.instance() < trx_entry_id) {
            db.remove(*backup_itr);
         } else {
            break;
         }
      }
   }
   FC_LOG_AND_RETHROW()
}
} // namespace detail

query_txid_plugin::query_txid_plugin()
   : my(new detail::query_txid_plugin_impl(*this))
{
}

query_txid_plugin::~query_txid_plugin()
{
}

std::string query_txid_plugin::plugin_name() const
{
   return "query_txid";
}

void query_txid_plugin::plugin_set_program_options(
   boost::program_options::options_description &cli,
   boost::program_options::options_description &cfg)
{
   cli.add_options()("query-txid-path", boost::program_options::value<std::string>(), 
   "Save the leveldb path of the transaction history")
   ("limit-batch", boost::program_options::value<uint64_t>(),
   "Number of records written to leveldb in batches");
   cfg.add(cli);
}

void query_txid_plugin::plugin_initialize(const boost::program_options::variables_map &options)
{
   try {
      ilog("query_txid plugin initialized");
      database().add_index<primary_index<trx_entry_index>>();
      database().applied_block.connect([this](const signed_block &b) { my->collect_txid_index(b); });
      if (options.count("query-txid-path")) {
         my->db_path = options["query-txid-path"].as<std::string>();
         if (!fc::exists(my->db_path))
            fc::create_directories(my->db_path);
      }
      if (options.count("limit-batch")) {
         my->limit_batch = options["limit-batch"].as<uint64_t>();
      }
      my->init();
   }
   FC_LOG_AND_RETHROW()
}

void query_txid_plugin::plugin_startup()
{
}

optional<trx_entry_object> query_txid_plugin::query_trx_by_id(std::string txid)
{
   return detail::query_txid_plugin_impl::query_trx_by_id(txid);
}

} } // graphene::query_txid
