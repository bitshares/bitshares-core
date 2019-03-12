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

#include <graphene/chain/protocol/config.hpp>

namespace graphene { namespace voting_stat {

namespace detail {

class voting_stat_plugin_impl
{
public:
   voting_stat_plugin_impl(voting_stat_plugin& _plugin)
      : _self( _plugin ){}

   virtual ~voting_stat_plugin_impl(){}

   graphene::chain::database& database()
   {
      return _self.database();
   }

   /**
    * @brief callback for database::on_stake_calculated
    * updates the given stake anc proxy account
    */
   void on_stake_calculated(
      const account_object& stake_account,
      const account_object& proxy_account, 
      uint64_t stake );

   /**
    * @brief callback for database::on_maintenance_begin
    * updates the block_id to the one where the maintenance interval occurs
    */
   void on_maintenance_begin( const block_id_type& block_id );

private:
   voting_stat_plugin& _self;
   
};

void voting_stat_plugin_impl::on_maintenance_begin( const block_id_type& block_id )
{
   graphene::chain::voting_statistics_object::block_id = block_id;
}

void voting_stat_plugin_impl::on_stake_calculated( 
   const account_object& stake_account,
   const account_object& proxy_account, 
   uint64_t stake )
{
   auto& db = database();

   account_id_type stake_id = stake_account.id;
   account_id_type proxy_id = proxy_account.id;
   account_id_type new_proxy_id = stake_id == proxy_id ?
      GRAPHENE_PROXY_TO_SELF_ACCOUNT : proxy_id;
   account_id_type old_proxy_id;
   bool has_stake_changed;
   
   const auto& idx = db.get_index_type<voting_statistics_index>().indices().get<by_owner>();
   auto stake_stat_it = idx.find( stake_id );
   if( stake_stat_it == idx.end() )
   {
      has_stake_changed = true;
      old_proxy_id      = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
      db.create<voting_statistics_object>(
         [&stake_account, new_proxy_id, stake] (voting_statistics_object& o){
            o.account = stake_account.id;
            o.stake   = stake;
            o.proxy   = new_proxy_id;
            o.votes   = stake_account.options.votes;
         }
      );
      stake_stat_it = idx.find( stake_id );
   }
   else
   {
      has_stake_changed = stake_stat_it->stake != stake;
      old_proxy_id      = stake_stat_it->proxy;
      db.modify<voting_statistics_object>( *stake_stat_it,
         [&stake_account, new_proxy_id, stake] (voting_statistics_object& o) {
            o.stake = stake;
            o.proxy = new_proxy_id;
            o.votes = stake_account.options.votes;
         }
      ); 
   }
   
   if( old_proxy_id != new_proxy_id )
   {
      if( old_proxy_id != GRAPHENE_PROXY_TO_SELF_ACCOUNT ) 
      {
         /* if the proxy account has changed delete the proxied stake from the old proxy account */
         auto old_proxy_stat_it = idx.find( old_proxy_id );
         db.modify<voting_statistics_object>( *old_proxy_stat_it, 
            [stake_id] (voting_statistics_object& o) {
               o.proxy_for.erase( stake_id ); 
            }
         );
      }
      
      if( new_proxy_id != GRAPHENE_PROXY_TO_SELF_ACCOUNT )
      {
         auto proxy_stat_it = idx.find( new_proxy_id );
         if( proxy_stat_it == idx.end() ) 
         {
            /* if the new proxy account doesn't exist, create and add the proxied stake */
            db.create<voting_statistics_object>(
               [stake_id, new_proxy_id, stake] (voting_statistics_object& o) {
                  o.account = new_proxy_id;
                  o.proxy_for.emplace( stake_id, stake ); 
               }
            );
         }
         else
         {
            /* insert the stake into the new proxy account */
            auto proxy_stat_it = idx.find( proxy_account.id );
            db.modify<voting_statistics_object>( *proxy_stat_it,
               [stake_id, stake] (voting_statistics_object& o) {
                  auto insertion_return = o.proxy_for.emplace( stake_id, stake );
                  if( !insertion_return.second )
                     insertion_return.first->second = stake;
               }
            );
         }
      }
   }
   else if( stake_stat_it->has_proxy() && has_stake_changed  )
   {
      /* when the proxied stake has changed update the proxy account with the new stake */
      auto proxy_stat_it = idx.find( proxy_account.id );
      db.modify<voting_statistics_object>( *proxy_stat_it,
         [stake_id, stake] (voting_statistics_object& o) {
            auto insertion_return = o.proxy_for.emplace( stake_id, stake );
            if( !insertion_return.second )
               insertion_return.first->second = stake;
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
   boost::program_options::options_description& cfg
   )
{
}

void voting_stat_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   auto& db = database();
   db.add_index< primary_index< voting_statistics_index > >();

   db.on_voting_stake_calculated.connect( 
      [&](const account_object& stake_account, const account_object& proxy_account, 
         const uint64_t stake) {
            my->on_stake_calculated( stake_account, proxy_account, stake ); 
         }
   );
   
   db.on_maintenance_begin.connect(
      [&](const block_id_type& block_id){ my->on_maintenance_begin( block_id ); 
   });

}

void voting_stat_plugin::plugin_startup()
{
}

} }
