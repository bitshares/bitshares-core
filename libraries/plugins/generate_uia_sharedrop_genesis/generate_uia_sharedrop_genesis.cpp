/*
 * Copyright (c) 2017 PeerPlays Blockchain Standards Association
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
#include <graphene/generate_uia_sharedrop_genesis/generate_uia_sharedrop_genesis.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/genesis_state.hpp>
#include <graphene/chain/witness_object.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>

#include <graphene/chain/market_object.hpp>

#include <iostream>
#include <fstream>

using namespace graphene::generate_uia_sharedrop_genesis;
using std::string;
using std::vector;

namespace bpo = boost::program_options;

void generate_uia_sharedrop_genesis_plugin::plugin_set_program_options(boost::program_options::options_description& command_line_options,
                                                                       boost::program_options::options_description& config_file_options)
{
    command_line_options.add_options()
        ("input-uia-sharedrop-genesis-file", bpo::value<std::string>()->default_value("genesis.json"), "Genesis file to read")
        ("output-uia-sharedrop-genesis-file", bpo::value<std::string>()->default_value("genesis.json"), "Genesis file to create")
        ("output-uia-sharedrop-csvlog-file", bpo::value<std::string>()->default_value("log.csv"), "CSV log file to create")
        ("sharedrop-asset", bpo::value<std::string>()->default_value("BTS"), "The Asset to sharedrop on")
        ("uia-sharedrop-snapshot-block-number", bpo::value<uint32_t>()->default_value(1000), "Block number at which to snapshot balances")
        ("exclude-accounts", bpo::value<vector<string>>()->default_value(
               std::vector<std::string>({
                  /// Scam accounts
                  "polonie-wallet",
                  "polonie-xwallet",
                  "poloniewallet",
                  "poloniex-deposit",
                  "poloniex-wallet",
                  "poloniexwall-et",
                  "poloniexwallett",
                  "poloniexwall-t",
                  "poloniexwalle",
                  "poloniex",
                  "poloneix",
                  "poloniex1",
                  "bittrex-deopsit",
                  "bittrex-deposi",
                  "bittrex-depositt",
                  "bittrex-dposit",
                  "bittrex",
                  "bittrex-deposits",
                  "coinbase",
                  "blocktrade",
                  "locktrades",
                  "yun.bts",
                  "transwiser-walle",
                  "transwiser-wallets",
                  "ranswiser-wallet",
                  "yun.btc",
                  "pay.coinbase.com",
                  "pay.bts.com",
                  "btc38.com",
                  "yunbi.com",
                  "coinbase.com",
                  "ripple.com",
                  /// Exchange accounts
                  "poloniexcoldstorage",
                  "btc38-public-for-bts-cold",
                  "poloniexwallet",
                  "btercom",
                  "yunbi-cold-wallet",
                  "btc38-btsx-octo-72722",
                  "bittrex-deposit",
                  "btc38btsxwithdrawal"
               }),
               "['poloneix', '...'] - Known scam and exchange accounts"
            )->composing(), "Exclude this list of accounts");
        ;
    config_file_options.add(command_line_options);
}

std::string generate_uia_sharedrop_genesis_plugin::plugin_name()const
{
    return "generate_uia_sharedrop_genesis";
}

void generate_uia_sharedrop_genesis_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
        ilog("generate uia sharedrop genesis plugin:  plugin_initialize() begin");
        _options = &options;

        _output_genesis_filename = options["output-uia-sharedrop-genesis-file"].as<std::string>();
        _input_genesis_filename = options["input-uia-sharedrop-genesis-file"].as<std::string>();
        _csvlog_filename = options["output-uia-sharedrop-csvlog-file"].as<std::string>();
        if (options.count("uia-sharedrop-snapshot-block-number"))
            _block_to_snapshot = options["uia-sharedrop-snapshot-block-number"].as<uint32_t>();
        database().applied_block.connect([this](const graphene::chain::signed_block& b){ block_applied(b); });
        ilog("generate uia sharedrop genesis plugin:  plugin_initialize() end");
    } FC_LOG_AND_RETHROW() }

void generate_uia_sharedrop_genesis_plugin::plugin_startup()
{ try {
        ilog("generate uia sharedrop genesis plugin:  plugin_startup() begin");
        if (_block_to_snapshot)
        {
            chain::database& d = database();
            if (d.head_block_num() == *_block_to_snapshot)
            {
                ilog("generate uia sharedrop genesis plugin: already at snapshot block");
                generate_snapshot();
            }
            else if (d.head_block_num() > *_block_to_snapshot)
                elog("generate uia sharedrop genesis plugin: already passed snapshot block, you must reindex to return to the snapshot state");
            else
                elog("generate uia sharedrop genesis plugin: waiting for block ${snapshot_block} to generate snapshot, current head is ${head}",
                     ("snapshot_block", _block_to_snapshot)("head", d.head_block_num()));
        }
        else
            ilog("generate uia sharedrop genesis plugin: no snapshot block number provided, plugin is disabled");
        ilog("generate uia sharedrop genesis plugin:  plugin_startup() end");
    } FC_CAPTURE_AND_RETHROW() }

void generate_uia_sharedrop_genesis_plugin::block_applied(const graphene::chain::signed_block& b)
{
    if (_block_to_snapshot && b.block_num() == *_block_to_snapshot)
    {
        ilog("generate uia sharedrop genesis plugin: snapshot block has arrived");
        generate_snapshot();
    }
}

// anonymous namespace for file-scoped helper functions
std::string modify_account_name(const std::string& name)
{
    return std::string("bts-") + name;
}

std::string unmodify_account_name(const std::string& name)
{
    FC_ASSERT(name.substr(0, 4) == "bts-");
    return name.substr(4);
}

bool is_special_account(const graphene::chain::account_id_type& account_id)
{
    return account_id.instance < 100;
}

bool generate_uia_sharedrop_genesis_plugin::is_excluded_account(const std::string& account_name)
{
    if( _options.count("exclude-account") )
    {
        auto exclude_accounts = _options.at("exclude-account").as<std::set<std::string>>();
        return exclude_accounts.find(account_name) != exclude_accounts.end();
    }
    return false;
}

void generate_uia_sharedrop_genesis_plugin::generate_snapshot()
{
    ilog("generate genesis plugin: generating snapshot now");
    chain::database& d = database();

    // Lookup the ID of the UIA we will be sharedropping on
    std::string uia_symbol(_options.at("sharedrop-asset").as<std::string>());
    const auto& assets_by_symbol = d.get_index_type<graphene::chain::asset_index>().indices().get<graphene::chain::by_symbol>();
    auto itr = assets_by_symbol.find(uia_symbol);
    FC_ASSERT(itr != assets_by_symbol.end(), "Unable to find asset named ${uia_symbol}", ("uia_symbol", uia_symbol));
    graphene::chain::asset_id_type uia_id = itr->get_id();
    ilog("Scanning for all balances of asset ${uia_symbol} (${uia_id})", ("uia_symbol", uia_symbol)("uia_id", uia_id));

    uia_sharedrop_balance_object_index_type sharedrop_balances;

    // load the balances from the input genesis file, if any
    graphene::chain::genesis_state_type new_genesis_state;
    if (!_input_genesis_filename.empty())
    {
        new_genesis_state = fc::json::from_file<graphene::chain::genesis_state_type>(_input_genesis_filename);
        for (const graphene::chain::genesis_state_type::initial_bts_account_type& initial_bts_account : new_genesis_state.initial_bts_accounts)
        {
            std::string account_name = unmodify_account_name(initial_bts_account.name);
            auto& account_by_name_index = d.get_index_type<graphene::chain::account_index>().indices().get<graphene::chain::by_name>();
            auto account_iter = account_by_name_index.find(account_name);
            FC_ASSERT(account_iter != account_by_name_index.end(), "No account ${name}", ("name", account_name));
            uia_sharedrop_balance_object balance_object;
            balance_object.account_id = account_iter->id;
            balance_object.genesis = initial_bts_account.core_balance;
            sharedrop_balances.insert(balance_object);
            ilog("Loaded genesis balance for ${name}: ${balance}", ("name", account_name)("balance", initial_bts_account.core_balance));
        }
    }
    new_genesis_state.initial_bts_accounts.clear();

    auto& balance_index = d.get_index_type<graphene::chain::account_balance_index>().indices().get<graphene::chain::by_asset_balance>();
    for (auto balance_iter = balance_index.begin(); balance_iter != balance_index.end(); ++balance_iter)
        if (balance_iter->asset_type == uia_id && balance_iter->balance != graphene::chain::share_type())
        {
            if (is_special_account(balance_iter->owner) || is_excluded_account(balance_iter->owner(d).name) )
            {
                ilog("skipping balance in ${account_id} because special or exchange", ("account_id", balance_iter->owner));
            }
            else
            {
                auto sharedrop_balance_iter = sharedrop_balances.find(balance_iter->owner);
                if (sharedrop_balance_iter == sharedrop_balances.end())
                {
                    uia_sharedrop_balance_object balance_object;
                    balance_object.account_id = balance_iter->owner;
                    balance_object.balance = balance_iter->balance;
                    sharedrop_balances.insert(balance_object);
                }
                else
                {
                    sharedrop_balances.modify(sharedrop_balance_iter, [&](uia_sharedrop_balance_object& balance_object) {
                        balance_object.balance = balance_iter->balance;
                    });
                }
            }
        }

    // scan for sharedrop-asset tied up in market orders
    auto& limit_order_index = d.get_index_type<graphene::chain::limit_order_index>().indices().get<graphene::chain::by_account>();
    for (auto limit_order_iter = limit_order_index.begin(); limit_order_iter != limit_order_index.end(); ++limit_order_iter)
    {
        if (limit_order_iter->sell_price.base.asset_id == uia_id)
        {
            if (is_special_account(limit_order_iter->seller) || is_excluded_account(limit_order_iter->seller(d).name))
                ilog("Skipping account ${name} because special/scam/exchange", ("name", limit_order_iter->seller(d).name));
            else
            {
                auto sharedrop_balance_iter = sharedrop_balances.find(limit_order_iter->seller);
                if (sharedrop_balance_iter == sharedrop_balances.end())
                {
                    //ilog("found order for new account ${account_id}", ("account_id", limit_order_iter->seller));
                    uia_sharedrop_balance_object balance_object;
                    balance_object.account_id = limit_order_iter->seller;
                    balance_object.orders = limit_order_iter->for_sale;
                    sharedrop_balances.insert(balance_object);
                }
                else
                {
                    //ilog("found order for existing account ${account_id}", ("account_id", limit_order_iter->seller));
                    sharedrop_balances.modify(sharedrop_balance_iter, [&](uia_sharedrop_balance_object& balance_object) {
                        balance_object.orders += limit_order_iter->for_sale;
                    });
                }
            }
        }
    }

    // compute the sharedrop
    for (auto sharedrop_balance_iter = sharedrop_balances.begin(); sharedrop_balance_iter != sharedrop_balances.end();)
    {
        auto this_iter = sharedrop_balance_iter;
        ++sharedrop_balance_iter;
        sharedrop_balances.modify(this_iter, [&](uia_sharedrop_balance_object& balance_object) {
            balance_object.sharedrop = balance_object.genesis + (balance_object.balance + balance_object.orders) * 10;
        });
    }


    // Generate CSV file of all sharedrops and the balances we used to calculate them 
    std::ofstream csv_log_file;
    csv_log_file.open(_csvlog_filename);
    assert(csv_log_file.is_open());
    csv_log_file << "name,genesis,balance,orders,sharedrop\n";
    for (const uia_sharedrop_balance_object& balance_object : sharedrop_balances)
        csv_log_file << balance_object.account_id(d).name << "," << balance_object.genesis.value << "," << balance_object.balance.value << "," << balance_object.orders.value << "," << balance_object.sharedrop.value << "\n";
    ilog("CSV log written to file ${filename}", ("filename", _csvlog_filename));
    csv_log_file.close();

    //auto& account_index = d.get_index_type<graphene::chain::account_index>();
    //auto& account_by_id_index = account_index.indices().get<graphene::chain::by_id>();
    // inefficient way of crawling the graph, but we only do it once
    std::set<graphene::chain::account_id_type> already_generated;
    for (;;)
    {
        unsigned accounts_generated_this_round = 0;
        for (const uia_sharedrop_balance_object& balance_object : sharedrop_balances)
        {
            const graphene::chain::account_id_type& account_id = balance_object.account_id;
            const graphene::chain::share_type& sharedrop_amount = balance_object.sharedrop;
            const graphene::chain::account_object& account_obj = account_id(d);
            if (already_generated.find(account_id) == already_generated.end())
            {
                graphene::chain::genesis_state_type::initial_bts_account_type::initial_authority owner;
                owner.weight_threshold = account_obj.owner.weight_threshold;
                owner.key_auths = account_obj.owner.key_auths;
                for (const auto& value : account_obj.owner.account_auths)
                {
                    owner.account_auths.insert(std::make_pair(modify_account_name(value.first(d).name), value.second));
                    auto owner_balance_iter = sharedrop_balances.find(value.first);
                    if (owner_balance_iter == sharedrop_balances.end())
                    {
                        uia_sharedrop_balance_object balance_object;
                        balance_object.account_id = value.first;
                        sharedrop_balances.insert(balance_object);
                    }
                }
                owner.key_auths = account_obj.owner.key_auths;
                owner.address_auths = account_obj.owner.address_auths;

                graphene::chain::genesis_state_type::initial_bts_account_type::initial_authority active;
                active.weight_threshold = account_obj.active.weight_threshold;
                active.key_auths = account_obj.active.key_auths;
                for (const auto& value : account_obj.active.account_auths)
                {
                    active.account_auths.insert(std::make_pair(modify_account_name(value.first(d).name), value.second));
                    auto active_balance_iter = sharedrop_balances.find(value.first);
                    if (active_balance_iter == sharedrop_balances.end())
                    {
                        uia_sharedrop_balance_object balance_object;
                        balance_object.account_id = value.first;
                        sharedrop_balances.insert(balance_object);
                    }
                }
                active.key_auths = account_obj.active.key_auths;
                active.address_auths = account_obj.active.address_auths;

                new_genesis_state.initial_bts_accounts.emplace_back(
                            graphene::chain::genesis_state_type::initial_bts_account_type(modify_account_name(account_obj.name),
                                                                                          owner, active,
                                                                                          sharedrop_amount));
                already_generated.insert(account_id);
                ++accounts_generated_this_round;
            }
        }
        if (accounts_generated_this_round == 0)
            break;
    }
    fc::json::save_to_file(new_genesis_state, _output_genesis_filename);
    ilog("New genesis state written to file ${filename}", ("filename", _output_genesis_filename));
}

void generate_uia_sharedrop_genesis_plugin::plugin_shutdown()
{
}

