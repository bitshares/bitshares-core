/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

#include <graphene/app/plugin.hpp>
#include <graphene/chain/database.hpp>

#include <fc/thread/future.hpp>

namespace graphene { namespace witness_plugin {
namespace bpo = boost::program_options;

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

   virtual void plugin_initialize( const bpo::variables_map& options ) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;

private:
   void schedule_next_production(const graphene::chain::chain_parameters& global_parameters);
   void block_production_loop();

   bpo::variables_map _options;
   bool _production_enabled = false;
   std::map<chain::key_id_type, fc::ecc::private_key> _private_keys;
   std::set<chain::witness_id_type> _witnesses;
   fc::future<void> _block_production_task;
};

} } //graphene::delegate
