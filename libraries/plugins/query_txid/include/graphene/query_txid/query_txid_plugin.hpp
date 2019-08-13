#pragma once
#include <graphene/app/plugin.hpp>
#include <graphene/chain/transaction_entry_object.hpp>
#include <graphene/chain/database.hpp>

namespace graphene
{
namespace query_txid
{

using namespace chain;
namespace detail
{
class query_txid_plugin_impl;
}
class query_txid_plugin : public graphene::app::plugin
{
  public:
    query_txid_plugin();
    virtual ~query_txid_plugin();

    std::string plugin_name() const override;

    virtual void plugin_set_program_options(
        boost::program_options::options_description &cli,
        boost::program_options::options_description &cfg) override;

    virtual void plugin_initialize(const boost::program_options::variables_map &options) override;
    virtual void plugin_startup() override;

    static optional<trx_entry_object> query_trx_by_id(std::string txid);

    friend class detail::query_txid_plugin_impl;

    std::unique_ptr<detail::query_txid_plugin_impl> my;
};
} // namespace query_txid
} // namespace graphene
