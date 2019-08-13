#include <graphene/query_txid/query_txid_plugin.hpp>
#include <fc/io/fstream.hpp>
#include <graphene/chain/transaction_entry_object.hpp>
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
    uint64_t limit_batch = 1000; //limit of leveldb batch

  private:
    query_txid_plugin &_self;

    fc::signal<void()> sig_db_write;
    fc::signal<void(const uint64_t)> sig_remove;

    static leveldb::DB *leveldb;
    void consume_block();                               //Consume block
    void remove_trx_index(const uint64_t trx_entry_id); //Remove trx_index in db
};
leveldb::DB *query_txid_plugin_impl::leveldb = nullptr;

void query_txid_plugin_impl::init()
{
    try {
        //Create leveldb
        leveldb::Options options;
        options.create_if_missing = true;
        leveldb::Status s = leveldb::DB::Open(options, db_path, &leveldb);

        // Respond to the sig_db_write signale
        sig_db_write.connect([&]() { consume_block(); });
        sig_remove.connect([&](const uint64_t trx_entry_id) { remove_trx_index(trx_entry_id); });
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
        auto result = fc::raw::unpack<trx_entry_object>(data);
        return result;
    }
    FC_LOG_AND_RETHROW()
}
void query_txid_plugin_impl::collect_txid_index(const signed_block &b)
{
    try {
        graphene::chain::database &db = database();
        for (auto idx = 0; idx < b.transactions.size(); idx++) {
            db.create<trx_entry_object>([&](trx_entry_object &obj) {
                obj.txid = b.transactions[idx].id();
                obj.block_num = b.block_num();
                obj.trx_in_block = idx;
            });
        }
        sig_db_write();
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
        auto number = std::distance(itor_begin,itor_end);
        auto backupnum = number;
        auto put_index = itor_begin->id.instance();
        while (number > limit_batch) {
            leveldb::WriteBatch batch;
            auto itor_backup = itor_begin;
            for (auto idx = 0; idx < limit_batch; idx++) {
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
        //ilog("remove,${trx_ent_id},bengin: ${begin},end: ${end}",("trx_ent_id",trx_entry_id)("begin",trx_idx.begin()->id.instance())("end",trx_idx.rbegin()->id.instance()));
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

// -----------------------------------query_txid_plugin --------------------------------------

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
    cli.add_options()("query-txid-path", boost::program_options::value<std::string>(), "Save the leveldb path of the transaction history")("limit-batch", boost::program_options::value<uint64_t>(), "Number of records written to leveldb in batches");
    cfg.add(cli);
}

void query_txid_plugin::plugin_initialize(const boost::program_options::variables_map &options)
{
    try {
        ilog("query_txid plugin initialized");
        // Add the index of the trx_entry_index object table to the database
        database().add_index<primary_index<trx_entry_index>>();
        // Respond to the apply_block signal
        database().applied_block.connect([&](const signed_block &b) { my->collect_txid_index(b); });
        if (options.count("query-txid-path")) {
            my->db_path = options["query-txid-path"].as<std::string>();
            if (!fc::exists(my->db_path))
                fc::create_directories(my->db_path);
        }
        if (options.count("limit-batch")) {
            my->limit_batch = options["limit-batch"].as<uint64_t>();
        }
        // Initialize the plugin instance
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

} // namespace query_txid
} // namespace graphene