/*
 * Copyright (c) 2017, PeerPlays Blockchain Standards Association
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
#include <graphene/generate_genesis/generate_genesis_plugin.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/genesis_state.hpp>
#include <graphene/chain/witness_object.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>

#include <graphene/chain/market_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>

#include <iostream>
#include <fstream>

using namespace graphene::generate_genesis_plugin;
using std::string;
using std::vector;

namespace bpo = boost::program_options;

void generate_genesis_plugin::plugin_set_program_options(
        boost::program_options::options_description& command_line_options,
        boost::program_options::options_description& config_file_options)
{
    command_line_options.add_options()
            ("output-genesis-file", bpo::value<std::string>()->default_value("genesis.json"), "Genesis file to create")
            ("output-csvlog-file", bpo::value<std::string>()->default_value("log.csv"), "CSV log file to create")
            ("snapshot-block-number", bpo::value<uint32_t>()->default_value(1000), "Block number at which to snapshot balances")
            ("shares-to-distribute", bpo::value<uint32_t>()->default_value(100000000), "Integer number of Shares to distribute (in 'satoshi')")
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
    config_file_options.add(command_line_options);
}

std::string generate_genesis_plugin::plugin_name()const
{
    return "generate_genesis";
}

void generate_genesis_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
        ilog("generate genesis plugin:  plugin_initialize() begin");
        _options = &options;

        _genesis_filename = options["output-genesis-file"].as<std::string>();
        _csvlog_filename = options["output-csvlog-file"].as<std::string>();
        if (options.count("snapshot-block-number"))
            _block_to_snapshot = options["snapshot-block-number"].as<uint32_t>();
        database().applied_block.connect([this](const graphene::chain::signed_block& b){ block_applied(b); });
        ilog("generate genesis plugin:  plugin_initialize() end");
    } FC_LOG_AND_RETHROW() }

void generate_genesis_plugin::plugin_startup()
{ try {
        ilog("generate genesis plugin:  plugin_startup() begin");
        if (_block_to_snapshot)
        {
            chain::database& d = database();
            if (d.head_block_num() == *_block_to_snapshot)
            {
                ilog("generate genesis plugin: already at snapshot block");
                generate_snapshot();
            }
            else if (d.head_block_num() > *_block_to_snapshot)
                elog("generate genesis plugin: already passed snapshot block, you must reindex to return to the snapshot state");
            else
                elog("generate genesis plugin: waiting for block ${snapshot_block} to generate snapshot, current head is ${head}",
                     ("snapshot_block", _block_to_snapshot)("head", d.head_block_num()));
        }
        else
            ilog("generate genesis plugin: no snapshot block number provided, plugin is disabled");

        ilog("generate genesis plugin:  plugin_startup() end");
    } FC_CAPTURE_AND_RETHROW() }

void generate_genesis_plugin::block_applied(const graphene::chain::signed_block& b)
{
    if (_block_to_snapshot && b.block_num() == *_block_to_snapshot)
    {
        ilog("generate genesis plugin: snapshot block has arrived");
        generate_snapshot();
    }
}

std::string modify_account_name(const std::string& name)
{
    return std::string("bts-") + name;
}

bool is_special_account(const graphene::chain::account_id_type& account_id)
{
    return account_id.instance < 100;
}

bool generate_genesis_plugin::is_excluded_account(const std::string& account_name)
{
    if( _options.count("exclude-account") )
    {
        auto exclude_accounts = _options.at("exclude-account").as<std::set<std::string>>();
        return exclude_accounts.find(account_name) != exclude_accounts.end();
    }
    return false;
}

bool generate_genesis_plugin::exclude_account_from_sharedrop(graphene::chain::database& d, const graphene::chain::account_id_type& account_id)
{
    if (is_special_account(account_id))
        return true;
    const std::string& account_name = account_id(d).name;
    return is_excluded_account(account_name);
}

void generate_genesis_plugin::generate_snapshot()
{ try {
    ilog("generate genesis plugin: generating snapshot now");
    graphene::chain::genesis_state_type new_genesis_state;
    chain::database& d = database();

    // we'll distribute 5% of (some amount of tokens), so:
    graphene::chain::share_type total_amount_to_distribute(_options.at("shares-to-distribute").as<uint32_t>());

    my_account_balance_object_index_type db_balances;
    graphene::chain::share_type total_bts_balance;

    auto& balance_index = d.get_index_type<graphene::chain::account_balance_index>().indices().get<graphene::chain::by_asset_balance>();
    for (auto balance_iter = balance_index.begin(); balance_iter != balance_index.end() && balance_iter->asset_type == graphene::chain::asset_id_type(); ++balance_iter)
    {
        if (balance_iter->balance > 0 && !exclude_account_from_sharedrop(d, balance_iter->owner))
        {
            total_bts_balance += balance_iter->balance;

            my_account_balance_object new_balance_object;
            new_balance_object.account_id = balance_iter->owner;
            new_balance_object.balance = balance_iter->balance;
            db_balances.insert(new_balance_object);
        }
    }


    // account for BTS tied up in market orders
    auto limit_order_index = d.get_index_type<graphene::chain::limit_order_index>().indices();
    for (const graphene::chain::limit_order_object& limit_order : limit_order_index)
        if (limit_order.amount_for_sale().asset_id == graphene::chain::asset_id_type())
        {
            graphene::chain::share_type limit_order_amount = limit_order.amount_for_sale().amount;
            if (limit_order_amount > 0 && !exclude_account_from_sharedrop(d, limit_order.seller))
            {
                total_bts_balance += limit_order_amount;

                auto my_balance_iter = db_balances.find(limit_order.seller);
                if (my_balance_iter == db_balances.end())
                {
                    my_account_balance_object balance_object;
                    balance_object.account_id = limit_order.seller;
                    balance_object.orders = limit_order_amount;
                    db_balances.insert(balance_object);
                }
                else
                {
                    db_balances.modify(my_balance_iter, [&](my_account_balance_object& balance_object) {
                        balance_object.orders += limit_order_amount;
                    });
                }
            }
        }

    // account for BTS tied up in collateral for SmartCoins
    auto call_order_index = d.get_index_type<graphene::chain::call_order_index>().indices();
    for (const graphene::chain::call_order_object& call_order : call_order_index)
        if (call_order.get_collateral().asset_id == graphene::chain::asset_id_type())
        {
            graphene::chain::share_type call_order_amount = call_order.get_collateral().amount;
            if (call_order_amount > 0 && !exclude_account_from_sharedrop(d, call_order.borrower))
            {
                total_bts_balance += call_order_amount;

                auto my_balance_iter = db_balances.find(call_order.borrower);
                if (my_balance_iter == db_balances.end())
                {
                    my_account_balance_object balance_object;
                    balance_object.account_id = call_order.borrower;
                    balance_object.collateral = call_order_amount;
                    db_balances.insert(balance_object);
                }
                else
                {
                    db_balances.modify(my_balance_iter, [&](my_account_balance_object& balance_object) {
                        balance_object.collateral += call_order_amount;
                    });
                }
            }
        }

    // account available-but-unclaimed BTS in vesting balances
    auto vesting_balance_index = d.get_index_type<graphene::chain::vesting_balance_index>().indices();
    for (const graphene::chain::vesting_balance_object& vesting_balance : vesting_balance_index)
        if (vesting_balance.balance.asset_id == graphene::chain::asset_id_type())
        {
            graphene::chain::share_type vesting_balance_amount = vesting_balance.get_allowed_withdraw(d.head_block_time()).amount;
            if (vesting_balance_amount > 0 && !exclude_account_from_sharedrop(d, vesting_balance.owner))
            {
                total_bts_balance += vesting_balance_amount;

                auto my_balance_iter = db_balances.find(vesting_balance.owner);
                if (my_balance_iter == db_balances.end())
                {
                    my_account_balance_object balance_object;
                    balance_object.account_id = vesting_balance.owner;
                    balance_object.vesting = vesting_balance_amount;
                    db_balances.insert(balance_object);
                }
                else
                {
                    db_balances.modify(my_balance_iter, [&](my_account_balance_object& balance_object) {
                        balance_object.vesting += vesting_balance_amount;
                    });
                }
            }
        }

    graphene::chain::share_type total_shares_dropped;

    // Now, we assume we're distributing balances to all BTS holders proportionally, figure
    // the smallest balance we can distribute and still assign the user a satoshi of the share drop
    graphene::chain::share_type effective_total_bts_balance;
    auto& by_effective_balance_index = db_balances.get<by_effective_balance>();
    auto balance_iter = by_effective_balance_index.begin();
    for (; balance_iter != by_effective_balance_index.end(); ++balance_iter)
    {
        fc::uint128 share_drop_amount = total_amount_to_distribute.value;
        share_drop_amount *= balance_iter->get_effective_balance().value;
        share_drop_amount /= total_bts_balance.value;
        if (!share_drop_amount.to_uint64())
            break; // balances are decreasing, so every balance after will also round to zero
        total_shares_dropped += share_drop_amount.to_uint64();
        effective_total_bts_balance += balance_iter->get_effective_balance();
    }

    // our iterator is just after the smallest balance we will process,
    // walk it backwards towards the larger balances, distributing the sharedrop as we go
    graphene::chain::share_type remaining_amount_to_distribute = total_amount_to_distribute;
    graphene::chain::share_type bts_balance_remaining = effective_total_bts_balance;

    do {
        --balance_iter;
        fc::uint128 share_drop_amount = remaining_amount_to_distribute.value;
        share_drop_amount *= balance_iter->get_effective_balance().value;
        share_drop_amount /= bts_balance_remaining.value;
        graphene::chain::share_type amount_distributed =  share_drop_amount.to_uint64();

        by_effective_balance_index.modify(balance_iter, [&](my_account_balance_object& balance_object) {
            balance_object.sharedrop += amount_distributed;
        });

        remaining_amount_to_distribute -= amount_distributed;
        bts_balance_remaining -= balance_iter->get_effective_balance();
    } while (balance_iter != by_effective_balance_index.begin());
    assert(remaining_amount_to_distribute == 0);

    std::ofstream logfile;
    logfile.open(_csvlog_filename);
    assert(logfile.is_open());
    logfile << "name,balance+orders+collateral+vesting,balance,orders,collateral,vesting,sharedrop\n";
    for (const my_account_balance_object& balance : by_effective_balance_index)
        logfile << balance.account_id(d).name << "," << 
                   balance.get_effective_balance().value << "," << 
                   balance.balance.value << "," << 
                   balance.orders.value << "," << 
                   balance.collateral.value << "," << 
                   balance.vesting.value << "," << 
                   balance.sharedrop.value << "\n";
    ilog("CSV log written to file ${filename}", ("filename", _csvlog_filename));
    logfile.close();

    // remove all balance objects with zero sharedrops
    by_effective_balance_index.erase(by_effective_balance_index.lower_bound(0), 
                                     by_effective_balance_index.end());

    // inefficient way of crawling the graph, but we only do it once
    std::set<graphene::chain::account_id_type> already_generated;
    for (;;)
    {
        unsigned accounts_generated_this_round = 0;
        for (const my_account_balance_object& balance : by_effective_balance_index)
        {
            const graphene::chain::account_object& account_obj = balance.account_id(d);
            if (already_generated.find(balance.account_id) == already_generated.end())
            {
                graphene::chain::genesis_state_type::initial_bts_account_type::initial_authority owner;
                owner.weight_threshold = account_obj.owner.weight_threshold;
                owner.key_auths = account_obj.owner.key_auths;
                for (const auto& value : account_obj.owner.account_auths)
                {
                    owner.account_auths.insert(std::make_pair(modify_account_name(value.first(d).name), value.second));
                    db_balances.insert(my_account_balance_object{value.first}); // make sure the account is generated, even if it has a zero balance
                }
                owner.key_auths = account_obj.owner.key_auths;
                owner.address_auths = account_obj.owner.address_auths;

                graphene::chain::genesis_state_type::initial_bts_account_type::initial_authority active;
                active.weight_threshold = account_obj.active.weight_threshold;
                active.key_auths = account_obj.active.key_auths;
                for (const auto& value : account_obj.active.account_auths)
                {
                    active.account_auths.insert(std::make_pair(modify_account_name(value.first(d).name), value.second));
                    db_balances.insert(my_account_balance_object{value.first}); // make sure the account is generated, even if it has a zero balance
                }
                active.key_auths = account_obj.active.key_auths;
                active.address_auths = account_obj.active.address_auths;

                new_genesis_state.initial_bts_accounts.emplace_back(
                            graphene::chain::genesis_state_type::initial_bts_account_type(modify_account_name(account_obj.name),
                                                                                          owner, active,
                                                                                          balance.sharedrop));
                already_generated.insert(balance.account_id);
                ++accounts_generated_this_round;
            }
        }
        if (accounts_generated_this_round == 0)
            break;
    }
    fc::json::save_to_file(new_genesis_state, _genesis_filename);
    ilog("New genesis state written to file ${filename}", ("filename", _genesis_filename));
} FC_LOG_AND_RETHROW() }

void generate_genesis_plugin::plugin_shutdown()
{
}

