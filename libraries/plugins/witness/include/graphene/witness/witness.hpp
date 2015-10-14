/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace witness_plugin {

namespace block_production_condition
{
   enum block_production_condition_enum
   {
      produced = 0,
      not_synced = 1,
      not_my_turn = 2,
      not_time_yet = 3,
      no_private_key = 4,
      low_participation = 5,
      lag = 6,
      consecutive = 7,
      exception_producing_block = 8
   };
}

class witness_plugin : public graphene::app::plugin {
public:
   ~witness_plugin() {
      try {
         if( _block_production_task.valid() )
            _block_production_task.cancel_and_wait(__FUNCTION__);
      } catch(fc::canceled_exception&) {
         //Expected exception. Move along.
      } catch(fc::exception& e) {
         edump((e.to_detail_string()));
      }
   }

   std::string plugin_name()const override;

   virtual void plugin_set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   void set_block_production(bool allow) { _production_enabled = allow; }

   virtual void plugin_initialize( const boost::program_options::variables_map& options ) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;

private:
   void schedule_production_loop();
   block_production_condition::block_production_condition_enum block_production_loop();
   block_production_condition::block_production_condition_enum maybe_produce_block( fc::mutable_variant_object& capture );

   boost::program_options::variables_map _options;
   bool _production_enabled = false;
   bool _consecutive_production_enabled = false;
   uint32_t _required_witness_participation = 33 * GRAPHENE_1_PERCENT;
   uint32_t _production_skip_flags = graphene::chain::database::skip_nothing;

   std::map<chain::public_key_type, fc::ecc::private_key> _private_keys;
   std::set<chain::witness_id_type> _witnesses;
   fc::future<void> _block_production_task;
};

} } //graphene::witness_plugin
