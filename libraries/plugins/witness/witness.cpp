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
#include <graphene/witness/witness.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/time/time.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/thread/thread.hpp>

#include <iostream>

using namespace graphene::witness_plugin;
using std::string;
using std::vector;

namespace bpo = boost::program_options;

void new_chain_banner( const graphene::chain::database& db )
{
   std::cerr << "\n"
      "********************************\n"
      "*                              *\n"
      "*   ------- NEW CHAIN ------   *\n"
      "*   - Welcome to Graphene! -   *\n"
      "*   ------------------------   *\n"
      "*                              *\n"
      "********************************\n"
      "\n";
   if( db.get_slot_at_time( graphene::time::now() ) > 200 )
   {
      std::cerr << "Your genesis seems to have an old timestamp\n"
         "Please consider using the --genesis-timestamp option to give your genesis a recent timestamp\n"
         "\n"
         ;
   }
   return;
}

void witness_plugin::plugin_set_program_options(
   boost::program_options::options_description& command_line_options,
   boost::program_options::options_description& config_file_options)
{
   auto default_priv_key = fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("nathan")));
   string witness_id_example = fc::json::to_string(chain::witness_id_type());
   command_line_options.add_options()
         ("enable-stale-production", bpo::bool_switch()->notifier([this](bool e){_production_enabled = e;}), "Enable block production, even if the chain is stale.")
         ("required-participation", bpo::bool_switch()->notifier([this](int e){_required_witness_participation = uint32_t(e*GRAPHENE_1_PERCENT);}), "Percent of witnesses (0-99) that must be participating in order to produce blocks")
         ("allow-consecutive", bpo::bool_switch()->notifier([this](bool e){_consecutive_production_enabled = e;}), "Allow block production, even if the last block was produced by the same witness.")
         ("witness-id,w", bpo::value<vector<string>>()->composing()->multitoken(),
          ("ID of witness controlled by this node (e.g. " + witness_id_example + ", quotes are required, may specify multiple times)").c_str())
         ("private-key", bpo::value<vector<string>>()->composing()->multitoken()->
          DEFAULT_VALUE_VECTOR(std::make_pair(chain::public_key_type(default_priv_key.get_public_key()), graphene::utilities::key_to_wif(default_priv_key))),
          "Tuple of [PublicKey, WIF private key] (may specify multiple times)")
         ;
   config_file_options.add(command_line_options);
}

std::string witness_plugin::plugin_name()const
{
   return "witness";
}

void witness_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
   _options = &options;
   LOAD_VALUE_SET(options, "witness-id", _witnesses, chain::witness_id_type)

   if( options.count("private-key") )
   {
      const std::vector<std::string> key_id_to_wif_pair_strings = options["private-key"].as<std::vector<std::string>>();
      for (const std::string& key_id_to_wif_pair_string : key_id_to_wif_pair_strings)
      {
         auto key_id_to_wif_pair = graphene::app::dejsonify<std::pair<chain::public_key_type, std::string> >(key_id_to_wif_pair_string);
         idump((key_id_to_wif_pair));
         fc::optional<fc::ecc::private_key> private_key = graphene::utilities::wif_to_key(key_id_to_wif_pair.second);
         if (!private_key)
         {
            // the key isn't in WIF format; see if they are still passing the old native private key format.  This is
            // just here to ease the transition, can be removed soon
            try
            {
               private_key = fc::variant(key_id_to_wif_pair.second).as<fc::ecc::private_key>();
            }
            catch (const fc::exception&)
            {
               FC_THROW("Invalid WIF-format private key ${key_string}", ("key_string", key_id_to_wif_pair.second));
            }
         }
         _private_keys[key_id_to_wif_pair.first] = *private_key;
      }
   }
} FC_LOG_AND_RETHROW() }

void witness_plugin::plugin_startup()
{ try {
   chain::database& d = database();
   std::set<chain::witness_id_type> bad_wits;
   //Start NTP time client
   graphene::time::now();
   for( auto wit : _witnesses )
   {
      if( d.find(wit) == nullptr )
      {
         if( app().is_finished_syncing() )
         {
            elog("ERROR: Unable to find witness ${w}, even though syncing has finished. This witness will be ignored.",
                 ("w", wit));
            continue;
         } else {
            wlog("WARNING: Unable to find witness ${w}. Postponing initialization until syncing finishes.",
                 ("w", wit));
            app().syncing_finished.connect([this]{plugin_startup();});
            return;
         }
      }

      auto signing_key = wit(d).signing_key;
      if( !_private_keys.count(signing_key) )
      {
         // Check if it's a duplicate key of one I do have
         bool found_duplicate = false;
         for( const auto& private_key : _private_keys )
            if( chain::public_key_type(private_key.second.get_public_key()) == signing_key )
            {
               ilog("Found duplicate key: ${k1} matches ${k2}; using this key to sign for ${w}",
                    ("k1", private_key.first)("k2", signing_key)("w", wit));
               _private_keys[signing_key] = private_key.second;
               found_duplicate = true;
               break;
            }
         if( found_duplicate )
            continue;

         elog("Unable to find key for witness ${w}. Removing it from my witnesses.", ("w", wit));
         bad_wits.insert(wit);
      }
   }
   for( auto wit : bad_wits )
      _witnesses.erase(wit);

   if( !_witnesses.empty() )
   {
      ilog("Launching block production for ${n} witnesses.", ("n", _witnesses.size()));
      app().set_block_production(true);
      if( _production_enabled && (d.head_block_num() == 0) )
         new_chain_banner(d);
      schedule_next_production(d.get_global_properties().parameters);
   } else
      elog("No witnesses configured! Please add witness IDs and private keys to configuration.");
} FC_CAPTURE_AND_RETHROW() }

void witness_plugin::plugin_shutdown()
{
   graphene::time::shutdown_ntp_time();
   return;
}

void witness_plugin::schedule_next_production(const graphene::chain::chain_parameters& global_parameters)
{
   //Get next production time for *any* witness
   auto block_interval = global_parameters.block_interval;
   fc::time_point next_block_time = fc::time_point_sec() +
         (graphene::time::now().sec_since_epoch() / block_interval + 1) * block_interval;

   if( graphene::time::ntp_time().valid() )
      next_block_time -= graphene::time::ntp_error();

   //Sleep until the next production time for *any* witness
   _block_production_task = fc::schedule([this]{block_production_loop();},
                                         next_block_time, "Witness Block Production");
}

void witness_plugin::block_production_loop()
{
   chain::database& db = database();
   const auto& global_parameters = db.get_global_properties().parameters;

   // Is there a head block within a block interval of now? If so, we're synced and can begin production.
   if( !_production_enabled &&
       llabs((db.head_block_time() - graphene::time::now()).to_seconds()) <= global_parameters.block_interval )
      _production_enabled = true;

   // is anyone scheduled to produce now or one second in the future?
   const fc::time_point_sec now = graphene::time::now();
   uint32_t slot = db.get_slot_at_time( now );
   graphene::chain::witness_id_type scheduled_witness = db.get_scheduled_witness( slot );
   fc::time_point_sec scheduled_time = db.get_slot_time( slot );
   graphene::chain::public_key_type scheduled_key = scheduled_witness( db ).signing_key;

   auto is_scheduled = [&]()
   {
      // conditions needed to produce a block:

      // we must control the witness scheduled to produce the next block.
      if( _witnesses.find( scheduled_witness ) == _witnesses.end() ) {
         return false;
      }

      // we must know the private key corresponding to the witness's
      // published block production key.
      if( _private_keys.find( scheduled_key ) == _private_keys.end() ) {
         elog("Not producing block because I don't have the private key for ${id}.", ("id", scheduled_key));
         return false;
      }

      // the next block must be scheduled after the head block.
      // if this check fails, the local clock has not advanced far
      // enough from the head block.
      if( slot == 0 ) {
         ilog("Not producing block because next slot time is in the future (likely a maitenance block).");
         return false;
      }

      // block production must be enabled (i.e. witness must be synced)
      if( !_production_enabled )
      {
         wlog("Not producing block because production is disabled until we receive a recent block (see: --enable-stale-production)");
         return false;
      }

      uint32_t prate = db.witness_participation_rate();
      if( prate < _required_witness_participation )
      {
         elog("Not producing block because node appears to be on a minority fork with only ${x}% witness participation",
              ("x",uint32_t(100*uint64_t(prate) / GRAPHENE_1_PERCENT) ) );
         return false;
      }

      // the local clock must be at least 1 second ahead of head_block_time.
      if( (now - db.head_block_time()).to_seconds() < GRAPHENE_MIN_BLOCK_INTERVAL ) {
         elog("Not producing block because head block is less than a second old.");
         return false;
      }

      // the local clock must be within 500 milliseconds of
      // the scheduled production time.
      if( llabs((scheduled_time - now).count()) > fc::milliseconds( 500 ).count() ) {
         elog("Not producing block because network time is not within 250ms of scheduled block time.");
         return false;
      }


      return true;
   };

   wdump((slot)(scheduled_witness)(scheduled_time)(now));
   if( is_scheduled() )
   {
      ilog("Witness ${id} production slot has arrived; generating a block now...", ("id", scheduled_witness));
      try
      {
         FC_ASSERT( _consecutive_production_enabled || db.get_dynamic_global_properties().current_witness != scheduled_witness,
                    "Last block was generated by the same witness, this node is probably disconnected from the network so block production"
                    " has been disabled.  Disable this check with --allow-consecutive option." );
         auto block = db.generate_block(
            scheduled_time,
            scheduled_witness,
            _private_keys[ scheduled_key ],
            graphene::chain::database::skip_nothing
            );
         ilog("Generated block #${n} with timestamp ${t} at time ${c}",
              ("n", block.block_num())("t", block.timestamp)("c", now));
         p2p_node().broadcast(net::block_message(block));
      }
      catch( const fc::canceled_exception& )
      {
         //We're trying to exit. Go ahead and let this one out.
         throw;
      }
      catch( const fc::exception& e )
      {
         elog("Got exception while generating block:\n${e}", ("e", e.to_detail_string()));
      }
   }

   schedule_next_production(global_parameters);
}
