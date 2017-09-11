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
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace generate_genesis_plugin {

class generate_genesis_plugin : public graphene::app::plugin {
public:
   ~generate_genesis_plugin() {
   }

   std::string plugin_name()const override;

   virtual void plugin_set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   virtual void plugin_initialize( const boost::program_options::variables_map& options ) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;
   bool is_excluded_account(const std::string& account_name);
   bool exclude_account_from_sharedrop(graphene::chain::database& d, const graphene::chain::account_id_type& account_id);

private:
   void block_applied(const graphene::chain::signed_block& b);
   void generate_snapshot();

   boost::program_options::variables_map _options;

   fc::optional<uint32_t> _block_to_snapshot;
   std::string _genesis_filename;
   std::string _csvlog_filename;
};

class my_account_balance_object
{
public:
   // constructor copying from base class
   //my_account_balance_object(const graphene::chain::account_balance_object& abo) : graphene::chain::account_balance_object(abo) {}
   graphene::chain::account_id_type   account_id;

   graphene::chain::share_type    balance;
   graphene::chain::share_type    orders;
   graphene::chain::share_type    collateral;
   graphene::chain::share_type    vesting;
   graphene::chain::share_type    sharedrop;
   graphene::chain::share_type    get_effective_balance() const { return balance + orders + collateral + vesting; }
};
using namespace boost::multi_index;
struct by_account{};
struct by_effective_balance{};
typedef multi_index_container<my_account_balance_object, 
                              indexed_by<ordered_unique<tag<by_account>,
                                                        member<my_account_balance_object, graphene::chain::account_id_type, &my_account_balance_object::account_id> >,
                                         ordered_non_unique<tag<by_effective_balance>,
                                                            const_mem_fun<my_account_balance_object, graphene::chain::share_type, &my_account_balance_object::get_effective_balance>,
                                                            std::greater<graphene::chain::share_type> > > > my_account_balance_object_index_type;


} } //graphene::generate_genesis_plugin
