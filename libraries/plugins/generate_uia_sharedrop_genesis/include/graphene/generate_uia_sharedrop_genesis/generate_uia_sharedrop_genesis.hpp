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
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace generate_uia_sharedrop_genesis {

class generate_uia_sharedrop_genesis_plugin : public graphene::app::plugin {
public:
   ~generate_uia_sharedrop_genesis_plugin() {
   }

   std::string plugin_name()const override;

   virtual void plugin_set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   virtual void plugin_initialize( const boost::program_options::variables_map& options ) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;

private:
   void block_applied(const graphene::chain::signed_block& b);
   void generate_snapshot();
   bool is_excluded_account(const std::string& account_name);

   boost::program_options::variables_map _options;

   fc::optional<uint32_t> _block_to_snapshot;
   std::string _input_genesis_filename;
   std::string _output_genesis_filename;
   std::string _csvlog_filename;
};

class uia_sharedrop_balance_object
{
public:
   graphene::chain::account_id_type   account_id;

   graphene::chain::share_type        genesis;
   graphene::chain::share_type        balance;
   graphene::chain::share_type        orders;

   graphene::chain::share_type        sharedrop;
};

using namespace boost::multi_index;
struct by_account{};
typedef multi_index_container<uia_sharedrop_balance_object, 
                              indexed_by<ordered_unique<tag<by_account>,
                                                        member<uia_sharedrop_balance_object, graphene::chain::account_id_type, &uia_sharedrop_balance_object::account_id> > > > uia_sharedrop_balance_object_index_type;

} } //graphene::generate_uia_sharedrop_genesis_plugin
