/*
 * Copyright (c) 2019 Blockchain Projects BV.
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
#include <graphene/voting_stat/voting_stat_plugin.hpp>

#include <graphene/chain/voting_statistics_object.hpp>
#include <graphene/chain/voteable_statistics_object.hpp>
#include <graphene/chain/worker_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/committee_member_object.hpp>

#include <graphene/chain/protocol/config.hpp>

#include <boost/signals2/connection.hpp>
#include <boost/signals2/shared_connection_block.hpp>

#include <memory>

namespace graphene { namespace voting_stat {

namespace detail {

class voting_stat_plugin_impl
{
public:
   voting_stat_plugin_impl(voting_stat_plugin& _plugin) : _self( _plugin ){}
   virtual ~voting_stat_plugin_impl(){}

   boost::signals2::connection _on_voting_stake_calc_conn;
   std::unique_ptr<boost::signals2::shared_connection_block> _on_voting_stake_calc_block;

   uint16_t _maint_counter = 0;

   /**
    * plugin parameters
    */
   bool     _keep_objects_in_db     = true;
   uint16_t _track_every_x_maint    = 12;
   bool     _track_worker_votes     = true;
   bool     _track_witness_votes    = true;
   bool     _track_committee_votes  = true;

   /**
    * @brief callback for database::on_stake_calculated
    *
    * This function is triggered when the calculation of a stake for a given account is done inside the maintenance
    * interval. It creates/updates the voting_statistics_object for a stake account. Optionally if a proxy is set
    * in the stake account, also the voting_statistics_object for the proxy account is created/updated.
    */
   void on_stake_calculated(const account_object& stake_account, const account_object& proxy_account, uint64_t stake);

   /**
    * @brief callback for database::on_maintenance_begin
    *
    * Updates the block number to the one where the maintenance interval occurs and unblocks the
    * database::on_stake_calculated signal, so that statistics objects can be created.
    */
   void on_maintenance_begin(uint32_t block_num);

   /**
    * @brief callback for database::on_maintenance_end
    *
    * Disconnects the on_stake_calculated callback and deletes all statistics objects if
    * _voting_stat_delete_objects_after_interval=true
    */
   void on_maintenance_end();

   void create_voteable_statistics_objects();
   void delete_all_statistics_objects();

   graphene::chain::database& database()
   {
      return _self.database();
   }

private:
   voting_stat_plugin& _self;

   uint32_t _maint_block;
   bool     _create_voteable = false;
};

void voting_stat_plugin_impl::on_maintenance_begin(uint32_t block_num)
{
   if( !_keep_objects_in_db )
      delete_all_statistics_objects();

   if( _maint_counter == _track_every_x_maint )
   {
      _on_voting_stake_calc_block->unblock();
      _maint_counter = 0;
      _maint_block = block_num;
      _create_voteable = true;
   }
   ++_maint_counter;
}

void voting_stat_plugin_impl::on_maintenance_end()
{
   _on_voting_stake_calc_block->block();
   if( _create_voteable ) {
      _create_voteable = false;
      create_voteable_statistics_objects();
   }
}

void voting_stat_plugin_impl::delete_all_statistics_objects()
{
   auto& db = database();

   // TODO find better way to delete
   const auto& voting_idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();
   for( const auto& voting_obj : voting_idx ) {
      db.remove( voting_obj );
   }

   const auto& voteable_idx = db.get_index_type<voteable_statistics_index>().indices().get<by_block_number>();
   for( const auto& voteable_obj : voteable_idx ) {
      db.remove( voteable_obj );
   }
}

void voting_stat_plugin_impl::create_voteable_statistics_objects()
{
   auto& db = database();

   // TODO secondary index for workers where current_time < worker_end_time
   // will reduce the iteration time
   if( _track_worker_votes )
   {
      const auto& worker_idx = db.get_index_type<worker_index>().indices().get<by_id>();

      auto now = db.head_block_time();
      for( const auto& worker : worker_idx )
      {
         if( now > worker.work_end_date )
            continue;

         db.create<voteable_statistics_object>( [this, &worker]( voteable_statistics_object& o ){
            o.block_number = _maint_block;
            o.vote_id = worker.vote_for;
         });
      }
   }

   if( _track_witness_votes )
   {
      const auto& witness_idx = db.get_index_type<witness_index>().indices().get<by_id>();
      for( const auto& witness : witness_idx )
      {
         db.create<voteable_statistics_object>( [this, &witness]( voteable_statistics_object& o ){
            o.block_number = _maint_block;
            o.vote_id = witness.vote_id;
         });
      }
   }

   if( _track_committee_votes )
   {
      const auto& committee_idx = db.get_index_type<committee_member_index>().indices().get<by_id>();
      for( const auto& committee_member : committee_idx )
      {
         db.create<voteable_statistics_object>( [this, &committee_member](voteable_statistics_object& o){
            o.block_number = _maint_block;
            o.vote_id = committee_member.vote_id;
         });
      }
   }


   const auto& voting_stat_idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();
   auto voting_stat_range = voting_stat_idx.equal_range( _maint_block );

   const auto& voteable_idx = db.get_index_type<voteable_statistics_index>().indices().get<by_block_number>();

   for( auto voting_stat_it = voting_stat_range.first; voting_stat_it != voting_stat_range.second; ++voting_stat_it )
   {
      uint64_t total_stake = voting_stat_it->get_total_voting_stake();
      if( !total_stake )
         continue; // don't bother inserting a 0 stake

      const flat_set<vote_id_type>& votes = voting_stat_it->votes;
      for( const auto& vote_id : votes )
      {
         auto voteable_obj_range = voteable_idx.equal_range( boost::make_tuple( _maint_block, vote_id ) );
         if( voteable_obj_range.first == voteable_obj_range.second )
            continue; // when the obj isn't found it isn't needed hence skip it

         db.modify<voteable_statistics_object>( *voteable_obj_range.first,
            [voting_stat_it, total_stake]( voteable_statistics_object& o ){
               o.voted_by.emplace( voting_stat_it->account, total_stake );
            }
         );
      }
   }
}

void voting_stat_plugin_impl::on_stake_calculated(
   const account_object& stake_account,
   const account_object& proxy_account,
   uint64_t stake )
{
   auto& db = database();

   account_id_type stake_id = stake_account.id;
   account_id_type proxy_id = proxy_account.id;
   proxy_id = ( stake_id == proxy_id ? GRAPHENE_PROXY_TO_SELF_ACCOUNT : proxy_id );

   const auto& voting_stat_idx = db.get_index_type<voting_statistics_index>().indices().get<by_block_number>();

   auto stake_stat_range = voting_stat_idx.equal_range( boost::make_tuple( _maint_block, stake_id ) );
   if( stake_stat_range.first == stake_stat_range.second )
   {
      db.create<voting_statistics_object>( [this, &stake_account, &proxy_id, stake]( voting_statistics_object& o ){
         o.block_number = _maint_block;
         o.account = stake_account.id;
         o.stake   = stake;
         o.proxy   = proxy_id;
         o.votes   = stake_account.options.votes;
      });
   }
   else
   {
      db.modify<voting_statistics_object>( *stake_stat_range.first,
         [this, &stake_account, &proxy_id, stake]( voting_statistics_object& o ){
            o.stake = stake;
            o.proxy = proxy_id;
            o.votes = stake_account.options.votes;
         }
      );
   }

   if( proxy_id == GRAPHENE_PROXY_TO_SELF_ACCOUNT )
      return;

   auto proxy_stat_range = voting_stat_idx.equal_range( boost::make_tuple( _maint_block, proxy_id ) );
   if( proxy_stat_range.first == proxy_stat_range.second )
   {
      db.create<voting_statistics_object>( [this, &stake_id, &proxy_id, stake]( voting_statistics_object& o ){
         o.block_number = _maint_block;
         o.account = proxy_id;
         o.proxy_for.emplace( stake_id, stake );
      });
   }
   else
   {
      db.modify<voting_statistics_object>( *proxy_stat_range.first,
         [&stake_id, stake]( voting_statistics_object& o ){
            o.proxy_for.emplace( stake_id, stake );
         }
      );
   }
}

} // namespace::detail



voting_stat_plugin::voting_stat_plugin() :
   my( new detail::voting_stat_plugin_impl(*this) )
{
}

voting_stat_plugin::~voting_stat_plugin()
{
}

std::string voting_stat_plugin::plugin_name()const
{
   return "voting_stat";
}

void voting_stat_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg )
{
   cli.add_options()
      (
         "voting-stat-track-every-x-maint", boost::program_options::value<uint16_t>(),
         "Every x maintenance interval statistic objects will be created (12=2per day)"
      )
      (
         "voting-stat-keep-objects-in-db", boost::program_options::value<bool>(),
         "Every created object will be deleted after the maintenance interval (true)"
      )
      (
         "voting-stat-track-worker-votes", boost::program_options::value<bool>(),
         "Worker votes will be tracked (true)"
      )
      (
         "voting-stat-track-witness-votes", boost::program_options::value<bool>(),
         "Witness votes will be tracked (true)"
      )
      (
         "voting-stat-track-committee-votes", boost::program_options::value<bool>(),
         "Committee votes will be tracked (true)"
      );
   cfg.add(cli);
}

void voting_stat_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   auto& db = database();
   db.add_index< primary_index<voting_statistics_index> >();
   db.add_index< primary_index<voteable_statistics_index> >();

   if( options.count("voting-stat-track-every-x-maint") ){
      my->_track_every_x_maint = options["voting-stat-track-every-x-maint"].as<uint16_t>();
      if( my->_track_every_x_maint == 0 )
         my->_track_every_x_maint = 1;
      my->_maint_counter = my->_track_every_x_maint;
   }
   if( options.count("voting-stat-keep-objects-in-db") ){
      my->_keep_objects_in_db = options["voting-stat-keep-objects-in-db"].as<bool>();
   }
   if( options.count("voting-stat-track-worker-votes") ){
      my->_track_worker_votes = options["voting-stat-track-worker-votes"].as<bool>();
   }
   if( options.count("voting-stat-track-witness-votes") ){
      my->_track_witness_votes = options["voting-stat-track-witness-votes"].as<bool>();
   }
   if( options.count("voting-stat-track-committee-votes") ){
      my->_track_committee_votes = options["voting-stat-track-committee-votes"].as<bool>();
   }

   my->_on_voting_stake_calc_conn = db.on_voting_stake_calculated.connect(
      [&]( const account_object& stake_account, const account_object& proxy_account, const uint64_t stake ){
         my->on_stake_calculated( stake_account, proxy_account, stake );
      }
   );

   my->_on_voting_stake_calc_block = std::unique_ptr<boost::signals2::shared_connection_block>(
      new boost::signals2::shared_connection_block(my->_on_voting_stake_calc_conn)
   );

   db.on_maintenance_begin.connect( [this](uint32_t block_num){
      my->on_maintenance_begin( block_num );
   });

   db.on_maintenance_end.connect( [this](){
      my->on_maintenance_end();
   });
}

void voting_stat_plugin::plugin_startup()
{
}

} }
