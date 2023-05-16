/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <sstream>
#include <iomanip>
#include <deque>
#include <unordered_set>
#include <list>
#include <forward_list>
#include <iostream>
#include <algorithm>
#include <tuple>
#include <string>
#include <boost/tuple/tuple.hpp>
#include <boost/circular_buffer.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/range/algorithm_ext/push_back.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/numeric.hpp>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <fc/thread/thread.hpp>
#include <fc/thread/future.hpp>
#include <fc/thread/non_preemptable_scope_check.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/json.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/network/ip.hpp>
#include <fc/network/resolve.hpp>

#include <graphene/net/node.hpp>
#include <graphene/net/peer_database.hpp>
#include <graphene/net/peer_connection.hpp>
#include <graphene/net/stcp_socket.hpp>
#include <graphene/net/config.hpp>
#include <graphene/net/exceptions.hpp>

#include <graphene/chain/config.hpp>
#include <graphene/chain/exceptions.hpp>
// Nasty hack: A circular dependency around fee_schedule is resolved by fwd-declaring it and using a shared_ptr
// to it in chain_parameters, which is used in an operation and thus must be serialized by the net library.
// Resolving that forward declaration doesn't happen until now:
#include <graphene/protocol/fee_schedule.hpp>

#include <fc/git_revision.hpp>

#include "node_impl.hxx"

namespace graphene { namespace net { namespace detail {

   void blockchain_tied_message_cache::block_accepted()
   {
      ++block_clock;
      if( block_clock > cache_duration_in_blocks )
         _message_cache.get<block_clock_index>().erase(_message_cache.get<block_clock_index>().begin(),
               _message_cache.get<block_clock_index>().lower_bound(block_clock - cache_duration_in_blocks ) );
   }

   void blockchain_tied_message_cache::cache_message( const message& message_to_cache,
                                                      const message_hash_type& hash_of_message_to_cache,
                                                      const message_propagation_data& propagation_data,
                                                      const message_hash_type& message_content_hash )
   {
      _message_cache.insert( message_info(hash_of_message_to_cache,
                                         message_to_cache,
                                         block_clock,
                                         propagation_data,
                                         message_content_hash ) );
   }

   message blockchain_tied_message_cache::get_message( const message_hash_type& hash_of_message_to_lookup ) const
   {
      message_cache_container::index<message_hash_index>::type::const_iterator iter =
         _message_cache.get<message_hash_index>().find(hash_of_message_to_lookup );
      if( iter != _message_cache.get<message_hash_index>().end() )
         return iter->message_body;
      FC_THROW_EXCEPTION(  fc::key_not_found_exception, "Requested message not in cache" );
   }

    message_propagation_data blockchain_tied_message_cache::get_message_propagation_data(
             const message_hash_type& hash_of_msg_contents_to_lookup ) const
    {
      if( hash_of_msg_contents_to_lookup != message_hash_type() )
      {
        message_cache_container::index<message_contents_hash_index>::type::const_iterator iter =
           _message_cache.get<message_contents_hash_index>().find(hash_of_msg_contents_to_lookup );
        if( iter != _message_cache.get<message_contents_hash_index>().end() )
          return iter->propagation_data;
      }
      FC_THROW_EXCEPTION(  fc::key_not_found_exception, "Requested message not in cache" );
    }

    void node_impl_deleter::operator()(node_impl* impl_to_delete)
    {
#ifdef P2P_IN_DEDICATED_THREAD
      std::weak_ptr<fc::thread> weak_thread;
      if (impl_to_delete)
      {
        std::shared_ptr<fc::thread> impl_thread(impl_to_delete->_thread);
        weak_thread = impl_thread;
        impl_thread->async([impl_to_delete](){ delete impl_to_delete; }, "delete node_impl").wait();
        dlog("deleting the p2p thread");
      }
      if (weak_thread.expired())
        dlog("done deleting the p2p thread");
      else
        dlog("failed to delete the p2p thread, we must be leaking a smart pointer somewhere");
#else // P2P_IN_DEDICATED_THREAD
      delete impl_to_delete;
#endif // P2P_IN_DEDICATED_THREAD
    }

#ifdef P2P_IN_DEDICATED_THREAD
# define VERIFY_CORRECT_THREAD() assert(_thread->is_current())
#else
# define VERIFY_CORRECT_THREAD() do {} while (0)
#endif

   /// Greatly delays the next connection to the endpoint
   static void greatly_delay_next_conn_to( node_impl* impl, const fc::ip::endpoint& ep )
   {
      dlog( "Greatly delaying the next connection to endpoint ${ep}", ("ep", ep) );
      fc::optional<potential_peer_record> updated_peer_record
            = impl->_potential_peer_db.lookup_entry_for_endpoint( ep );
      if( updated_peer_record )
      {
         updated_peer_record->last_connection_disposition = last_connection_rejected;
         updated_peer_record->last_connection_attempt_time = fc::time_point::now();
         constexpr uint32_t failed_attempts_to_add = 120; // * 30 seconds = 1 hour
         updated_peer_record->number_of_failed_connection_attempts += failed_attempts_to_add;
         impl->_potential_peer_db.update_entry( *updated_peer_record );
      }
   }
   /// Saves a successfully connected endpoint to the peer database
   static void save_successful_address( node_impl* impl, const fc::ip::endpoint& ep )
   {
      dlog( "Saving successfully connected endpoint ${ep} to peer database", ("ep", ep) );
      auto updated_peer_record = impl->_potential_peer_db.lookup_or_create_entry_for_ep( ep );
      updated_peer_record.last_connection_disposition = last_connection_succeeded;
      updated_peer_record.last_connection_attempt_time = fc::time_point::now();
      // halve number_of_failed_connection_attempts
      constexpr uint16_t two = 2;
      updated_peer_record.number_of_failed_connection_attempts /= two;
      updated_peer_record.last_seen_time = fc::time_point::now();
      impl->_potential_peer_db.update_entry(updated_peer_record);
   }
   static void update_address_seen_time( node_impl* impl, const peer_connection* active_peer )
   {
      fc::optional<fc::ip::endpoint> inbound_endpoint = active_peer->get_endpoint_for_connecting();
      if( inbound_endpoint.valid() && inbound_endpoint->port() != 0 )
      {
         fc::optional<potential_peer_record> updated_peer_record
               = impl->_potential_peer_db.lookup_entry_for_endpoint( *inbound_endpoint );
         if( updated_peer_record )
         {
            updated_peer_record->last_seen_time = fc::time_point::now();
            impl->_potential_peer_db.update_entry( *updated_peer_record );
         }
      }
   }
   static void update_address_seen_time( node_impl* impl, const peer_connection_ptr& active_peer )
   {
      update_address_seen_time( impl, active_peer.get() );
   }

   /// Base class for list address builder and exclude_list address builder
   class generic_list_address_builder : public node_impl::address_builder
   {
   public:
      fc::flat_set<fc::ip::endpoint> list;

      explicit generic_list_address_builder(const std::vector<std::string>& address_list)
      {
         FC_ASSERT( !address_list.empty(), "The peer node list must not be empty" );

         std::for_each( address_list.begin(), address_list.end(), [&list = list]( const std::string& str )
            {
               // ignore fc exceptions (like poorly formatted endpoints)
               try
               {
                  list.insert( fc::ip::endpoint::from_string(str) );
               }
               catch(const fc::exception& )
               {
                  wlog( "Address ${addr} invalid.", ("addr", str) );
               }
            } );
      }
   };

   /******
    * Use information passed from command line or config file to advertise nodes
    */
   class list_address_builder : public generic_list_address_builder
   {
   public:
      using generic_list_address_builder::generic_list_address_builder;

      bool should_advertise( const fc::ip::endpoint& in ) const override
      {
         return !( list.find(in) == list.end() );
      }
   };

   /****
    * Advertise all nodes except a predefined list
    */
   class exclude_address_builder : public generic_list_address_builder
   {
   public:
      using generic_list_address_builder::generic_list_address_builder;

      bool should_advertise( const fc::ip::endpoint& in ) const override
      {
         return ( list.find( in ) == list.end() );
      }
   };

   /***
    * Return all peers when node asks
    */
   class all_address_builder : public node_impl::address_builder
   {
      bool should_advertise( const fc::ip::endpoint& in ) const override
      {
         return true;
      }
   };

   std::shared_ptr<node_impl::address_builder> node_impl::address_builder::create_default_address_builder()
   {
      return std::make_shared<all_address_builder>();
   }

   void node_impl::address_builder::build(node_impl* impl, address_message& reply) const
   {
      reply.addresses.clear();
      reply.addresses.reserve( impl->_active_connections.size() );
      fc::scoped_lock<fc::mutex> lock(impl->_active_connections.get_mutex());
      fc::time_point_sec now = fc::time_point::now();
      // only pass those that are allowed to advertise AND we are connected to
      for( const peer_connection_ptr& active_peer : impl->_active_connections )
      {
         // Note:
         // * We want to advertise the peer's inbound endpoint, but not necessarily the remote endpoint.
         // * If the peer's inbound port is 0, we still advertise it so that observers know about it.
         //   The peer is marked as "firewalled", so peers running older versions should be able to handle it too.
         //
         // If it is an outbound connection, we know that the remote endpoint works (at least for us),
         //   and we have assigned it to the inbound endpoint, so just use either of them.
         // If it is an inbound connection, we just advertise what we have.
         fc::optional<fc::ip::endpoint> inbound_endpoint = active_peer->get_endpoint_for_connecting();
         if( inbound_endpoint.valid() && should_advertise( *inbound_endpoint ) )
         {
            update_address_seen_time( impl, active_peer );
            reply.addresses.emplace_back( *inbound_endpoint,
                                          now,
                                          active_peer->round_trip_delay,
                                          active_peer->node_id,
                                          active_peer->direction,
                                          active_peer->is_firewalled );
         }
      }
   }

   node_impl::node_impl(const std::string& user_agent) :
      _user_agent_string(user_agent)
   {
      _rate_limiter.set_actual_rate_time_constant(fc::seconds(2));
      // Note: this means that the node gets a new node_id every time it restarts
      fc::rand_bytes((char*) _node_id.data(), (int)_node_id.size());
   }

    node_impl::~node_impl()
    {
      VERIFY_CORRECT_THREAD();
      ilog( "cleaning up node" );
      _node_is_shutting_down = true;

      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& active_peer : _active_connections)
         {
            update_address_seen_time( this, active_peer );
         }
      }

      try
      {
        ilog( "close" );
        close();
      }
      catch ( const fc::exception& e )
      {
        wlog( "unexpected exception on close ${e}", ("e", e) );
      }
      ilog( "done" );
    }

   void node_impl::save_node_configuration()
   {
      VERIFY_CORRECT_THREAD();

      fc::path configuration_file_name( _node_configuration_directory / NODE_CONFIGURATION_FILENAME );
      try
      {
         if( !fc::exists(_node_configuration_directory ) )
            fc::create_directories( _node_configuration_directory );
         fc::json::save_to_file( _node_configuration, configuration_file_name );
         dlog( "Saved node configuration to file ${filename}", ( "filename", configuration_file_name ) );
      }
      catch (const fc::canceled_exception&)
      {
         throw;
      }
      catch ( const fc::exception& except )
      {
         wlog( "error writing node configuration to file ${filename}: ${error}",
               ( "filename", configuration_file_name )("error", except.to_detail_string() ) );
      }
   }

    void node_impl::p2p_network_connect_loop()
    {
      VERIFY_CORRECT_THREAD();
      while (!_p2p_network_connect_loop_done.canceled())
      {
        try
        {
          dlog("Starting an iteration of p2p_network_connect_loop().");
          display_current_connections();

          // add-once peers bypass our checks on the maximum/desired number of connections
          // (but they will still be counted against the totals once they're connected)
          if (!_add_once_node_list.empty())
          {
            std::list<potential_peer_record> add_once_node_list;
            add_once_node_list.swap(_add_once_node_list);
            dlog("Processing \"add once\" node list containing ${count} peers:",
                 ("count", add_once_node_list.size()));
            for (const potential_peer_record& add_once_peer : add_once_node_list)
            {
              dlog("    ${peer}", ("peer", add_once_peer.endpoint));
            }
            for (const potential_peer_record& add_once_peer : add_once_node_list)
            {
              // If we have an existing connection to that peer, skip it.
              peer_connection_ptr existing_connection_ptr = get_connection_for_endpoint( add_once_peer.endpoint );
              if(!existing_connection_ptr)
                connect_to_endpoint(add_once_peer.endpoint);
            }
            dlog("Done processing \"add once\" node list");
          }

          while (is_wanting_new_connections())
          {
            bool initiated_connection_this_pass = false;
            _potential_peer_db_updated = false;

            for (peer_database::iterator iter = _potential_peer_db.begin();
                 iter != _potential_peer_db.end() && is_wanting_new_connections();
                 ++iter)
            {
              fc::microseconds delay_until_retry = fc::seconds( (iter->number_of_failed_connection_attempts + 1)
                                                                * _peer_connection_retry_timeout );

              bool last_connection_not_ok = ( iter->last_connection_disposition == last_connection_failed ||
                         iter->last_connection_disposition == last_connection_rejected ||
                         iter->last_connection_disposition == last_connection_handshaking_failed );

              if( !is_connected_to_endpoint( iter->endpoint )
                  && ( !last_connection_not_ok
                       || ( fc::time_point::now() - iter->last_connection_attempt_time ) > delay_until_retry ) )
              {
                connect_to_endpoint(iter->endpoint);
                initiated_connection_this_pass = true;
              }
            }

            if (!initiated_connection_this_pass && !_potential_peer_db_updated)
              break;
          }

          display_current_connections();

          // if we broke out of the while loop, that means either we have connected to enough nodes, or
          // we don't have any good candidates to connect to right now.
#if 0
          try
          {
            _retrigger_connect_loop_promise = fc::promise<void>::create("graphene::net::retrigger_connect_loop");
            if( is_wanting_new_connections() || !_add_once_node_list.empty() )
            {
              if( is_wanting_new_connections() )
                dlog( "Still want to connect to more nodes, but I don't have any good candidates.  Trying again in 15 seconds" );
              else
                dlog( "I still have some \"add once\" nodes to connect to.  Trying again in 15 seconds" );
              _retrigger_connect_loop_promise->wait_until( fc::time_point::now() + fc::seconds(GRAPHENE_PEER_DATABASE_RETRY_DELAY ) );
            }
            else
            {
              dlog( "I don't need any more connections, waiting forever until something changes" );
              _retrigger_connect_loop_promise->wait();
            }
          }
          catch ( fc::timeout_exception& ) //intentionally not logged
          {
          }  // catch
#else
          fc::usleep(fc::seconds(10));
#endif
        }
        catch (const fc::canceled_exception&)
        {
          ilog( "p2p_network_connect_loop canceled" );
          throw;
        }
        FC_CAPTURE_AND_LOG( (0) ) // GCOVR_EXCL_LINE
      }// while !canceled
    }

    void node_impl::trigger_p2p_network_connect_loop()
    {
      VERIFY_CORRECT_THREAD();
      dlog( "Triggering connect loop now" );
      _potential_peer_db_updated = true;
      //if( _retrigger_connect_loop_promise )
      //  _retrigger_connect_loop_promise->set_value();
    }

   void node_impl::update_seed_nodes_task()
   {
      VERIFY_CORRECT_THREAD();

      try
      {
         ilog("Starting an iteration of update_seed_nodes loop.");
         for( const std::string& endpoint_string : _seed_nodes )
         {
            resolve_seed_node_and_add( endpoint_string );
         }
         ilog("Done an iteration of update_seed_nodes loop.");
      }
      catch (const fc::canceled_exception&)
      {
         ilog( "update_seed_nodes_task canceled" );
         throw;
      }
      FC_CAPTURE_AND_LOG( (_seed_nodes) ) // GCOVR_EXCL_LINE

      schedule_next_update_seed_nodes_task();
   }

   void node_impl::schedule_next_update_seed_nodes_task()
   {
      VERIFY_CORRECT_THREAD();

      if( _node_is_shutting_down )
         return;

      if( _update_seed_nodes_loop_done.valid() && _update_seed_nodes_loop_done.canceled() )
         return;

      constexpr uint32_t five = 5;
      auto interval = _active_connections.empty() ? fc::minutes(five) : fc::hours(1);
      _update_seed_nodes_loop_done = fc::schedule( [this]() { update_seed_nodes_task(); },
                                                   fc::time_point::now() + interval,
                                                   "update_seed_nodes_loop" );
   }

    bool node_impl::have_already_received_sync_item( const item_hash_t& item_hash )
    {
      VERIFY_CORRECT_THREAD();
      return std::find_if(_received_sync_items.begin(), _received_sync_items.end(),
                          [&item_hash]( const graphene::net::block_message& message ) { return message.block_id == item_hash; } ) != _received_sync_items.end() ||
             std::find_if(_new_received_sync_items.begin(), _new_received_sync_items.end(),
                          [&item_hash]( const graphene::net::block_message& message ) { return message.block_id == item_hash; } ) != _new_received_sync_items.end();                          ;
    }

    void node_impl::request_sync_item_from_peer( const peer_connection_ptr& peer, const item_hash_t& item_to_request )
    {
      VERIFY_CORRECT_THREAD();
      dlog( "requesting item ${item_hash} from peer ${endpoint}", ("item_hash", item_to_request )("endpoint", peer->get_remote_endpoint() ) );
      item_id item_id_to_request( graphene::net::block_message_type, item_to_request );
      _active_sync_requests.insert( active_sync_requests_map::value_type(item_to_request, fc::time_point::now() ) );
      peer->last_sync_item_received_time = fc::time_point::now();
      peer->sync_items_requested_from_peer.insert(item_to_request);
      peer->send_message( fetch_items_message(item_id_to_request.item_type, std::vector<item_hash_t>{item_id_to_request.item_hash} ) );
    }

    void node_impl::request_sync_items_from_peer( const peer_connection_ptr& peer, const std::vector<item_hash_t>& items_to_request )
    {
      VERIFY_CORRECT_THREAD();
      dlog( "requesting ${item_count} item(s) ${items_to_request} from peer ${endpoint}",
            ("item_count", items_to_request.size())("items_to_request", items_to_request)("endpoint", peer->get_remote_endpoint()) );
      for (const item_hash_t& item_to_request : items_to_request)
      {
        _active_sync_requests.insert( active_sync_requests_map::value_type(item_to_request, fc::time_point::now() ) );
        peer->last_sync_item_received_time = fc::time_point::now();
        peer->sync_items_requested_from_peer.insert(item_to_request);
      }
      peer->send_message(fetch_items_message(graphene::net::block_message_type, items_to_request));
    }

    void node_impl::fetch_sync_items_loop()
    {
      VERIFY_CORRECT_THREAD();
      while( !_fetch_sync_items_loop_done.canceled() )
      {
        _sync_items_to_fetch_updated = false;
        dlog( "beginning another iteration of the sync items loop" );

        if (!_suspend_fetching_sync_blocks)
        {
          std::map<peer_connection_ptr, std::vector<item_hash_t> > sync_item_requests_to_send;

          {
            std::set<item_hash_t> sync_items_to_request;

            // for each idle peer that we're syncing with
            fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
            for( const peer_connection_ptr& peer : _active_connections )
            {
              if( peer->we_need_sync_items_from_peer &&
                  // if we've already scheduled a request for this peer, don't consider scheduling another
                  sync_item_requests_to_send.find(peer) == sync_item_requests_to_send.end() &&
                  peer->idle() )
              {
                if (!peer->inhibit_fetching_sync_blocks)
                {
                  // loop through the items it has that we don't yet have on our blockchain
                  for( const auto& item_to_potentially_request : peer->ids_of_items_to_get )
                  {
                    // if we don't already have this item in our temporary storage
                    // and we haven't requested from another syncing peer
                    if( // already got it, but for some reson it's still in our list of items to fetch
                        !have_already_received_sync_item(item_to_potentially_request) &&
                        // we have already decided to request it from another peer during this iteration
                        sync_items_to_request.find(item_to_potentially_request) == sync_items_to_request.end() &&
                        // we've requested it in a previous iteration and we're still waiting for it to arrive
                        _active_sync_requests.find(item_to_potentially_request) == _active_sync_requests.end() )
                    {
                      // then schedule a request from this peer
                      sync_item_requests_to_send[peer].push_back(item_to_potentially_request);
                      sync_items_to_request.insert( item_to_potentially_request );
                      if (sync_item_requests_to_send[peer].size() >= _max_sync_blocks_per_peer)
                        break;
                    }
                  }
                }
              }
            }
          } // end non-preemptable section

          // make all the requests we scheduled in the loop above
          for( auto sync_item_request : sync_item_requests_to_send )
            request_sync_items_from_peer( sync_item_request.first, sync_item_request.second );
          sync_item_requests_to_send.clear();
        }
        else
          dlog("fetch_sync_items_loop is suspended pending backlog processing");

        if( !_sync_items_to_fetch_updated )
        {
          dlog( "no sync items to fetch right now, going to sleep" );
          _retrigger_fetch_sync_items_loop_promise
                = fc::promise<void>::create("graphene::net::retrigger_fetch_sync_items_loop");
          _retrigger_fetch_sync_items_loop_promise->wait();
          _retrigger_fetch_sync_items_loop_promise.reset();
        }
      } // while( !canceled )
    }

    void node_impl::trigger_fetch_sync_items_loop()
    {
      VERIFY_CORRECT_THREAD();
      dlog( "Triggering fetch sync items loop now" );
      _sync_items_to_fetch_updated = true;
      if( _retrigger_fetch_sync_items_loop_promise )
        _retrigger_fetch_sync_items_loop_promise->set_value();
    }

    bool node_impl::is_item_in_any_peers_inventory(const item_id& item) const
    {
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for( const peer_connection_ptr& peer : _active_connections )
      {
        if (peer->inventory_peer_advertised_to_us.find(item) != peer->inventory_peer_advertised_to_us.end() )
          return true;
      }
      return false;
    }

    void node_impl::fetch_items_loop()
    {
      VERIFY_CORRECT_THREAD();
      while (!_fetch_item_loop_done.canceled())
      {
        _items_to_fetch_updated = false;
        dlog("beginning an iteration of fetch items (${count} items to fetch)",
             ("count", _items_to_fetch.size()));

        fc::time_point oldest_timestamp_to_fetch = fc::time_point::now()
              - fc::seconds(_recent_block_interval_seconds * GRAPHENE_NET_MESSAGE_CACHE_DURATION_IN_BLOCKS);
        fc::time_point next_peer_unblocked_time = fc::time_point::maximum();

        // we need to construct a list of items to request from each peer first,
        // then send the messages (in two steps, to avoid yielding while iterating)
        // we want to evenly distribute our requests among our peers.
        struct requested_item_count_index {};
        struct peer_and_items_to_fetch
        {
          peer_connection_ptr peer;
          std::vector<item_id> item_ids;
          peer_and_items_to_fetch(const peer_connection_ptr& peer) : peer(peer) {}
          bool operator<(const peer_and_items_to_fetch& rhs) const { return peer < rhs.peer; }
          size_t number_of_items() const { return item_ids.size(); }
        };
        using fetch_messages_to_send_set = boost::multi_index_container< peer_and_items_to_fetch, bmi::indexed_by<
                 bmi::ordered_unique<
                    bmi::member<peer_and_items_to_fetch, peer_connection_ptr, &peer_and_items_to_fetch::peer> >,
                 bmi::ordered_non_unique< bmi::tag<requested_item_count_index>,
                    bmi::const_mem_fun<peer_and_items_to_fetch, size_t, &peer_and_items_to_fetch::number_of_items> >
                 > >;
        fetch_messages_to_send_set items_by_peer;

        // initialize the fetch_messages_to_send with an empty set of items for all idle peers
        {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& peer : _active_connections)
            if (peer->idle())
               items_by_peer.insert(peer_and_items_to_fetch(peer));
        }

        // now loop over all items we want to fetch
        for (auto item_iter = _items_to_fetch.begin(); item_iter != _items_to_fetch.end();)
        {
          if (item_iter->timestamp < oldest_timestamp_to_fetch)
          {
            // this item has probably already fallen out of our peers' caches, we'll just ignore it.
            // this can happen during flooding, and the _items_to_fetch could otherwise get clogged
            // with a bunch of items that we'll never be able to request from any peer
            wlog("Unable to fetch item ${item} before its likely expiration time, "
                 "removing it from our list of items to fetch",
                 ("item", item_iter->item));
            item_iter = _items_to_fetch.erase(item_iter);
          }
          else
          {
            // find a peer that has it, we'll use the one who has the least requests going to it to load balance
            bool item_fetched = false;
            for (auto peer_iter = items_by_peer.get<requested_item_count_index>().begin(); peer_iter != items_by_peer.get<requested_item_count_index>().end(); ++peer_iter)
            {
              const peer_connection_ptr& peer = peer_iter->peer;
              // if they have the item and we haven't already decided to ask them for too many other items
              if (peer_iter->item_ids.size() < GRAPHENE_NET_MAX_ITEMS_PER_PEER_DURING_NORMAL_OPERATION &&
                  peer->inventory_peer_advertised_to_us.find(item_iter->item) != peer->inventory_peer_advertised_to_us.end())
              {
                if (item_iter->item.item_type == graphene::net::trx_message_type && peer->is_transaction_fetching_inhibited())
                  next_peer_unblocked_time = std::min(peer->transaction_fetching_inhibited_until, next_peer_unblocked_time);
                else
                {
                  //dlog("requesting item ${hash} from peer ${endpoint}",
                  //     ("hash", iter->item.item_hash)("endpoint", peer->get_remote_endpoint()));
                  item_id item_id_to_fetch = item_iter->item;
                  peer->items_requested_from_peer.insert(peer_connection::item_to_time_map_type::value_type(
                        item_id_to_fetch, fc::time_point::now()));
                  item_iter = _items_to_fetch.erase(item_iter);
                  item_fetched = true;
                  items_by_peer.get<requested_item_count_index>().modify(peer_iter,
                        [&item_id_to_fetch](peer_and_items_to_fetch& peer_and_items) {
                           peer_and_items.item_ids.push_back(item_id_to_fetch);
                  });
                  break;
                }
              }
            }
            if (!item_fetched)
              ++item_iter;
          }
        }

        // we've figured out which peer will be providing each item, now send the messages.
        for (const peer_and_items_to_fetch& peer_and_items : items_by_peer)
        {
          // the item lists are heterogenous and
          // the fetch_items_message can only deal with one item type at a time.
          std::map<uint32_t, std::vector<item_hash_t> > items_to_fetch_by_type;
          for (const item_id& item : peer_and_items.item_ids)
            items_to_fetch_by_type[item.item_type].push_back(item.item_hash);
          for (auto& items_by_type : items_to_fetch_by_type)
          {
            dlog("requesting ${count} items of type ${type} from peer ${endpoint}: ${hashes}",
                 ("count", items_by_type.second.size())("type", (uint32_t)items_by_type.first)
                 ("endpoint", peer_and_items.peer->get_remote_endpoint())
                 ("hashes", items_by_type.second));
            peer_and_items.peer->send_message(fetch_items_message(items_by_type.first,
                                                                  items_by_type.second));
          }
        }
        items_by_peer.clear();

        if (!_items_to_fetch_updated)
        {
          _retrigger_fetch_item_loop_promise = fc::promise<void>::create("graphene::net::retrigger_fetch_item_loop");
          fc::microseconds time_until_retrigger = fc::microseconds::maximum();
          if (next_peer_unblocked_time != fc::time_point::maximum())
            time_until_retrigger = next_peer_unblocked_time - fc::time_point::now();
          try
          {
            if (time_until_retrigger > fc::microseconds(0))
              _retrigger_fetch_item_loop_promise->wait(time_until_retrigger);
          }
          catch (const fc::timeout_exception&)
          {
            dlog("Resuming fetch_items_loop due to timeout -- one of our peers should no longer be throttled");
          }
          _retrigger_fetch_item_loop_promise.reset();
        }
      } // while !canceled
    }

    void node_impl::trigger_fetch_items_loop()
    {
      VERIFY_CORRECT_THREAD();
      _items_to_fetch_updated = true;
      if( _retrigger_fetch_item_loop_promise )
        _retrigger_fetch_item_loop_promise->set_value();
    }

    void node_impl::advertise_inventory_loop()
    {
      VERIFY_CORRECT_THREAD();
      while (!_advertise_inventory_loop_done.canceled())
      {
        dlog("beginning an iteration of advertise inventory");
        // swap inventory into local variable, clearing the node's copy
        std::unordered_set<item_id> inventory_to_advertise;
        _new_inventory.swap( inventory_to_advertise );

        // process all inventory to advertise and construct the inventory messages we'll send
        // first, then send them all in a batch (to avoid any fiber interruption points while
        // we're computing the messages)
        std::list<std::pair<peer_connection_ptr, item_ids_inventory_message> > inventory_messages_to_send;
        {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& peer : _active_connections)
         {
          // only advertise to peers who are in sync with us
          //idump((peer->peer_needs_sync_items_from_us)); // for debug
          if( !peer->peer_needs_sync_items_from_us )
          {
            std::map<uint32_t, std::vector<item_hash_t> > items_to_advertise_by_type;
            // don't send the peer anything we've already advertised to it
            // or anything it has advertised to us
            // group the items we need to send by type, because we'll need to send one inventory message per type
            size_t total_items_to_send = 0;
            //idump((inventory_to_advertise)); // for debug
            for (const item_id& item_to_advertise : inventory_to_advertise)
            {
               auto adv_to_peer = peer->inventory_advertised_to_peer.find(item_to_advertise);
               auto adv_to_us   = peer->inventory_peer_advertised_to_us.find(item_to_advertise);

              if (adv_to_peer == peer->inventory_advertised_to_peer.end() &&
                  adv_to_us == peer->inventory_peer_advertised_to_us.end())
              {
                items_to_advertise_by_type[item_to_advertise.item_type].push_back(item_to_advertise.item_hash);
                peer->inventory_advertised_to_peer.insert(
                         peer_connection::timestamped_item_id(item_to_advertise, fc::time_point::now()));
                ++total_items_to_send;
                if (item_to_advertise.item_type == trx_message_type)
                  testnetlog("advertising transaction ${id} to peer ${endpoint}",
                             ("id", item_to_advertise.item_hash)("endpoint", peer->get_remote_endpoint()));
                dlog("advertising item ${id} to peer ${endpoint}",
                     ("id", item_to_advertise.item_hash)("endpoint", peer->get_remote_endpoint()));
              }
              else
              {
                 if( adv_to_peer != peer->inventory_advertised_to_peer.end() )
                    dlog( "adv_to_peer != peer->inventory_advertised_to_peer.end() : ${adv_to_peer}",
                          ("adv_to_peer", *adv_to_peer) );
                 if( adv_to_us != peer->inventory_peer_advertised_to_us.end() )
                    dlog( "adv_to_us != peer->inventory_peer_advertised_to_us.end() : ${adv_to_us}",
                          ("adv_to_us", *adv_to_us) );
              }
            }
              dlog("advertising ${count} new item(s) of ${types} type(s) to peer ${endpoint}",
                   ("count", total_items_to_send)
                   ("types", items_to_advertise_by_type.size())
                   ("endpoint", peer->get_remote_endpoint()));
            for (auto items_group : items_to_advertise_by_type)
            {
               inventory_messages_to_send.emplace_back(std::make_pair(
                     peer, item_ids_inventory_message(items_group.first, items_group.second)));
            }
          }
          peer->clear_old_inventory();
         }
        } // lock_guard

        for (auto iter = inventory_messages_to_send.begin(); iter != inventory_messages_to_send.end(); ++iter)
          iter->first->send_message(iter->second);
        inventory_messages_to_send.clear();

        if (_new_inventory.empty())
        {
          _retrigger_advertise_inventory_loop_promise
                = fc::promise<void>::create("graphene::net::retrigger_advertise_inventory_loop");
          _retrigger_advertise_inventory_loop_promise->wait();
          _retrigger_advertise_inventory_loop_promise.reset();
        }
      } // while(!canceled)
    }

    void node_impl::trigger_advertise_inventory_loop()
    {
      VERIFY_CORRECT_THREAD();
      if( _retrigger_advertise_inventory_loop_promise )
        _retrigger_advertise_inventory_loop_promise->set_value();
    }

    void node_impl::kill_inactive_conns_loop(node_impl_ptr self)
    {
      VERIFY_CORRECT_THREAD();
      std::list<peer_connection_ptr> peers_to_disconnect_gently;
      std::list<peer_connection_ptr> peers_to_disconnect_forcibly;
      std::list<peer_connection_ptr> peers_to_send_keep_alive;
      std::list<peer_connection_ptr> peers_to_terminate;

     try {
      // Note: if the node is shutting down, it's possible that _delegate is already unusable,
      //       in this case, we'll get an exception
      _recent_block_interval_seconds = _delegate->get_current_block_interval_in_seconds();

      // Disconnect peers that haven't sent us any data recently
      // These numbers are just guesses and we need to think through how this works better.
      // If we and our peers get disconnected from the rest of the network, we will not
      // receive any blocks or transactions from the rest of the world, and that will
      // probably make us disconnect from our peers even though we have working connections to
      // them (but they won't have sent us anything since they aren't getting blocks either).
      // This might not be so bad because it could make us initiate more connections and
      // reconnect with the rest of the network, or it might just futher isolate us.
      // As usual, the first step is to walk through all our peers and figure out which
      // peers need action (disconneting, sending keepalives, etc), then we walk through
      // those lists yielding at our leisure later.

      uint32_t handshaking_timeout = _peer_inactivity_timeout;
      fc::time_point handshaking_disconnect_threshold = fc::time_point::now() - fc::seconds(handshaking_timeout);
      {
         fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
         for( const peer_connection_ptr& handshaking_peer : _handshaking_connections )
         {
            if( handshaking_peer->connection_initiation_time < handshaking_disconnect_threshold &&
                  handshaking_peer->get_last_message_received_time() < handshaking_disconnect_threshold &&
               handshaking_peer->get_last_message_sent_time() < handshaking_disconnect_threshold )
            {
               wlog( "Forcibly disconnecting from handshaking peer ${peer} due to inactivity of at least ${timeout} seconds",
                     ( "peer", handshaking_peer->get_remote_endpoint() )("timeout", handshaking_timeout ) );
               wlog("Peer's negotiating status: ${status}, bytes sent: ${sent}, bytes received: ${received}",
                     ("status", handshaking_peer->negotiation_status)
                     ("sent", handshaking_peer->get_total_bytes_sent())
                     ("received", handshaking_peer->get_total_bytes_received()));
               handshaking_peer->connection_closed_error = fc::exception(FC_LOG_MESSAGE(warn, 
                     "Terminating handshaking connection due to inactivity of ${timeout} seconds.  Negotiating status: ${status}, bytes sent: ${sent}, bytes received: ${received}",
                     ("peer", handshaking_peer->get_remote_endpoint())
                     ("timeout", handshaking_timeout)
                     ("status", handshaking_peer->negotiation_status)
                     ("sent", handshaking_peer->get_total_bytes_sent())
                     ("received", handshaking_peer->get_total_bytes_received())));
               peers_to_disconnect_forcibly.push_back( handshaking_peer );
            } // if
         } // for
      } // scoped_lock
      // timeout for any active peers is two block intervals
      uint32_t active_disconnect_timeout = 10 * _recent_block_interval_seconds;
      uint32_t active_send_keepalive_timeout = active_disconnect_timeout / 2;

      // set the ignored request time out to 6 second.  When we request a block
      // or transaction from a peer, this timeout determines how long we wait for them
      // to reply before we give up and ask another peer for the item.
      // Ideally this should be significantly shorter than the block interval, because
      // we'd like to realize the block isn't coming and fetch it from a different
      // peer before the next block comes in.
      // Increased to 6 from 1 in #1660 due to heavy load. May need to adjust further
      // Note: #1660 is https://github.com/steemit/steem/issues/1660
      fc::microseconds active_ignored_request_timeout = fc::seconds(6);

      fc::time_point active_disconnect_threshold = fc::time_point::now() - fc::seconds(active_disconnect_timeout);
      fc::time_point active_send_keepalive_threshold = fc::time_point::now() - fc::seconds(active_send_keepalive_timeout);
      fc::time_point active_ignored_request_threshold = fc::time_point::now() - active_ignored_request_timeout;
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());

         for( const peer_connection_ptr& active_peer : _active_connections )
         {
            if( active_peer->connection_initiation_time < active_disconnect_threshold &&
                  active_peer->get_last_message_received_time() < active_disconnect_threshold )
            {
               wlog( "Closing connection with peer ${peer} due to inactivity of at least ${timeout} seconds",
                     ( "peer", active_peer->get_remote_endpoint() )("timeout", active_disconnect_timeout ) );
               peers_to_disconnect_gently.push_back( active_peer );
            }
            else
            {
               bool disconnect_due_to_request_timeout = false;
               if (!active_peer->sync_items_requested_from_peer.empty() &&
                  active_peer->last_sync_item_received_time < active_ignored_request_threshold)
               {
                  wlog("Disconnecting peer ${peer} because they haven't made any progress on my remaining ${count} sync item requests",
                        ("peer", active_peer->get_remote_endpoint())("count",
                        active_peer->sync_items_requested_from_peer.size()));
                  disconnect_due_to_request_timeout = true;
               }
               if (!disconnect_due_to_request_timeout &&
                  active_peer->item_ids_requested_from_peer &&
                  active_peer->item_ids_requested_from_peer->get<1>() < active_ignored_request_threshold)
               {
                  wlog("Disconnecting peer ${peer} because they didn't respond to my request for sync item ids after ${synopsis}",
                        ("peer", active_peer->get_remote_endpoint())
                        ("synopsis", active_peer->item_ids_requested_from_peer->get<0>()));
                  disconnect_due_to_request_timeout = true;
               }
               if (!disconnect_due_to_request_timeout)
                  for (const peer_connection::item_to_time_map_type::value_type& item_and_time : active_peer->items_requested_from_peer)
                     if (item_and_time.second < active_ignored_request_threshold)
                  {
                     wlog("Disconnecting peer ${peer} because they didn't respond to my request for item ${id}",
                           ("peer", active_peer->get_remote_endpoint())("id", item_and_time.first.item_hash));
                     disconnect_due_to_request_timeout = true;
                     break;
                  }
               if (disconnect_due_to_request_timeout)
               {
                  // we should probably disconnect nicely and give them a reason, but right now the logic
                  // for rescheduling the requests only executes when the connection is fully closed,
                  // and we want to get those requests rescheduled as soon as possible
                  peers_to_disconnect_forcibly.push_back(active_peer);
               }
               else if (active_peer->connection_initiation_time < active_send_keepalive_threshold &&
                     active_peer->get_last_message_received_time() < active_send_keepalive_threshold)
               {
                  wlog( "Sending a keepalive message to peer ${peer} who hasn't sent us any messages in the last ${timeout} seconds",
                        ( "peer", active_peer->get_remote_endpoint() )("timeout", active_send_keepalive_timeout ) );
                  peers_to_send_keep_alive.push_back(active_peer);
               }
               else if (active_peer->we_need_sync_items_from_peer && 
                     !active_peer->is_currently_handling_message() &&
                     !active_peer->item_ids_requested_from_peer &&
                     active_peer->ids_of_items_to_get.empty())
               {
                  // This is a state we should never get into in the first place, but if we do, we should disconnect the peer
                  // to re-establish the connection.
                  fc_wlog(fc::logger::get("sync"), "Disconnecting peer ${peer} because we think we need blocks from them but sync has stalled.",
                        ("peer", active_peer->get_remote_endpoint()));
                  wlog("Disconnecting peer ${peer} because we think we need blocks from them but sync has stalled.",
                        ("peer", active_peer->get_remote_endpoint()));
                  peers_to_disconnect_forcibly.push_back(active_peer);
               }
            } // else
         } // for
      } // scoped_lock

      fc::time_point closing_disconnect_threshold = fc::time_point::now() - fc::seconds(GRAPHENE_NET_PEER_DISCONNECT_TIMEOUT);
      {
         fc::scoped_lock<fc::mutex> lock(_closing_connections.get_mutex());
         for( const peer_connection_ptr& closing_peer : _closing_connections )
         {
            if( closing_peer->connection_closed_time < closing_disconnect_threshold )
            {
               // we asked this peer to close their connectoin to us at least GRAPHENE_NET_PEER_DISCONNECT_TIMEOUT
               // seconds ago, but they haven't done it yet.  Terminate the connection now
               wlog( "Forcibly disconnecting peer ${peer} who failed to close their connection in a timely manner",
                     ( "peer", closing_peer->get_remote_endpoint() ) );
               peers_to_disconnect_forcibly.push_back( closing_peer );
            }
         } // for
      } // scoped_lock
      uint32_t failed_terminate_timeout_seconds = 120;
      fc::time_point failed_terminate_threshold = fc::time_point::now() - fc::seconds(failed_terminate_timeout_seconds);
      {
         fc::scoped_lock<fc::mutex> lock(_terminating_connections.get_mutex());
         for (const peer_connection_ptr& peer : _terminating_connections )
         {
            if (peer->get_connection_terminated_time() != fc::time_point::min() &&
               peer->get_connection_terminated_time() < failed_terminate_threshold)
            {
               wlog("Terminating connection with peer ${peer}, closing the connection didn't work", ("peer", peer->get_remote_endpoint()));
               peers_to_terminate.push_back(peer);
            }
         }
      } // scoped_lock
      // That's the end of the sorting step; now all peers that require further processing are now in one of the
      // lists peers_to_disconnect_gently,  peers_to_disconnect_forcibly, peers_to_send_keep_alive, or peers_to_terminate

      // if we've decided to delete any peers, do it now; in its current implementation this doesn't yield,
      // and once we start yielding, we may find that we've moved that peer to another list (closed or active)
      // and that triggers assertions, maybe even errors
      {
         fc::scoped_lock<fc::mutex> lock(_terminating_connections.get_mutex());
         for (const peer_connection_ptr& peer : peers_to_terminate )
         {
            assert(_terminating_connections.find(peer) != _terminating_connections.end());
            _terminating_connections.erase(peer);
            schedule_peer_for_deletion(peer);
         }
      } // scoped_lock
      peers_to_terminate.clear();

      // if we're going to abruptly disconnect anyone, do it here
      // (it doesn't yield).  I don't think there would be any harm if this were
      // moved to the yielding section
      for( const peer_connection_ptr& peer : peers_to_disconnect_forcibly )
      {
         move_peer_to_terminating_list(peer);
         peer->close_connection();
      }
      peers_to_disconnect_forcibly.clear();

      // Now process the peers that we need to do yielding functions with (disconnect sends a message with the
      // disconnect reason, so it may yield)
      for( const peer_connection_ptr& peer : peers_to_disconnect_gently )
      {
         {
            fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
            fc::exception detailed_error( FC_LOG_MESSAGE(warn, "Disconnecting due to inactivity",
                  ( "last_message_received_seconds_ago", (peer->get_last_message_received_time()
                  - fc::time_point::now() ).count() / fc::seconds(1 ).count() )
                  ( "last_message_sent_seconds_ago", (peer->get_last_message_sent_time()
                  - fc::time_point::now() ).count() / fc::seconds(1 ).count() )
                  ( "inactivity_timeout", _active_connections.find(peer ) != _active_connections.end()
                  ? _peer_inactivity_timeout * 10 : _peer_inactivity_timeout ) ) );
            disconnect_from_peer( peer.get(), "Disconnecting due to inactivity", false, detailed_error );
         }
      }
      peers_to_disconnect_gently.clear();

      for( const peer_connection_ptr& peer : peers_to_send_keep_alive )
        peer->send_message(current_time_request_message(),
                           offsetof(current_time_request_message, request_sent_time));
      peers_to_send_keep_alive.clear();

     } catch( const fc::exception& e ) {
         wlog( "Exception caught in kill_inactive_conns_loop: ${e}", ("e",e.to_detail_string()) );
         // If the node is shutting down, we just quit, no need to throw.
         // If the node is not shutting down, the old code will throw, which means we won't schedule a new loop,
         // likely it's unexpected behavior.
         // Thus we don't throw here.
     }

      if (!_node_is_shutting_down && !_kill_inactive_conns_loop_done.canceled())
      {
         _kill_inactive_conns_loop_done = fc::schedule(
               [this,self](){ kill_inactive_conns_loop(self); },
               fc::time_point::now() + fc::seconds(GRAPHENE_NET_PEER_HANDSHAKE_INACTIVITY_TIMEOUT / 2),
               "kill_inactive_conns_loop" );
      }
    }

    void node_impl::fetch_updated_peer_lists_loop()
    {
      VERIFY_CORRECT_THREAD();
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         // JMJ 2018-10-22 Unsure why we're making a copy here, but this is probably unnecessary
         std::list<peer_connection_ptr> original_active_peers(_active_connections.begin(), _active_connections.end());
         for( const peer_connection_ptr& active_peer : original_active_peers )
         {
            try
            {
               active_peer->expecting_address_message = true;
               active_peer->send_message(address_request_message());
            }
            catch ( const fc::canceled_exception& )
            {
               throw;
            }
            catch (const fc::exception& e)
            {
               dlog("Caught exception while sending address request message to peer ${peer} : ${e}",
                     ("peer", active_peer->get_remote_endpoint())("e", e));
            }
         }
      }

      // this has nothing to do with updating the peer list, but we need to prune this list
      // at regular intervals, this is a fine place to do it.
      fc::time_point_sec oldest_failed_ids_to_keep(fc::time_point::now() - fc::minutes(15));
      auto oldest_failed_ids_to_keep_iter = _recently_failed_items.get<peer_connection::timestamp_index>()
                                                                  .lower_bound(oldest_failed_ids_to_keep);
      auto begin_iter = _recently_failed_items.get<peer_connection::timestamp_index>().begin();
      _recently_failed_items.get<peer_connection::timestamp_index>()
                            .erase(begin_iter, oldest_failed_ids_to_keep_iter);

      if (!_node_is_shutting_down && !_fetch_updated_peer_lists_loop_done.canceled() )
         _fetch_updated_peer_lists_loop_done = fc::schedule( [this](){ fetch_updated_peer_lists_loop(); },
                                                             fc::time_point::now() + fc::minutes(15),
                                                             "fetch_updated_peer_lists_loop" );
    }
    void node_impl::update_bandwidth_data(uint32_t bytes_read_this_second, uint32_t bytes_written_this_second)
    {
      VERIFY_CORRECT_THREAD();
      _avg_net_read_speed_seconds.push_back(bytes_read_this_second);
      _avg_net_write_speed_seconds.push_back(bytes_written_this_second);
      ++_avg_net_usage_second_counter;
      constexpr uint8_t seconds_per_minute = 60;
      constexpr uint8_t minutes_per_hour = 60;
      if (_avg_net_usage_second_counter >= seconds_per_minute)
      {
        _avg_net_usage_second_counter = 0;
        ++_avg_net_usage_minute_counter;
        uint32_t average_read_this_minute = (uint32_t)boost::accumulate(_avg_net_read_speed_seconds, uint64_t(0))
                                          / (uint32_t)_avg_net_read_speed_seconds.size();
        _avg_net_read_speed_minutes.push_back(average_read_this_minute);
        uint32_t average_written_this_minute = (uint32_t)boost::accumulate(_avg_net_write_speed_seconds, uint64_t(0))
                                             / (uint32_t)_avg_net_write_speed_seconds.size();
        _avg_net_write_speed_minutes.push_back(average_written_this_minute);
        if (_avg_net_usage_minute_counter >= minutes_per_hour)
        {
          _avg_net_usage_minute_counter = 0;
          uint32_t average_read_this_hour = (uint32_t)boost::accumulate(_avg_net_read_speed_minutes, uint64_t(0))
                                          / (uint32_t)_avg_net_read_speed_minutes.size();
          _avg_net_read_speed_hours.push_back(average_read_this_hour);
          uint32_t average_written_this_hour = (uint32_t)boost::accumulate(_avg_net_write_speed_minutes, uint64_t(0))
                                             / (uint32_t)_avg_net_write_speed_minutes.size();
          _avg_net_write_speed_hours.push_back(average_written_this_hour);
        }
      }
    }
    void node_impl::bandwidth_monitor_loop()
    {
      VERIFY_CORRECT_THREAD();
      fc::time_point_sec current_time = fc::time_point::now();

      if (_bandwidth_monitor_last_update_time == fc::time_point_sec::min())
        _bandwidth_monitor_last_update_time = current_time;

      uint32_t seconds_since_last_update = current_time.sec_since_epoch()
                                         - _bandwidth_monitor_last_update_time.sec_since_epoch();
      seconds_since_last_update = std::max(UINT32_C(1), seconds_since_last_update);
      uint32_t bytes_read_this_second = _rate_limiter.get_actual_download_rate();
      uint32_t bytes_written_this_second = _rate_limiter.get_actual_upload_rate();
      for (uint32_t i = 0; i < seconds_since_last_update - 1; ++i)
        update_bandwidth_data(0, 0);
      update_bandwidth_data(bytes_read_this_second, bytes_written_this_second);
      _bandwidth_monitor_last_update_time = current_time;

      if (!_node_is_shutting_down && !_bandwidth_monitor_loop_done.canceled())
        _bandwidth_monitor_loop_done = fc::schedule( [=](){ bandwidth_monitor_loop(); },
                                                     fc::time_point::now() + fc::seconds(1),
                                                     "bandwidth_monitor_loop" );
    }

    void node_impl::dump_node_status_task()
    {
      VERIFY_CORRECT_THREAD();
      dump_node_status();
      if (!_node_is_shutting_down && !_dump_node_status_task_done.canceled())
        _dump_node_status_task_done = fc::schedule([=](){ dump_node_status_task(); },
                                                   fc::time_point::now() + fc::minutes(1),
                                                   "dump_node_status_task");
    }

    void node_impl::delayed_peer_deletion_task()
    {
      VERIFY_CORRECT_THREAD();
#ifdef USE_PEERS_TO_DELETE_MUTEX
      fc::scoped_lock<fc::mutex> lock(_peers_to_delete_mutex);
      dlog("in delayed_peer_deletion_task with ${count} in queue", ("count", _peers_to_delete.size()));
      _peers_to_delete.clear();
      dlog("_peers_to_delete cleared");
#else
      while (!_peers_to_delete.empty())
      {
        std::list<peer_connection_ptr> peers_to_delete_copy;
        dlog("beginning an iteration of delayed_peer_deletion_task with ${count} in queue",
             ("count", _peers_to_delete.size()));
        peers_to_delete_copy.swap(_peers_to_delete);
      }
      dlog("leaving delayed_peer_deletion_task");
#endif
    }

    void node_impl::schedule_peer_for_deletion(const peer_connection_ptr& peer_to_delete)
    {
      VERIFY_CORRECT_THREAD();

      assert(_handshaking_connections.find(peer_to_delete) == _handshaking_connections.end());
      assert(_active_connections.find(peer_to_delete) == _active_connections.end());
      assert(_closing_connections.find(peer_to_delete) == _closing_connections.end());
      assert(_terminating_connections.find(peer_to_delete) == _terminating_connections.end());

#ifdef USE_PEERS_TO_DELETE_MUTEX
      dlog("scheduling peer for deletion: ${peer} (may block on a mutex here)",
           ("peer", peer_to_delete->get_remote_endpoint()));

      size_t number_of_peers_to_delete;
      {
        fc::scoped_lock<fc::mutex> lock(_peers_to_delete_mutex);
        _peers_to_delete.emplace_back(peer_to_delete);
        number_of_peers_to_delete = _peers_to_delete.size();
      }
      dlog("peer scheduled for deletion: ${peer}", ("peer", peer_to_delete->get_remote_endpoint()));

      if (!_node_is_shutting_down &&
          (!_delayed_peer_deletion_task_done.valid() || _delayed_peer_deletion_task_done.ready()))
      {
        dlog("asyncing delayed_peer_deletion_task to delete ${size} peers",
             ("size", number_of_peers_to_delete));
        _delayed_peer_deletion_task_done = fc::async([this](){ delayed_peer_deletion_task(); },
                                                     "delayed_peer_deletion_task" );
    }
      else
        dlog("delayed_peer_deletion_task is already scheduled (current size of _peers_to_delete is ${size})",
             ("size", number_of_peers_to_delete));
#else
      dlog("scheduling peer for deletion: ${peer} (this will not block)",
           ("peer", peer_to_delete->get_remote_endpoint()));
      _peers_to_delete.push_back(peer_to_delete);
      if (!_node_is_shutting_down &&
          (!_delayed_peer_deletion_task_done.valid() || _delayed_peer_deletion_task_done.ready()))
      {
        dlog("asyncing delayed_peer_deletion_task to delete ${size} peers", ("size", _peers_to_delete.size()));
        _delayed_peer_deletion_task_done = fc::async([this](){ delayed_peer_deletion_task(); },
                                                     "delayed_peer_deletion_task" );
      }
      else
        dlog("delayed_peer_deletion_task is already scheduled (current size of _peers_to_delete is ${size})",
             ("size", _peers_to_delete.size()));

#endif
    }

    bool node_impl::is_accepting_new_connections()
    {
      VERIFY_CORRECT_THREAD();
      return !_p2p_network_connect_loop_done.canceled()
             && get_number_of_connections() <= _maximum_number_of_connections;
    }

    bool node_impl::is_wanting_new_connections()
    {
      VERIFY_CORRECT_THREAD();
      return !_p2p_network_connect_loop_done.canceled()
             && get_number_of_connections() < _desired_number_of_connections;
    }

    uint32_t node_impl::get_number_of_connections()
    {
      VERIFY_CORRECT_THREAD();
      return (uint32_t)(_handshaking_connections.size() + _active_connections.size());
    }

    peer_connection_ptr node_impl::get_peer_by_node_id(const node_id_t& node_id) const
    {
      VERIFY_CORRECT_THREAD();
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& active_peer : _active_connections)
            if (node_id == active_peer->node_id)
               return active_peer;
      }
      {
         fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
         for (const peer_connection_ptr& handshaking_peer : _handshaking_connections)
            if (node_id == handshaking_peer->node_id)
               return handshaking_peer;
      }
      return peer_connection_ptr();
    }

    // merge addresses received from a peer into our database
    bool node_impl::merge_address_info_with_potential_peer_database(const std::vector<address_info> addresses)
    {
      VERIFY_CORRECT_THREAD();
      bool new_information_received = false;
      for (const address_info& address : addresses)
      {
         // If the peer's inbound port is 0, we don't add it to our peer database.
         // Although it should have been handled by the caller, be defensive here.
         if( 0 == address.remote_endpoint.port() )
            continue;
         // Note: if found, a copy is returned
         auto updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep(address.remote_endpoint);
         // Note:
         // We don't save node_id in the peer database so far
         // 1. node_id of that peer may have changed, but we don't check or update
         // 2. we don't check by node_id either, in case when a peer's IP address has changed, we don't handle it
         // 3. if the peer's inbound port is not 0, no matter if the address is reported as firewalled or not,
         //    we add it to our database and check by ourselves later
         if (address.last_seen_time > updated_peer_record.last_seen_time) // usually true, except when received from
                                                                          // multiple peers in the same second
         {
            new_information_received = true;
            updated_peer_record.last_seen_time = address.last_seen_time;
            _potential_peer_db.update_entry(updated_peer_record);
         }
      }
      // TODO maybe delete too old info by the way
      return new_information_received;
    }

    void node_impl::display_current_connections()
    {
      VERIFY_CORRECT_THREAD();
      dlog("Currently have ${current} of [${desired}/${max}] connections",
           ("current", get_number_of_connections())
           ("desired", _desired_number_of_connections)
           ("max", _maximum_number_of_connections));
      dlog("   my id is ${id}", ("id", _node_id));

      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& active_connection : _active_connections)
         {
            dlog("        active: ${endpoint} with ${id}   [${direction}]",
                  ("endpoint", active_connection->get_remote_endpoint())
                  ("id", active_connection->node_id)
                  ("direction", active_connection->direction));
         }
      }
      {
         fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
         for (const peer_connection_ptr& handshaking_connection : _handshaking_connections)
         {
            dlog("   handshaking: ${endpoint} with ${id}  [${direction}]",
                  ("endpoint", handshaking_connection->get_remote_endpoint())
                  ("id", handshaking_connection->node_id)
                  ("direction", handshaking_connection->direction));
         }
      }
    }

    void node_impl::on_message( peer_connection* originating_peer, const message& received_message )
    {
      VERIFY_CORRECT_THREAD();
      message_hash_type message_hash = received_message.id();
      dlog("handling message ${type} ${hash} size ${size} from peer ${endpoint}",
           ("type", graphene::net::core_message_type_enum(received_message.msg_type.value()))("hash", message_hash)
           ("size", received_message.size)
           ("endpoint", originating_peer->get_remote_endpoint()));
      // Gatekeeping code
      if( originating_peer->we_have_requested_close
          // allow hello_message so we can learn more about the peer
          && received_message.msg_type.value() != core_message_type_enum::hello_message_type
          // allow closing_connection_message so we can finish disconnecting
          && received_message.msg_type.value() != core_message_type_enum::closing_connection_message_type )
      {
         dlog( "Unexpected message from peer ${peer} while we have requested to close connection",
               ("peer", originating_peer->get_remote_endpoint()) );
         return;
      }
      switch ( received_message.msg_type.value() )
      {
      case core_message_type_enum::hello_message_type:
        on_hello_message(originating_peer, received_message.as<hello_message>());
        break;
      case core_message_type_enum::connection_accepted_message_type:
        on_connection_accepted_message(originating_peer, received_message.as<connection_accepted_message>());
        break;
      case core_message_type_enum::connection_rejected_message_type:
        on_connection_rejected_message(originating_peer, received_message.as<connection_rejected_message>());
        break;
      case core_message_type_enum::address_request_message_type:
        on_address_request_message(originating_peer, received_message.as<address_request_message>());
        break;
      case core_message_type_enum::address_message_type:
        on_address_message(originating_peer, received_message.as<address_message>());
        break;
      case core_message_type_enum::fetch_blockchain_item_ids_message_type:
        on_fetch_blockchain_item_ids_message(
              originating_peer, received_message.as<fetch_blockchain_item_ids_message>());
        break;
      case core_message_type_enum::blockchain_item_ids_inventory_message_type:
        on_blockchain_item_ids_inventory_message(
              originating_peer, received_message.as<blockchain_item_ids_inventory_message>());
        break;
      case core_message_type_enum::fetch_items_message_type:
        on_fetch_items_message(originating_peer, received_message.as<fetch_items_message>());
        break;
      case core_message_type_enum::item_not_available_message_type:
        on_item_not_available_message(originating_peer, received_message.as<item_not_available_message>());
        break;
      case core_message_type_enum::item_ids_inventory_message_type:
        on_item_ids_inventory_message(originating_peer, received_message.as<item_ids_inventory_message>());
        break;
      case core_message_type_enum::closing_connection_message_type:
        on_closing_connection_message(originating_peer, received_message.as<closing_connection_message>());
        break;
      case core_message_type_enum::block_message_type:
        process_block_message(originating_peer, received_message, message_hash);
        break;
      case core_message_type_enum::current_time_request_message_type:
        on_current_time_request_message(originating_peer, received_message.as<current_time_request_message>());
        break;
      case core_message_type_enum::current_time_reply_message_type:
        on_current_time_reply_message(originating_peer, received_message.as<current_time_reply_message>());
        break;
      case core_message_type_enum::check_firewall_message_type:
        break;
      case core_message_type_enum::check_firewall_reply_message_type:
        break;
      case core_message_type_enum::get_current_connections_request_message_type:
        break;
      case core_message_type_enum::get_current_connections_reply_message_type:
        break;

      default:
        // ignore any message in between core_message_type_first and _last that we don't handle above
        // to allow us to add messages in the future
        if (received_message.msg_type.value() < core_message_type_enum::core_message_type_first ||
            received_message.msg_type.value() > core_message_type_enum::core_message_type_last)
          process_ordinary_message(originating_peer, received_message, message_hash);
        break;
      }
    }


    fc::variant_object node_impl::generate_hello_user_data()
    {
      VERIFY_CORRECT_THREAD();
      // for the time being, shoehorn a bunch of properties into the user_data variant object,
      // which lets us add and remove fields without changing the protocol.  Once we
      // settle on what we really want in there, we'll likely promote them to first
      // class fields in the hello message
      fc::mutable_variant_object user_data;
      user_data["fc_git_revision_sha"] = fc::git_revision_sha;
      user_data["fc_git_revision_unix_timestamp"] = fc::git_revision_unix_timestamp;
#if defined( __APPLE__ )
      user_data["platform"] = "osx";
#elif defined( __OpenBSD__ )
      user_data["platform"] = "obsd";
#elif defined( __linux__ )
      user_data["platform"] = "linux";
#elif defined( _MSC_VER )
      user_data["platform"] = "win32";
#else
      user_data["platform"] = "other";
#endif
      user_data["bitness"] = sizeof(void*) * 8;

      user_data["node_id"] = fc::variant( _node_id, 1 );

      item_hash_t head_block_id = _delegate->get_head_block_id();
      user_data["last_known_block_hash"] = fc::variant( head_block_id, 1 );
      user_data["last_known_block_number"] = _delegate->get_block_number(head_block_id);
      user_data["last_known_block_time"] = _delegate->get_block_time(head_block_id);

      if (!_hard_fork_block_numbers.empty())
        user_data["last_known_fork_block_number"] = _hard_fork_block_numbers.back();

      return user_data;
    }
    void node_impl::parse_hello_user_data_for_peer(peer_connection* originating_peer, const fc::variant_object& user_data)
    {
      VERIFY_CORRECT_THREAD();
      // try to parse data out of the user_agent string
      if (user_data.contains("graphene_git_revision_sha"))
        originating_peer->graphene_git_revision_sha = user_data["graphene_git_revision_sha"].as_string();
      if (user_data.contains("graphene_git_revision_unix_timestamp"))
        originating_peer->graphene_git_revision_unix_timestamp = fc::time_point_sec(
              user_data["graphene_git_revision_unix_timestamp"].as<uint32_t>(1));
      if (user_data.contains("fc_git_revision_sha"))
        originating_peer->fc_git_revision_sha = user_data["fc_git_revision_sha"].as_string();
      if (user_data.contains("fc_git_revision_unix_timestamp"))
        originating_peer->fc_git_revision_unix_timestamp = fc::time_point_sec(
              user_data["fc_git_revision_unix_timestamp"].as<uint32_t>(1));
      if (user_data.contains("platform"))
        originating_peer->platform = user_data["platform"].as_string();
      if (user_data.contains("bitness"))
        originating_peer->bitness = user_data["bitness"].as<uint32_t>(1);
      if (user_data.contains("node_id"))
        originating_peer->node_id = user_data["node_id"].as<node_id_t>(1);
      if (user_data.contains("last_known_fork_block_number"))
        originating_peer->last_known_fork_block_number = user_data["last_known_fork_block_number"].as<uint32_t>(1);
    }

   void node_impl::on_hello_message( peer_connection* originating_peer, const hello_message& hello_message_received )
   {
      VERIFY_CORRECT_THREAD();
      auto remote_endpoint = originating_peer->get_remote_endpoint(); // Note: this returns a copy
      // Do gatekeeping first
      if( originating_peer->their_state != peer_connection::their_connection_state::just_connected )
      {
         // we can wind up here if we've connected to ourselves, and the source and
         // destination endpoints are the same, causing messages we send out
         // to arrive back on the initiating socket instead of the receiving
         // socket.  If we did a complete job of enumerating local addresses,
         // we could avoid directly connecting to ourselves, or at least detect
         // immediately when we did it and disconnect.

         // The only way I know of that we'd get an unexpected hello that we
         //  can't really guard against is if we do a simulatenous open, we
         // probably need to think through that case.  We're not attempting that
         // yet, though, so it's ok to just disconnect here.
         wlog( "Unexpected hello_message from peer ${peer}, disconnecting",
               ("peer", remote_endpoint) );
         disconnect_from_peer( originating_peer, "Received an unexpected hello_message" );
         return;
      }

      // Check chain_id
      if( hello_message_received.chain_id != _chain_id )
      {
         wlog( "Received hello message from peer ${peer} on a different chain: ${message}",
               ("peer", remote_endpoint)
               ("message", hello_message_received) );
         // If it is an outbound connection, make sure we won't reconnect to the peer soon
         if( peer_connection_direction::outbound == originating_peer->direction )
         {
            // Note: deleting is not the best approach since it can be readded soon and we will reconnect soon.
            //       Marking it "permanently rejected" is also not good enough since the peer can be "fixed".
            //       It seems the best approach is to reduce its weight significantly.
            greatly_delay_next_conn_to( this, *remote_endpoint );
         }
         // Now reject
         std::ostringstream rejection_message;
         rejection_message << "You're on a different chain than I am.  I'm on " << _chain_id.str() <<
                              " and you're on " << hello_message_received.chain_id.str();
         connection_rejected_message connection_rejected( _user_agent_string, core_protocol_version,
                                                          *remote_endpoint,
                                                          rejection_reason_code::different_chain,
                                                          rejection_message.str() );
         originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
         originating_peer->send_message( message(connection_rejected) );
         // for this type of message, we're immediately disconnecting this peer, instead of trying to
         // allowing her to ask us for peers (any of our peers will be on the same chain as us, so there's no
         // benefit of sharing them)
         disconnect_from_peer( originating_peer, "You are on a different chain from me" );
         return;
      }

      // Validate the peer's public key.
      // Note: the node_id in user_data is not verified.
      fc::optional<fc::ecc::public_key> expected_node_public_key;
      try
      {
         fc::sha256::encoder shared_secret_encoder;
         fc::sha512 shared_secret = originating_peer->get_shared_secret();
         shared_secret_encoder.write(shared_secret.data(), sizeof(shared_secret));
         expected_node_public_key = fc::ecc::public_key( hello_message_received.signed_shared_secret,
                                                         shared_secret_encoder.result(), false );
      }
      catch( const fc::exception& e )
      {
         wlog( "Error when validating signature in hello message from peer ${peer}: ${e}",
               ("peer", remote_endpoint)("e", e.to_detail_string()) );
      }

      if( !expected_node_public_key
          || hello_message_received.node_public_key != expected_node_public_key->serialize() )
      {
         wlog( "Invalid signature in hello message from peer ${peer}",
               ("peer", remote_endpoint) );
         connection_rejected_message connection_rejected( _user_agent_string, core_protocol_version,
                                                          *remote_endpoint,
                                                          rejection_reason_code::invalid_hello_message,
                                                          "Invalid signature in hello message" );
         originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
         originating_peer->send_message( message(connection_rejected) );
         // for this type of message, we're immediately disconnecting this peer
         disconnect_from_peer( originating_peer, connection_rejected.reason_string );
         return;
      }

      // this already_connected check must come before we fill in peer data below
      node_id_t peer_node_id = hello_message_received.node_public_key;
      try
      {
        peer_node_id = hello_message_received.user_data["node_id"].as<node_id_t>(1);
      }
      catch (const fc::exception&)
      {
        // either it's not there or it's not a valid session id.  either way, ignore.
        dlog( "Peer ${endpoint} sent us a hello message without a valid node_id in user_data",
              ("endpoint", remote_endpoint ) );
      }
      // The peer's node_id should not be null
      static const node_id_t null_node_id;
      if( null_node_id == peer_node_id )
      {
         wlog( "The node_id in the hello_message from peer ${peer} is null, disconnecting",
               ("peer", remote_endpoint) );
         disconnect_from_peer( originating_peer, "Your node_id in the hello_message is null" );
         return;
      }
      // Check whether the peer is myself
      if( _node_id == peer_node_id )
      {
         ilog( "Received a hello_message from peer ${peer} with id ${id} that is myself or claimed to be myself, "
               "rejection",
               ("peer", remote_endpoint)
               ("id", peer_node_id) );
         // If it is an outbound connection, make sure we won't reconnect to the peer soon
         if( peer_connection_direction::outbound == originating_peer->direction )
         {
            // Note: deleting is not the best approach since it can be readded soon and we will reconnect soon.
            //       Marking it "permanently rejected" is also not good enough since the peer can be "fixed".
            //       It seems the best approach is to reduce its weight significantly.
            greatly_delay_next_conn_to( this, *remote_endpoint );
         }
         // Now reject
         // Note: this can happen in rare cases if the peer is not actually myself but another node.
         //       Anyway, we see it as ourselves, reject it and disconnect it.
         connection_rejected_message connection_rejected( _user_agent_string, core_protocol_version,
                                                          *remote_endpoint,
                                                          rejection_reason_code::connected_to_self,
                                                          "I'm connecting to myself" );
         originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
         originating_peer->send_message( message(connection_rejected) );
         disconnect_from_peer( originating_peer, connection_rejected.reason_string );
         return;
      }
      // Get a pointer to an exising connection to the peer (if one exists) for later use
      peer_connection_ptr already_connected_peer = get_peer_by_node_id( peer_node_id );

      // store off the data provided in the hello message
      originating_peer->user_agent = hello_message_received.user_agent;
      originating_peer->node_public_key = hello_message_received.node_public_key;
      originating_peer->core_protocol_version = hello_message_received.core_protocol_version;
      originating_peer->inbound_address = hello_message_received.inbound_address;
      originating_peer->inbound_port = hello_message_received.inbound_port;
      originating_peer->outbound_port = hello_message_received.outbound_port;
      // Note: more data is stored after initialized remote_inbound_endpoint

      // For an outbound connection, we know the remote_inbound_endpoint already, so keep it unchanged.
      // For an inbound connection, we initialize it here.
      if( !originating_peer->remote_inbound_endpoint )
      {
         // Note: the data is not yet verified, so we need to use it with caution.
         //
         // We will advertise "remote_inbound_endpoint" when other peers request addresses.
         //
         // On the one hand, we want to advertise as accurate data as possible to other peers (we will try to verify),
         // on the other hand, we still want to advertise it to other peers if we didn't have a chance to verify it.
         //
         // When the peer is not listening (i.e. it tells us its inbound port is 0), the inbound address it tells us
         // may be invalid (e.g. 0.0.0.0), and we are not going to verify it anyway.
         // For observation purposes, we still advertise it to other peers, and we need to tell them an address,
         // so we use the address we see.
         //
         // In addition, by now, our list or exclude list for peer advertisement only contains IP endpoints but not
         // nodes' public keys (we can't use node_id because it changes every time the node restarts). Using a valid
         // address is better for the purpose.
         if( 0 == originating_peer->inbound_port )
            originating_peer->remote_inbound_endpoint = fc::ip::endpoint( remote_endpoint->get_address() );
         else if( originating_peer->inbound_address.is_public_address()
                  || originating_peer->inbound_address == remote_endpoint->get_address() )
            originating_peer->remote_inbound_endpoint = fc::ip::endpoint( originating_peer->inbound_address,
                                                                          originating_peer->inbound_port );
         else
            originating_peer->remote_inbound_endpoint = remote_endpoint;
      }

      // Note: store node_id after initialized remote_inbound_endpoint to avoid a race condition

      // will probably be overwritten in parse_hello_user_data_for_peer()
      originating_peer->node_id = hello_message_received.node_public_key;

      parse_hello_user_data_for_peer(originating_peer, hello_message_received.user_data);

      // if they didn't provide a last known fork, try to guess it
      if (originating_peer->last_known_fork_block_number == 0 &&
          originating_peer->graphene_git_revision_unix_timestamp)
      {
        uint32_t unix_timestamp = originating_peer->graphene_git_revision_unix_timestamp->sec_since_epoch();
        originating_peer->last_known_fork_block_number = _delegate->estimate_last_known_fork_from_git_revision_timestamp(unix_timestamp);
      }

      // now decide what to do with it
      if (originating_peer->last_known_fork_block_number != 0)
      {
          uint32_t next_fork_block_number = get_next_known_hard_fork_block_number(originating_peer->last_known_fork_block_number);
          if (next_fork_block_number != 0)
          {
            // we know about a fork they don't.  See if we've already passed that block.  If we have, don't let them
            // connect because we won't be able to give them anything useful
            uint32_t head_block_num = _delegate->get_block_number(_delegate->get_head_block_id());
            if (next_fork_block_number < head_block_num)
            {
#ifdef ENABLE_DEBUG_ULOGS
              ulog("Rejecting connection from peer because their version is too old.  Their version date: ${date}", ("date", originating_peer->graphene_git_revision_unix_timestamp));
#endif
              wlog("Received hello message from peer running a version of that can only understand blocks up to #${their_hard_fork}, but I'm at head block number #${my_block_number}",
                   ("their_hard_fork", next_fork_block_number)("my_block_number", head_block_num));
              std::ostringstream rejection_message;
              rejection_message << "Your client is outdated -- you can only understand blocks up to #" << next_fork_block_number << ", but I'm already on block #" << head_block_num;
              connection_rejected_message connection_rejected(_user_agent_string, core_protocol_version,
                                                              *remote_endpoint,
                                                              rejection_reason_code::unspecified,
                                                              rejection_message.str() );

              originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
              originating_peer->send_message(message(connection_rejected));
              // for this type of message, we're immediately disconnecting this peer, instead of trying to
              // allowing her to ask us for peers (any of our peers will be on the same chain as us, so there's no
              // benefit of sharing them)
              disconnect_from_peer(originating_peer, "Your client is too old, please upgrade");
              return;
            }
          }
      }

        if( peer_connection_ptr() != already_connected_peer )
        {
          // If it is an outbound connection, update the existing connection's inbound_endpoint.
          // Note: there may be a race condition that multiple tasks try to write the same data
          if( peer_connection_direction::outbound == originating_peer->direction
              && originating_peer->node_public_key == already_connected_peer->node_public_key )
          {
              auto already_connected_endpoint = already_connected_peer->get_remote_endpoint(); // This returns a copy
              ilog( "Verified that endpoint ${ep} is reachable and belongs to peer ${peer} with id ${id}",
                    ("ep", remote_endpoint)
                    ("peer", already_connected_endpoint)
                    ("id", already_connected_peer->node_id) );
              // Do not replace a verified public address with a private or local address.
              // Note: there is a scenario that some nodes in the same local network may have connected to each other,
              //         and of course some are outbound connections and some are inbound, so we are unable to update
              //         all the data, not to mention that their external addresses might be inaccessible to each
              //         other.
              //       Unless they are all configured with the "p2p-inbound-endpoint" option with an external address,
              //         even if they all start out connecting to each other's external addresses, at some point they
              //         may try to connect to each other's local addresses and possibly stay connected.
              //       In this case, if the nodes aren't configured with the "advertise-peer-algorithm" option and
              //         related options properly, when advertising connected peers to other peers, they may expose
              //         that they are in the same local network and connected to each other.
              //       On the other hand, when we skip updates in some cases, we may end up trying to reconnect soon
              //         and endlessly (which is addressed with additional_inbound_endpoints).
              already_connected_peer->additional_inbound_endpoints.insert( *remote_endpoint );
              if( peer_connection_direction::inbound == already_connected_peer->direction )
              {
                 already_connected_peer->potential_inbound_endpoints[*remote_endpoint]
                       = firewalled_state::not_firewalled;
              }
              if( already_connected_peer->is_firewalled != firewalled_state::not_firewalled // implies it's inbound
                  || remote_endpoint->get_address().is_public_address()
                  || !already_connected_peer->get_endpoint_for_connecting()->get_address().is_public_address() )
              {
                 ilog( "Saving verification result ${ep} for peer ${peer} with id ${id}",
                       ("ep", remote_endpoint)
                       ("peer", already_connected_endpoint)
                       ("id", already_connected_peer->node_id) );
                 already_connected_peer->remote_inbound_endpoint = remote_endpoint;
                 already_connected_peer->is_firewalled = firewalled_state::not_firewalled;
              }
              // If the already connected peer is in the active connections list, save the endpoint to the peer db
              if( peer_connection::connection_negotiation_status::negotiation_complete
                     == already_connected_peer->negotiation_status )
                 save_successful_address( this, *remote_endpoint );
          }
          // Now reject
          connection_rejected_message connection_rejected( _user_agent_string, core_protocol_version,
                                                           *remote_endpoint,
                                                           rejection_reason_code::already_connected,
                                                           "I'm already connected to you" );
          originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
          originating_peer->send_message( message(connection_rejected) );
          ilog("Received a hello_message from peer ${peer} that I'm already connected to (with id ${id}), rejection",
               ("peer", remote_endpoint)
               ("id", originating_peer->node_id));
          // If already connected, we disconnect
          disconnect_from_peer( originating_peer, connection_rejected.reason_string );
        }
#ifdef ENABLE_P2P_DEBUGGING_API
        else if(!_allowed_peers.empty() &&
                _allowed_peers.find(originating_peer->node_id) == _allowed_peers.end())
        {
          connection_rejected_message connection_rejected(_user_agent_string, core_protocol_version,
                                                          *remote_endpoint,
                                                          rejection_reason_code::blocked,
                                                          "you are not in my allowed_peers list");
          originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
          originating_peer->send_message( message(connection_rejected ) );
          dlog( "Received a hello_message from peer ${peer} who isn't in my allowed_peers list, rejection",
                ("peer", remote_endpoint ) );
        }
#endif // ENABLE_P2P_DEBUGGING_API
        else
        {
          // whether we're planning on accepting them as a peer or not, they seem to be a valid node,
          // so add them to our database if they're not firewalled
          if( peer_connection_direction::outbound == originating_peer->direction )
          {
             // For outbound connection, we already know the peer is not firewalled,
             // and it should be already in the peer database. Do nothing here.
          }
          else if( 0 == originating_peer->inbound_port )
          {
             ilog( "peer ${peer} did not give an inbound port so I'm treating them as if they are firewalled.",
                   ("peer", remote_endpoint) );
             originating_peer->is_firewalled = firewalled_state::firewalled;
          }
          else
          {
             // Note: no matter how we guess, we end up adding these to our peer database and trying to connect later.

             // First, we add the inbound endpoint that the peer told us it is listening on.
             fc::flat_set<fc::ip::endpoint> endpoints_to_save;
             endpoints_to_save.insert( fc::ip::endpoint( originating_peer->inbound_address,
                                                         originating_peer->inbound_port ) );

             // Second, we add the address and port we see.
             // It might be the same as above, but that's OK.
             endpoints_to_save.insert( *remote_endpoint );

             // Third, we add the address we see, with the inbound port the peer told us.
             // It might be the same as above, but that's OK.
             endpoints_to_save.insert( fc::ip::endpoint( remote_endpoint->get_address(),
                                                         originating_peer->inbound_port ) );

             ilog( "Saving potential endpoints to the peer database for peer ${peer}: ${endpoints}",
                   ("peer", remote_endpoint) ("endpoints", endpoints_to_save) );

             for( const auto& ep : endpoints_to_save )
             {
                // add to the peer database
                auto updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep( ep );
                updated_peer_record.last_seen_time = fc::time_point::now();
                _potential_peer_db.update_entry( updated_peer_record );
                // mark as a potential inbound address
                originating_peer->potential_inbound_endpoints[ep] = firewalled_state::unknown;
             }

             // Note: we don't update originating_peer->is_firewalled, because we might guess wrong

          }

          if (!is_accepting_new_connections())
          {
            connection_rejected_message connection_rejected(_user_agent_string, core_protocol_version,
                                                            *remote_endpoint,
                                                            rejection_reason_code::not_accepting_connections,
                                                            "not accepting any more incoming connections");
            originating_peer->their_state = peer_connection::their_connection_state::connection_rejected;
            originating_peer->send_message(message(connection_rejected));
            ilog("Received a hello_message from peer ${peer}, but I'm not accepting any more connections, rejection",
                 ("peer", remote_endpoint));
          }
          else
          {
            originating_peer->their_state = peer_connection::their_connection_state::connection_accepted;
            originating_peer->send_message(message(connection_accepted_message()));
            ilog("Received a hello_message from peer ${peer}, sending reply to accept connection",
                 ("peer", remote_endpoint));
          }
        }
   }

   void node_impl::on_connection_accepted_message( peer_connection* originating_peer,
                                                   const connection_accepted_message& ) const
   {
      VERIFY_CORRECT_THREAD();
      // Gatekeeping code
      // We only send one address request message shortly after connected
      if( originating_peer->our_state != peer_connection::our_connection_state::just_connected )
      {
         // Log and ignore
         wlog( "Received an unexpected connection_accepted message from ${peer}",
               ("peer", originating_peer->get_remote_endpoint()) );
         return;
      }

      ilog( "Received a connection_accepted in response to my \"hello\" from ${peer}",
            ("peer", originating_peer->get_remote_endpoint()) );
      originating_peer->negotiation_status = peer_connection::connection_negotiation_status::peer_connection_accepted;
      originating_peer->our_state = peer_connection::our_connection_state::connection_accepted;
      originating_peer->expecting_address_message = true;
      originating_peer->send_message(address_request_message());
   }

    void node_impl::on_connection_rejected_message(peer_connection* originating_peer, const connection_rejected_message& connection_rejected_message_received)
    {
      VERIFY_CORRECT_THREAD();
      if (originating_peer->our_state == peer_connection::our_connection_state::just_connected)
      {
        ilog("Received a rejection from ${peer} in response to my \"hello\", reason: \"${reason}\"",
             ("peer", originating_peer->get_remote_endpoint())
             ("reason", connection_rejected_message_received.reason_string));

        originating_peer->negotiation_status = peer_connection::connection_negotiation_status
                                                              ::peer_connection_rejected;
        originating_peer->our_state = peer_connection::our_connection_state::connection_rejected;

        if( connection_rejected_message_received.reason_code == rejection_reason_code::connected_to_self
            || connection_rejected_message_received.reason_code == rejection_reason_code::different_chain )
        {
          // Using remote_endpoint here for an outbound connection is OK.
          // For an inbound connection, we should have not saved anything to the peer database yet, nor we will
          //   save anything (it would be weird if they rejected us but we didn't reject them),
          //   so using remote_endpoint here at least won't do anything bad.
          //   Note: we should not erase or update data by the peer's claimed inbound_address,
          //         because the data is still unreliable.
          // Note: deleting is not the best approach since it can be readded soon and we will reconnect soon.
          //       Marking it "permanently rejected" is also not good enough since the peer can be "fixed".
          //       It seems the best approach is to reduce its weight significantly.
          greatly_delay_next_conn_to( this, *originating_peer->get_remote_endpoint() );
          // Note: we do not send closing_connection_message, but close directly. This is probably OK
          move_peer_to_closing_list(originating_peer->shared_from_this());
          originating_peer->close_connection();
        }
        // Note: ideally, if it is an outbound connection, and the rejection reason is "already_connected",
        //         we should update the existing connection's inbound_endpoint and mark it as verified.
        //       However, at the moment maybe we haven't processed its hello message,
        //         so don't know its node_id and unable to locate the existing connection.
        //       So it is better to do the update in on_hello_message().
        //       It is also possible that its hello message comes too late and the connection is already closed,
        //         in which case we don't have a chance to update anyway.
        else
        {
          // update our database to record that we were rejected so we won't try to connect again for a while
          // this only happens on connections we originate, so we should already know that peer is not firewalled
          fc::optional<potential_peer_record> updated_peer_record
                = _potential_peer_db.lookup_entry_for_endpoint(originating_peer->get_socket().remote_endpoint());
          if (updated_peer_record)
          {
            updated_peer_record->last_connection_disposition = last_connection_rejected;
            updated_peer_record->last_connection_attempt_time = fc::time_point::now();
            // Note: we do not increase number_of_failed_connection_attempts here, this is probably OK
            _potential_peer_db.update_entry(*updated_peer_record);
          }
          originating_peer->expecting_address_message = true;
          originating_peer->send_message(address_request_message());
        }
      }
      else
      {
        // Note: in older versions, FC_THROW() was called here,
        //       which would cause on_connection_closed() to be called,
        //       which would then close the connection when the peer_connection object was destroyed.
        //       Explicitly closing the connection here is more intuitive.
        wlog( "Unexpected connection_rejected_message from peer ${peer}, disconnecting",
              ("peer", originating_peer->get_remote_endpoint()) );
        disconnect_from_peer( originating_peer, "Received an unexpected connection_rejected_message" );
      }
    }

    void node_impl::on_address_request_message(peer_connection* originating_peer, const address_request_message&)
    {
      VERIFY_CORRECT_THREAD();
      // Gatekeeping code
      if( originating_peer->their_state != peer_connection::their_connection_state::connection_accepted
          && originating_peer->their_state != peer_connection::their_connection_state::connection_rejected )
      {
         wlog( "Unexpected address_request_message from peer ${peer}, disconnecting",
               ("peer", originating_peer->get_remote_endpoint()) );
         disconnect_from_peer( originating_peer, "Received an unexpected address_request_message" );
         return;
      }

      dlog( "Received an address request message from peer ${peer}",
            ("peer", originating_peer->get_remote_endpoint()) );

      address_message reply;
      if (_address_builder != nullptr )
         _address_builder->build( this, reply );
      originating_peer->send_message(reply);

      // If we rejected their connection, disconnect now
      if( originating_peer->their_state == peer_connection::their_connection_state::connection_rejected )
      {
         disconnect_from_peer( originating_peer,
                               "I rejected your connection request (hello message) so I'm disconnecting" );
      }
    }

   void node_impl::set_advertise_algorithm( const std::string& algo,
         const std::vector<std::string>& advertise_or_exclude_list )
   {
      VERIFY_CORRECT_THREAD();
      if (algo == "exclude_list")
      {
         _address_builder = std::make_shared<exclude_address_builder>(advertise_or_exclude_list);
      }
      else if (algo == "list")
      {
         _address_builder = std::make_shared<list_address_builder>(advertise_or_exclude_list);
      }
      else if (algo == "nothing")
      {
         _address_builder = nullptr;
      }
      else
         _address_builder = std::make_shared<all_address_builder>();
   }

   void node_impl::on_address_message( peer_connection* originating_peer,
                                       const address_message& address_message_received )
   {
      VERIFY_CORRECT_THREAD();
      // Do some gatekeeping here.
      // Malious peers can easily bypass our checks in on_hello_message(), and we will then request addresses anyway,
      //   so checking connection_state here is useless.
      // The size can be large, so we only handle the first N addresses.
      // The peer might send us lots of address messages even if we didn't request,
      //   so we'd better know whether we have sent an address request message recently.
      if( !originating_peer->expecting_address_message )
      {
         // Log and ignore
         wlog( "Received an unexpected address message containing ${size} addresses for peer ${peer}",
               ("size", address_message_received.addresses.size())
               ("peer", originating_peer->get_remote_endpoint()) );
         return;
      }
      originating_peer->expecting_address_message = false;

      dlog( "Received an address message containing ${size} addresses for peer ${peer}",
            ("size", address_message_received.addresses.size())
            ("peer", originating_peer->get_remote_endpoint()) );
      if( _node_configuration.connect_to_new_peers )
      {
         size_t count = 0;
         for (const address_info& address : address_message_received.addresses)
         {
            dlog( "    ${endpoint} last seen ${time}, firewalled status ${fw}",
                  ("endpoint", address.remote_endpoint)("time", address.last_seen_time)
                  ("fw", address.firewalled) );
            ++count;
            if( count >= _max_addrs_to_handle_at_once )
               break;
         }
         std::vector<graphene::net::address_info> updated_addresses;
         updated_addresses.reserve( count );
         auto now = fc::time_point_sec(fc::time_point::now());
         count = 0;
         for( const address_info& address : address_message_received.addresses )
         {
            if( 0 == address.remote_endpoint.port() )
               continue;
            updated_addresses.emplace_back( address.remote_endpoint,
                                            now,
                                            address.latency,
                                            address.node_id,
                                            address.direction,
                                            address.firewalled );
            ++count;
            if( count >= _max_addrs_to_handle_at_once )
               break;
         }
         if( merge_address_info_with_potential_peer_database(updated_addresses) )
            trigger_p2p_network_connect_loop();
      }

      if (_handshaking_connections.find(originating_peer->shared_from_this()) != _handshaking_connections.end())
      {
        // if we were handshaking, we need to continue with the next step in handshaking (which is either
        // ending handshaking and starting synchronization or disconnecting)
        if( originating_peer->our_state == peer_connection::our_connection_state::connection_rejected)
          disconnect_from_peer(originating_peer, "You rejected my connection request (hello message) so I'm disconnecting");
        else if (originating_peer->their_state == peer_connection::their_connection_state::connection_rejected)
          disconnect_from_peer(originating_peer, "I rejected your connection request (hello message) so I'm disconnecting");
        else
        {
          // Note: updating last_connection_disposition to last_connection_succeeded for inbound connections
          //       doesn't seem correct
          if( peer_connection_direction::outbound == originating_peer->direction )
             save_successful_address( this, *originating_peer->get_remote_endpoint() );

          // transition it to our active list
          originating_peer->negotiation_status = peer_connection::connection_negotiation_status::negotiation_complete;
          move_peer_to_active_list(originating_peer->shared_from_this());
          new_peer_just_added(originating_peer->shared_from_this());
        }
      }
      // else if this was an active connection, then this was just a reply to our periodic address requests.
      // we've processed it, there's nothing else to do
      // Note: we could reinitialize inbound endpoint verification here, but it doesn't seem necessary
   }

    void node_impl::on_fetch_blockchain_item_ids_message(peer_connection* originating_peer,
                                                         const fetch_blockchain_item_ids_message& fetch_blockchain_item_ids_message_received)
    {
      VERIFY_CORRECT_THREAD();
      // Gatekeeping code
      if( originating_peer->their_state != peer_connection::their_connection_state::connection_accepted )
      {
         wlog( "Unexpected fetch_blockchain_item_ids_message from peer ${peer}, disconnecting",
               ("peer", originating_peer->get_remote_endpoint()) );
         disconnect_from_peer( originating_peer, "Received an unexpected fetch_blockchain_item_ids_message" );
         return;
      }

      item_id peers_last_item_seen = item_id(fetch_blockchain_item_ids_message_received.item_type, item_hash_t());
      if (fetch_blockchain_item_ids_message_received.blockchain_synopsis.empty())
      {
        dlog("sync: received a request for item ids starting at the beginning of the chain "
             "from peer ${peer_endpoint} (full request: ${synopsis})",
             ("peer_endpoint", originating_peer->get_remote_endpoint())
             ("synopsis", fetch_blockchain_item_ids_message_received.blockchain_synopsis));
      }
      else
      {
        item_hash_t peers_last_item_hash_seen = fetch_blockchain_item_ids_message_received.blockchain_synopsis.back();
        dlog("sync: received a request for item ids after ${last_item_seen} from peer ${peer_endpoint} (full request: ${synopsis})",
             ("last_item_seen", peers_last_item_hash_seen)
             ("peer_endpoint", originating_peer->get_remote_endpoint())
             ("synopsis", fetch_blockchain_item_ids_message_received.blockchain_synopsis));
        peers_last_item_seen.item_hash = peers_last_item_hash_seen;
      }

      blockchain_item_ids_inventory_message reply_message;
      reply_message.item_type = fetch_blockchain_item_ids_message_received.item_type;
      reply_message.total_remaining_item_count = 0;
      try
      {
        reply_message.item_hashes_available
              = _delegate->get_block_ids(fetch_blockchain_item_ids_message_received.blockchain_synopsis,
                                         reply_message.total_remaining_item_count);
      }
      catch (const peer_is_on_an_unreachable_fork&)
      {
        dlog("Peer is on a fork and there's no set of blocks we can provide to switch them to our fork");
        // we reply with an empty list as if we had an empty blockchain;
        // we don't want to disconnect because they may be able to provide
        // us with blocks on their chain
      }

      bool disconnect_from_inhibited_peer = false;
      // if our client doesn't have any items after the item the peer requested, it will send back
      // a list containing the last item the peer requested
      //idump((reply_message)(fetch_blockchain_item_ids_message_received.blockchain_synopsis)); // for debug
      if( reply_message.item_hashes_available.empty() )
        originating_peer->peer_needs_sync_items_from_us = false; /* I have no items in my blockchain */
      else if( !fetch_blockchain_item_ids_message_received.blockchain_synopsis.empty() &&
               reply_message.item_hashes_available.size() == 1 &&
               std::find(fetch_blockchain_item_ids_message_received.blockchain_synopsis.begin(),
                         fetch_blockchain_item_ids_message_received.blockchain_synopsis.end(),
                         reply_message.item_hashes_available.back() ) != fetch_blockchain_item_ids_message_received.blockchain_synopsis.end() )
      {
        /* the last item in the peer's list matches the last item in our list */
        originating_peer->peer_needs_sync_items_from_us = false;
        if (originating_peer->inhibit_fetching_sync_blocks)
          disconnect_from_inhibited_peer = true; // delay disconnecting until after we send our reply to this fetch_blockchain_item_ids_message
      }
      else
        originating_peer->peer_needs_sync_items_from_us = true;

      if (!originating_peer->peer_needs_sync_items_from_us)
      {
        dlog("sync: peer is already in sync with us");
        // if we thought we had all the items this peer had, but now it turns out that we don't
        // have the last item it requested to send from,
        // we need to kick off another round of synchronization
        if (!originating_peer->we_need_sync_items_from_peer &&
            !fetch_blockchain_item_ids_message_received.blockchain_synopsis.empty() &&
            !_delegate->has_item(peers_last_item_seen))
        {
          dlog("sync: restarting sync with peer ${peer}", ("peer", originating_peer->get_remote_endpoint()));
          start_synchronizing_with_peer(originating_peer->shared_from_this());
        }
      }
      else
      {
        dlog("sync: peer is out of sync, sending peer ${count} items ids: first: ${first_item_id}, last: ${last_item_id}",
             ("count", reply_message.item_hashes_available.size())
             ("first_item_id", reply_message.item_hashes_available.front())
             ("last_item_id", reply_message.item_hashes_available.back()));
        if (!originating_peer->we_need_sync_items_from_peer &&
            !fetch_blockchain_item_ids_message_received.blockchain_synopsis.empty() &&
            !_delegate->has_item(peers_last_item_seen))
        {
          dlog("sync: restarting sync with peer ${peer}", ("peer", originating_peer->get_remote_endpoint()));
          start_synchronizing_with_peer(originating_peer->shared_from_this());
        }
      }
      originating_peer->send_message(reply_message);

      if (disconnect_from_inhibited_peer)
        {
        // the peer has all of our blocks, and we don't want any of theirs, so disconnect them
        disconnect_from_peer(originating_peer, "you are on a fork that I'm unable to switch to");
        return;
        }

      // Why only for inbound connections?
      if (originating_peer->direction == peer_connection_direction::inbound &&
          _handshaking_connections.find(originating_peer->shared_from_this()) != _handshaking_connections.end())
      {
        // handshaking is done, move the connection to fully active status and start synchronizing
        dlog("peer ${endpoint} which was handshaking with us has started synchronizing with us, "
             "start syncing with it",
             ("endpoint", originating_peer->get_remote_endpoint()));

        // Note: there was some code here to update the peer database, similar to the code in on_address_message(),
        //       but this is an inbound connection,
        //       updating last_connection_disposition to last_connection_succeeded doesn't seem correct,
        //       so the code was removed.

        // transition it to our active list
        originating_peer->negotiation_status = peer_connection::connection_negotiation_status::negotiation_complete;
        move_peer_to_active_list(originating_peer->shared_from_this());
        new_peer_just_added(originating_peer->shared_from_this());
      }
    }

    uint32_t node_impl::calculate_unsynced_block_count_from_all_peers()
    {
      VERIFY_CORRECT_THREAD();
      uint32_t max_number_of_unfetched_items = 0;
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for( const peer_connection_ptr& peer : _active_connections )
      {
        uint32_t this_peer_unfetched_items_count = (uint32_t)peer->ids_of_items_to_get.size()
                                                 + peer->number_of_unfetched_item_ids;
        max_number_of_unfetched_items = std::max(max_number_of_unfetched_items,
                                                 this_peer_unfetched_items_count);
      }
      return max_number_of_unfetched_items;
    }

    // get a blockchain synopsis that makes sense to send to the given peer.
    // If the peer isn't yet syncing with us, this is just a synopsis of our active blockchain
    // If the peer is syncing with us, it is a synopsis of our active blockchain plus the
    //    blocks the peer has already told us it has
    std::vector<item_hash_t> node_impl::create_blockchain_synopsis_for_peer( const peer_connection* peer )
    {
      VERIFY_CORRECT_THREAD();
      item_hash_t reference_point = peer->last_block_delegate_has_seen;

      // when we call _delegate->get_blockchain_synopsis(), we may yield and there's a
      // chance this peer's state will change before we get control back.  Save off
      // the stuff necessary for generating the synopsis.
      // This is pretty expensive, we should find a better way to do this
      std::vector<item_hash_t> original_ids_of_items_to_get(peer->ids_of_items_to_get.begin(),
                                                            peer->ids_of_items_to_get.end());
      uint32_t number_of_blocks_after_reference_point = original_ids_of_items_to_get.size();

      std::vector<item_hash_t> synopsis = _delegate->get_blockchain_synopsis(reference_point,
                                                number_of_blocks_after_reference_point);

#if 0
      // just for debugging, enable this and set a breakpoint to step through
      if (synopsis.empty())
        synopsis = _delegate->get_blockchain_synopsis(reference_point, number_of_blocks_after_reference_point);

      // TODO: it's possible that the returned synopsis is empty if the blockchain is empty (that's fine)
      // or if the reference point is now past our undo history (that's not).
      // in the second case, we should mark this peer as one we're unable to sync with and
      // disconnect them.
      if (reference_point != item_hash_t() && synopsis.empty())
        FC_THROW_EXCEPTION(block_older_than_undo_history, "You are on a fork I'm unable to switch to");
#endif

      if( number_of_blocks_after_reference_point )
      {
        // then the synopsis is incomplete, add the missing elements from ids_of_items_to_get
        uint32_t first_block_num_in_ids_to_get = _delegate->get_block_number(original_ids_of_items_to_get.front());
        uint32_t true_high_block_num = first_block_num_in_ids_to_get + original_ids_of_items_to_get.size() - 1;

        // in order to generate a seamless synopsis, we need to be using the same low_block_num as the
        // backend code; the first block in the synopsis will be the low block number it used
        uint32_t low_block_num = synopsis.empty() ? 1 : _delegate->get_block_number(synopsis.front());

        do
        {
          if( low_block_num >= first_block_num_in_ids_to_get )
            synopsis.push_back(original_ids_of_items_to_get[low_block_num - first_block_num_in_ids_to_get]);
          low_block_num += (true_high_block_num - low_block_num + 2 ) / 2;
        }
        while ( low_block_num <= true_high_block_num );
        assert(synopsis.back() == original_ids_of_items_to_get.back());
      }
      return synopsis;
    }

    void node_impl::fetch_next_batch_of_item_ids_from_peer( peer_connection* peer, bool reset_fork_tracking_data_for_peer /* = false */ )
    {
      VERIFY_CORRECT_THREAD();
      if( reset_fork_tracking_data_for_peer )
      {
        peer->last_block_delegate_has_seen = item_hash_t();
        peer->last_block_time_delegate_has_seen = _delegate->get_block_time(item_hash_t());
      }

      fc::oexception synopsis_exception;
      try
      {
        std::vector<item_hash_t> blockchain_synopsis = create_blockchain_synopsis_for_peer( peer );

        item_hash_t last_item_seen = blockchain_synopsis.empty() ? item_hash_t() : blockchain_synopsis.back();
        dlog( "sync: sending a request for the next items after ${last_item_seen} to peer ${peer}, "
              "(full request is ${blockchain_synopsis})",
             ( "last_item_seen", last_item_seen )
             ( "peer", peer->get_remote_endpoint() )
             ( "blockchain_synopsis", blockchain_synopsis ) );
        peer->item_ids_requested_from_peer = boost::make_tuple( blockchain_synopsis, fc::time_point::now() );
        peer->send_message( fetch_blockchain_item_ids_message(_sync_item_type, blockchain_synopsis ) );
      }
      catch (const block_older_than_undo_history& e)
      {
        synopsis_exception = e;
      }
      if (synopsis_exception)
        disconnect_from_peer(peer, "You are on a fork I'm unable to switch to");
    }

    void node_impl::on_blockchain_item_ids_inventory_message(peer_connection* originating_peer,
                                                             const blockchain_item_ids_inventory_message& blockchain_item_ids_inventory_message_received )
    {
      VERIFY_CORRECT_THREAD();
      // ignore unless we asked for the data
      if( originating_peer->item_ids_requested_from_peer )
      {
        // verify that the peer's the block ids the peer sent is a valid response to our request;
        // It should either be an empty list of blocks, or a list of blocks that builds off of one of
        // the blocks in the synopsis we sent
        if (!blockchain_item_ids_inventory_message_received.item_hashes_available.empty())
        {
          // what's more, it should be a sequential list of blocks, verify that first
          uint32_t first_block_number_in_reponse = _delegate->get_block_number(
                         blockchain_item_ids_inventory_message_received.item_hashes_available.front());
          // explicitly convert the size into 32 bit, should be OK
          auto total_items = uint32_t( blockchain_item_ids_inventory_message_received.item_hashes_available.size() );
          for (uint32_t i = 1; i < total_items; ++i)
          {
            uint32_t actual_num = _delegate->get_block_number(
                                        blockchain_item_ids_inventory_message_received.item_hashes_available[i]);
            uint32_t expected_num = first_block_number_in_reponse + i;
            if (actual_num != expected_num)
            {
              wlog("Invalid response from peer ${peer_endpoint}.  The list of blocks they provided is not sequential, "
                 "the ${position}th block in their reply was block number ${actual_num}, "
                 "but it should have been number ${expected_num}",
                 ("peer_endpoint", originating_peer->get_remote_endpoint())
                 ("position", i)
                 ("actual_num", actual_num)
                 ("expected_num", expected_num));
              fc::exception error_for_peer(FC_LOG_MESSAGE(error,
                                                        "You gave an invalid response to my request for sync blocks.  The list of blocks you provided is not sequential, "
                                                        "the ${position}th block in their reply was block number ${actual_num}, "
                                                        "but it should have been number ${expected_num}",
                                                        ("position", i)
                                                        ("actual_num", actual_num)
                                                        ("expected_num", expected_num)));
              disconnect_from_peer(originating_peer,
                                 "You gave an invalid response to my request for sync blocks",
                                 true, error_for_peer);
              return;
            }
          }

          const std::vector<item_hash_t>& synopsis_sent_in_request = originating_peer->item_ids_requested_from_peer->get<0>();
          const item_hash_t& first_item_hash = blockchain_item_ids_inventory_message_received.item_hashes_available.front();

          if (synopsis_sent_in_request.empty())
          {
            // if we sent an empty synopsis, we were asking for all blocks, so the first block should be block 1
            if (_delegate->get_block_number(first_item_hash) != 1)
            {
              wlog("Invalid response from peer ${peer_endpoint}.  We requested a list of sync blocks starting from the beginning of the chain, "
                   "but they provided a list of blocks starting with ${first_block}",
                   ("peer_endpoint", originating_peer->get_remote_endpoint())
                   ("first_block", first_item_hash));
              fc::exception error_for_peer(FC_LOG_MESSAGE(error, "You gave an invalid response for my request for sync blocks.  I asked for blocks starting from the beginning of the chain, "
                                                          "but you returned a list of blocks starting with ${first_block}",  
                                                          ("first_block", first_item_hash)));
              disconnect_from_peer(originating_peer,
                                   "You gave an invalid response to my request for sync blocks",
                                   true, error_for_peer);
              return;
            }
          }
          else // synopsis was not empty, we expect a response building off one of the blocks we sent
          {
            if (boost::range::find(synopsis_sent_in_request, first_item_hash) == synopsis_sent_in_request.end())
            {
              wlog("Invalid response from peer ${peer_endpoint}.  We requested a list of sync blocks based on the synopsis ${synopsis}, but they "
                   "provided a list of blocks starting with ${first_block}",
                   ("peer_endpoint", originating_peer->get_remote_endpoint())
                   ("synopsis", synopsis_sent_in_request)
                   ("first_block", first_item_hash));
              fc::exception error_for_peer(FC_LOG_MESSAGE(error, "You gave an invalid response for my request for sync blocks.  I asked for blocks following something in "
                                                          "${synopsis}, but you returned a list of blocks starting with ${first_block} which wasn't one of your choices",  
                                                          ("synopsis", synopsis_sent_in_request)
                                                          ("first_block", first_item_hash)));
              disconnect_from_peer(originating_peer,
                                   "You gave an invalid response to my request for sync blocks",
                                   true, error_for_peer);
              return;
            }
          }
        }
        originating_peer->item_ids_requested_from_peer.reset();

        // if exceptions are throw after clearing the item_ids_requested_from_peer (above),
        // it could leave our sync in a stalled state.  Wrap a try/catch around the rest
        // of the function so we can log if this ever happens.
        try
        {
          dlog( "sync: received a list of ${count} available items from ${peer_endpoint}",
               ( "count", blockchain_item_ids_inventory_message_received.item_hashes_available.size() )
               ( "peer_endpoint", originating_peer->get_remote_endpoint() ) );
          //for( const item_hash_t& item_hash : blockchain_item_ids_inventory_message_received.item_hashes_available )
          //{
          //  dlog( "sync:     ${hash}", ("hash", item_hash ) );
          //}

          // if the peer doesn't have any items after the one we asked for
          if( blockchain_item_ids_inventory_message_received.total_remaining_item_count == 0 &&
              ( blockchain_item_ids_inventory_message_received.item_hashes_available.empty() || // there are no items in the peer's blockchain.  this should only happen if our blockchain was empty when we requested, might want to verify that.
               ( blockchain_item_ids_inventory_message_received.item_hashes_available.size() == 1 &&
                _delegate->has_item( item_id(blockchain_item_ids_inventory_message_received.item_type,
                                            blockchain_item_ids_inventory_message_received.item_hashes_available.front() ) ) ) ) && // we've already seen the last item in the peer's blockchain
              originating_peer->ids_of_items_to_get.empty() &&
              originating_peer->number_of_unfetched_item_ids == 0 ) // <-- is the last check necessary?
          {
            dlog( "sync: peer said we're up-to-date, entering normal operation with this peer" );
            originating_peer->we_need_sync_items_from_peer = false;

            uint32_t new_number_of_unfetched_items = calculate_unsynced_block_count_from_all_peers();
            _total_num_of_unfetched_items = new_number_of_unfetched_items;
            if( new_number_of_unfetched_items == 0 )
              _delegate->sync_status( blockchain_item_ids_inventory_message_received.item_type, 0 );

            return;
          }

          std::deque<item_hash_t> item_hashes_received( blockchain_item_ids_inventory_message_received.item_hashes_available.begin(),
                                                       blockchain_item_ids_inventory_message_received.item_hashes_available.end() );
          originating_peer->number_of_unfetched_item_ids = blockchain_item_ids_inventory_message_received.total_remaining_item_count;
          // flush any items this peer sent us that we've already received and processed from another peer
          if (!item_hashes_received.empty() &&
              originating_peer->ids_of_items_to_get.empty())
          {
            bool is_first_item_for_other_peer = false;
            {
               fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
               for (const peer_connection_ptr& peer : _active_connections)
               {
                  if (peer != originating_peer->shared_from_this() &&
                        !peer->ids_of_items_to_get.empty() &&
                        peer->ids_of_items_to_get.front() == blockchain_item_ids_inventory_message_received.item_hashes_available.front())
                  {
                     dlog("The item ${newitem} is the first item for peer ${peer}",
                           ("newitem", blockchain_item_ids_inventory_message_received.item_hashes_available.front())
                           ("peer", peer->get_remote_endpoint()));
                     is_first_item_for_other_peer = true;
                     break;
                  }
               }
            }
            dlog("is_first_item_for_other_peer: ${is_first}.  item_hashes_received.size() = ${size}",
                 ("is_first", is_first_item_for_other_peer)("size", item_hashes_received.size()));
            if (!is_first_item_for_other_peer)
            {
              while (!item_hashes_received.empty() &&
                     _delegate->has_item(item_id(blockchain_item_ids_inventory_message_received.item_type,
                                                 item_hashes_received.front())))
              {
                assert(item_hashes_received.front() != item_hash_t());
                originating_peer->last_block_delegate_has_seen = item_hashes_received.front();
                originating_peer->last_block_time_delegate_has_seen = _delegate->get_block_time(item_hashes_received.front());
                dlog("popping item because delegate has already seen it.  peer ${peer}'s last block the delegate has seen is now ${block_id} (actual block #${actual_block_num})",
                     ("peer", originating_peer->get_remote_endpoint())
                     ("block_id", originating_peer->last_block_delegate_has_seen)
                     ("actual_block_num", _delegate->get_block_number(item_hashes_received.front())));

                item_hashes_received.pop_front();
              }
              dlog("after removing all items we have already seen, item_hashes_received.size() = ${size}", ("size", item_hashes_received.size()));
            }
          }
          else if (!item_hashes_received.empty())
          {
            // we received a list of items and we already have a list of items to fetch from this peer.
            // In the normal case, this list will immediately follow the existing list, meaning the
            // last hash of our existing list will match the first hash of the new list.

            // In the much less likely case, we've received a partial list of items from the peer, then
            // the peer switched forks before sending us the remaining list.  In this case, the first
            // hash in the new list may not be the last hash in the existing list (it may be earlier, or
            // it may not exist at all.

            // In either case, pop items off the back of our existing list until we find our first
            // item, then append our list.
            while (!originating_peer->ids_of_items_to_get.empty())
            {
              if (item_hashes_received.front() != originating_peer->ids_of_items_to_get.back())
                originating_peer->ids_of_items_to_get.pop_back();
              else
                break;
            }
            if (originating_peer->ids_of_items_to_get.empty())
            {
              // this happens when the peer has switched forks between the last inventory message and
              // this one, and there weren't any unfetched items in common
              // We don't know where in the blockchain the new front() actually falls, all we can
              // expect is that it is a block that we knew about because it should be one of the
              // blocks we sent in the initial synopsis.
              assert(_delegate->has_item(item_id(_sync_item_type, item_hashes_received.front())));
              originating_peer->last_block_delegate_has_seen = item_hashes_received.front();
              originating_peer->last_block_time_delegate_has_seen = _delegate->get_block_time(item_hashes_received.front());
              item_hashes_received.pop_front();
            }
            else
            {
              // the common simple case: the new list extends the old.  pop off the duplicate element
              originating_peer->ids_of_items_to_get.pop_back();
            }
          }

          if (!item_hashes_received.empty() && !originating_peer->ids_of_items_to_get.empty())
            assert(item_hashes_received.front() != originating_peer->ids_of_items_to_get.back());

          // at any given time, there's a maximum number of blocks that can possibly be out there
          // [(now - genesis time) / block interval].  If they offer us more blocks than that,
          // they must be an attacker or have a buggy client.
          fc::time_point_sec minimum_time_of_last_offered_block =
              originating_peer->last_block_time_delegate_has_seen + // timestamp of the block immediately before the first unfetched block
              originating_peer->number_of_unfetched_item_ids * GRAPHENE_MIN_BLOCK_INTERVAL;
          fc::time_point_sec now = fc::time_point::now();
          if (minimum_time_of_last_offered_block > (now + GRAPHENE_NET_FUTURE_SYNC_BLOCKS_GRACE_PERIOD_SEC))
          {
            wlog("Disconnecting from peer ${peer} who offered us an implausible number of blocks, their last block would be in the future (${timestamp})",
                 ("peer", originating_peer->get_remote_endpoint())
                 ("timestamp", minimum_time_of_last_offered_block));
            fc::exception error_for_peer(FC_LOG_MESSAGE(error, "You offered me a list of more sync blocks than could possibly exist.  Total blocks offered: ${blocks}, Minimum time of the last block you offered: ${minimum_time_of_last_offered_block}, Now: ${now}",
                                                        ("blocks", originating_peer->number_of_unfetched_item_ids)
                                                        ("minimum_time_of_last_offered_block", minimum_time_of_last_offered_block)
                                                        ("now", now)));
            disconnect_from_peer(originating_peer,
                                 "You offered me a list of more sync blocks than could possibly exist",
                                 true, error_for_peer);
            return;
          }

          // append the remaining items to the peer's list
          boost::push_back(originating_peer->ids_of_items_to_get, item_hashes_received);

          uint32_t new_number_of_unfetched_items = calculate_unsynced_block_count_from_all_peers();
          if (new_number_of_unfetched_items != _total_num_of_unfetched_items)
            _delegate->sync_status(blockchain_item_ids_inventory_message_received.item_type,
                                   new_number_of_unfetched_items);
          _total_num_of_unfetched_items = new_number_of_unfetched_items;

          if (blockchain_item_ids_inventory_message_received.total_remaining_item_count != 0)
          {
            // the peer hasn't sent us all the items it knows about.
            if (originating_peer->ids_of_items_to_get.size() > GRAPHENE_NET_MIN_BLOCK_IDS_TO_PREFETCH)
            {
              // we have a good number of item ids from this peer, start fetching blocks from it;
              // we'll switch back later to finish the job.
              trigger_fetch_sync_items_loop();
            }
            else
            {
              // keep fetching the peer's list of sync items until we get enough to switch into block-
              // fetchimg mode
              fetch_next_batch_of_item_ids_from_peer(originating_peer);
            }
          }
          else
          {
            // the peer has told us about all of the items it knows
            if (!originating_peer->ids_of_items_to_get.empty())
            {
              // we now know about all of the items the peer knows about, and there are some items on the list
              // that we should try to fetch.  Kick off the fetch loop.
              trigger_fetch_sync_items_loop();
            }
            else
            {
              // If we get here, the peer has sent us a non-empty list of items, but we have already
              // received all of the items from other peers.  Send a new request to the peer to
              // see if we're really in sync
              fetch_next_batch_of_item_ids_from_peer(originating_peer);
            }
          }
        }
        catch (const fc::canceled_exception&)
        {
          throw;
        }
        catch (const fc::exception& e)
        {
          elog("Caught unexpected exception: ${e}", ("e", e));
          assert(false && "exceptions not expected here");
        }
        catch (const std::exception& e)
        {
          elog("Caught unexpected exception: ${e}", ("e", e.what()));
          assert(false && "exceptions not expected here");
        }
        catch (...)
        {
          elog("Caught unexpected exception, could break sync operation");
        }
      }
      else
      {
        wlog( "sync: received a list of sync items available from peer ${peer}, but I didn't ask for any!",
              ("peer", originating_peer->get_remote_endpoint()) );
      }
    }

    graphene::net::message node_impl::get_message_for_item(const item_id& item)
    {
      try
      {
        return _message_cache.get_message(item.item_hash);
      }
      catch (fc::key_not_found_exception&)
      {}
      try
      {
        return _delegate->get_item(item);
      }
      catch (fc::key_not_found_exception&)
      {}
      return item_not_available_message(item);
    }

    void node_impl::on_fetch_items_message(peer_connection* originating_peer,
                                           const fetch_items_message& fetch_items_message_received)
    {
      VERIFY_CORRECT_THREAD();
      // Gatekeeping code
      if( originating_peer->their_state != peer_connection::their_connection_state::connection_accepted )
      {
         wlog( "Unexpected fetch_items_message from peer ${peer}, disconnecting",
               ("peer", originating_peer->get_remote_endpoint()) );
         disconnect_from_peer( originating_peer, "Received an unexpected fetch_items_message" );
         return;
      }

      dlog("received items request for ids ${ids} of type ${type} from peer ${endpoint}",
           ("ids", fetch_items_message_received.items_to_fetch)
           ("type", fetch_items_message_received.item_type)
           ("endpoint", originating_peer->get_remote_endpoint()));

      fc::optional<message> last_block_message_sent;

      std::list<message> reply_messages;
      for (const item_hash_t& item_hash : fetch_items_message_received.items_to_fetch)
      {
        try
        {
          message requested_message = _message_cache.get_message(item_hash);
          dlog("received item request for item ${id} from peer ${endpoint}, returning the item from my message cache",
               ("endpoint", originating_peer->get_remote_endpoint())
               ("id", requested_message.id()));
          reply_messages.push_back(requested_message);
          if (fetch_items_message_received.item_type == block_message_type)
            last_block_message_sent = requested_message;
          continue;
        }
        catch (fc::key_not_found_exception&)
        {
           // it wasn't in our local cache, that's ok ask the client
        }

        item_id item_to_fetch(fetch_items_message_received.item_type, item_hash);
        try
        {
          message requested_message = _delegate->get_item(item_to_fetch);
          dlog("received item request from peer ${endpoint}, returning the item from delegate with id ${id} size ${size}",
               ("id", requested_message.id())
               ("size", requested_message.size)
               ("endpoint", originating_peer->get_remote_endpoint()));
          reply_messages.push_back(requested_message);
          if (fetch_items_message_received.item_type == block_message_type)
            last_block_message_sent = requested_message;
          continue;
        }
        catch (fc::key_not_found_exception&)
        {
          reply_messages.push_back(item_not_available_message(item_to_fetch));
          dlog("received item request from peer ${endpoint} but we don't have it",
               ("endpoint", originating_peer->get_remote_endpoint()));
        }
      }

      // if we sent them a block, update our record of the last block they've seen accordingly
      if (last_block_message_sent)
      {
        graphene::net::block_message block = last_block_message_sent->as<graphene::net::block_message>();
        originating_peer->last_block_delegate_has_seen = block.block_id;
        originating_peer->last_block_time_delegate_has_seen = _delegate->get_block_time(block.block_id);
      }

      for (const message& reply : reply_messages)
      {
        if (reply.msg_type.value() == block_message_type)
          originating_peer->send_item(item_id(block_message_type, reply.as<graphene::net::block_message>().block_id));
        else
          originating_peer->send_message(reply);
      }
    }

    void node_impl::on_item_not_available_message( peer_connection* originating_peer, const item_not_available_message& item_not_available_message_received )
    {
      VERIFY_CORRECT_THREAD();
      const item_id& requested_item = item_not_available_message_received.requested_item;
      auto regular_item_iter = originating_peer->items_requested_from_peer.find(requested_item);
      if (regular_item_iter != originating_peer->items_requested_from_peer.end())
      {
        originating_peer->items_requested_from_peer.erase( regular_item_iter );
        originating_peer->inventory_peer_advertised_to_us.erase( requested_item );
        if (is_item_in_any_peers_inventory(requested_item))
        {
          _items_to_fetch.insert(prioritized_item_id(requested_item, _items_to_fetch_seq_counter));
          ++_items_to_fetch_seq_counter;
        }
        wlog( "Peer ${peer} doesn't have the requested item ${item}.",
              ("peer", originating_peer->get_remote_endpoint())
              ("item", requested_item) );
        trigger_fetch_items_loop();
        return;
      }

      auto sync_item_iter = originating_peer->sync_items_requested_from_peer.find(requested_item.item_hash);
      if (sync_item_iter != originating_peer->sync_items_requested_from_peer.end())
      {
        _active_sync_requests.erase(*sync_item_iter);
        originating_peer->sync_items_requested_from_peer.erase(sync_item_iter);

        if (originating_peer->peer_needs_sync_items_from_us)
          originating_peer->inhibit_fetching_sync_blocks = true;
        else
          disconnect_from_peer(originating_peer, "You are missing a sync item you claim to have, your database is probably corrupted. Try --rebuild-index.",true,
                               fc::exception(FC_LOG_MESSAGE(error,"You are missing a sync item you claim to have, your database is probably corrupted. Try --rebuild-index.",
                               ("item_id", requested_item))));
        wlog( "Peer ${peer} doesn't have the requested sync item ${item}.  This really shouldn't happen",
              ("peer", originating_peer->get_remote_endpoint())
              ("item", requested_item) );
        trigger_fetch_sync_items_loop();
        return;
      }

      dlog("Peer doesn't have an item we're looking for, which is fine because we weren't looking for it");
    }

    void node_impl::on_item_ids_inventory_message(peer_connection* originating_peer, const item_ids_inventory_message& item_ids_inventory_message_received)
    {
      VERIFY_CORRECT_THREAD();
      // Gatekeeping code
      if( originating_peer->their_state != peer_connection::their_connection_state::connection_accepted )
      {
         wlog( "Unexpected item_ids_inventory_message from peer ${peer}, disconnecting",
               ("peer", originating_peer->get_remote_endpoint()) );
         disconnect_from_peer( originating_peer, "Received an unexpected item_ids_inventory_message" );
         return;
      }

      // expire old inventory
      // so we'll be making our decisions about whether to fetch blocks below based only on recent inventory
      originating_peer->clear_old_inventory();

      dlog( "received inventory of ${count} items from peer ${endpoint}",
            ("count", item_ids_inventory_message_received.item_hashes_available.size())
            ("endpoint", originating_peer->get_remote_endpoint() ) );
      for( const item_hash_t& item_hash : item_ids_inventory_message_received.item_hashes_available )
      {
        item_id advertised_item_id(item_ids_inventory_message_received.item_type, item_hash);
        bool we_advertised_this_item_to_a_peer = false;
        bool we_requested_this_item_from_a_peer = false;
        {
           fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
            for (const peer_connection_ptr& peer : _active_connections)
            {
               if (peer->inventory_advertised_to_peer.find(advertised_item_id) != peer->inventory_advertised_to_peer.end())
               {
                  we_advertised_this_item_to_a_peer = true;
                  break;
               }
               if (peer->items_requested_from_peer.find(advertised_item_id) != peer->items_requested_from_peer.end())
                  we_requested_this_item_from_a_peer = true;
            }
        }

        // if we have already advertised it to a peer, we must have it, no need to do anything else
        if (!we_advertised_this_item_to_a_peer)
        {
          // if the peer has flooded us with transactions, don't add these to the inventory to prevent our
          // inventory list from growing without bound.  We try to allow fetching blocks even when
          // we've stopped fetching transactions.
          if ((item_ids_inventory_message_received.item_type == graphene::net::trx_message_type &&
               originating_peer->is_inventory_advertised_to_us_list_full_for_transactions()) ||
              originating_peer->is_inventory_advertised_to_us_list_full())
            break;
          originating_peer->inventory_peer_advertised_to_us.insert(peer_connection::timestamped_item_id(advertised_item_id, fc::time_point::now()));
          if (!we_requested_this_item_from_a_peer)
          {
            if (_recently_failed_items.find(item_id(item_ids_inventory_message_received.item_type, item_hash)) != _recently_failed_items.end())
            {
              dlog("not adding ${item_hash} to our list of items to fetch because we've recently fetched a copy and it failed to push",
                   ("item_hash", item_hash));
            }
            else
            {
              auto items_to_fetch_iter = _items_to_fetch.get<item_id_index>().find(advertised_item_id);
              if (items_to_fetch_iter == _items_to_fetch.get<item_id_index>().end())
              {
                // it's new to us
                _items_to_fetch.insert(prioritized_item_id(advertised_item_id, _items_to_fetch_seq_counter));
                ++_items_to_fetch_seq_counter;
                dlog("adding item ${item_hash} from inventory message to our list of items to fetch",
                     ("item_hash", item_hash));
                trigger_fetch_items_loop();
              }
              else
              {
                // another peer has told us about this item already, but this peer just told us it has the item
                // too, we can expect it to be around in this peer's cache for longer, so update its timestamp
                _items_to_fetch.get<item_id_index>().modify(items_to_fetch_iter,
                                                            [](prioritized_item_id& item) { item.timestamp = fc::time_point::now(); });
              }
            }
          }
        }
      }

    }

    void node_impl::on_closing_connection_message( peer_connection* originating_peer,
          const closing_connection_message& closing_connection_message_received )
    {
      VERIFY_CORRECT_THREAD();
      originating_peer->they_have_requested_close = true;

      if( closing_connection_message_received.closing_due_to_error )
      {
        wlog( "Peer ${peer} is disconnecting us because of an error: ${msg}, exception: ${error}",
             ( "peer", originating_peer->get_remote_endpoint() )
             ( "msg", closing_connection_message_received.reason_for_closing )
             ( "error", closing_connection_message_received.error ) );
        std::ostringstream message;
        message << "Peer " << fc::variant( originating_peer->get_remote_endpoint(),
                                           GRAPHENE_NET_MAX_NESTED_OBJECTS ).as_string() <<
                  " disconnected us: " << closing_connection_message_received.reason_for_closing;
        fc::exception detailed_error(FC_LOG_MESSAGE(warn,
              "Peer ${peer} is disconnecting us because of an error: ${msg}, exception: ${error}",
              ( "peer", originating_peer->get_remote_endpoint() )
              ( "msg", closing_connection_message_received.reason_for_closing )
              ( "error", closing_connection_message_received.error ) ));
        _delegate->error_encountered( message.str(),
                                      detailed_error );
      }
      else
      {
        wlog( "Peer ${peer} is disconnecting us because: ${msg}",
             ( "peer", originating_peer->get_remote_endpoint() )
             ( "msg", closing_connection_message_received.reason_for_closing ) );
      }
      if( originating_peer->we_have_requested_close )
        originating_peer->close_connection();
    }

    void node_impl::on_connection_closed(peer_connection* originating_peer)
    {
      VERIFY_CORRECT_THREAD();
      peer_connection_ptr originating_peer_ptr = originating_peer->shared_from_this();
      _rate_limiter.remove_tcp_socket( &originating_peer->get_socket() );

      // if we closed the connection (due to timeout or handshake failure), we should have recorded an
      // error message to store in the peer database when we closed the connection
      fc::optional<fc::ip::endpoint> inbound_endpoint = originating_peer->get_endpoint_for_connecting();
      if( originating_peer->connection_closed_error
          && inbound_endpoint.valid() && inbound_endpoint->port() != 0 )
      {
        fc::optional<potential_peer_record> updated_peer_record
              = _potential_peer_db.lookup_entry_for_endpoint(*inbound_endpoint);
        if (updated_peer_record)
        {
          updated_peer_record->last_error = *originating_peer->connection_closed_error;
          _potential_peer_db.update_entry(*updated_peer_record);
        }
      }

      _closing_connections.erase(originating_peer_ptr);
      _handshaking_connections.erase(originating_peer_ptr);
      _terminating_connections.erase(originating_peer_ptr);
      if (_active_connections.find(originating_peer_ptr) != _active_connections.end())
      {
        _active_connections.erase(originating_peer_ptr);

        update_address_seen_time( this, originating_peer );
      }

      ilog("Remote peer ${endpoint} closed their connection to us",
           ("endpoint", originating_peer->get_remote_endpoint()));
      display_current_connections();
      trigger_p2p_network_connect_loop();

      // notify the node delegate so it can update the display
      if( _active_connections.size() != _last_reported_number_of_conns )
      {
        _last_reported_number_of_conns = (uint32_t)_active_connections.size();
        _delegate->connection_count_changed( _last_reported_number_of_conns );
      }

      // if we had requested any sync or regular items from this peer that we haven't
      // received yet, reschedule them to be fetched from another peer
      if (!originating_peer->sync_items_requested_from_peer.empty())
      {
        for (auto sync_item : originating_peer->sync_items_requested_from_peer)
          _active_sync_requests.erase(sync_item);
        trigger_fetch_sync_items_loop();
      }

      if (!originating_peer->items_requested_from_peer.empty())
      {
        for (auto item_and_time : originating_peer->items_requested_from_peer)
        {
          if (is_item_in_any_peers_inventory(item_and_time.first))
          {
            _items_to_fetch.insert(prioritized_item_id(item_and_time.first, _items_to_fetch_seq_counter));
            ++_items_to_fetch_seq_counter;
          }
        }
        trigger_fetch_items_loop();
      }

      schedule_peer_for_deletion(originating_peer_ptr);
    }

    void node_impl::send_sync_block_to_node_delegate(const graphene::net::block_message& block_message_to_send)
    {
      dlog("in send_sync_block_to_node_delegate()");
      bool client_accepted_block = false;
      bool discontinue_fetching_blocks_from_peer = false;

      fc::oexception handle_message_exception;

      try
      {
        std::vector<message_hash_type> contained_transaction_msg_ids;
        _delegate->handle_block(block_message_to_send, true, contained_transaction_msg_ids);
        dlog("Successfully pushed sync block ${num} (id:${id})",
             ("num", block_message_to_send.block.block_num())
             ("id", block_message_to_send.block_id));
        _most_recent_blocks_accepted.push_back(block_message_to_send.block_id);

        client_accepted_block = true;
      }
      catch (const block_older_than_undo_history& e)
      {
        wlog("Failed to push sync block ${num} (id:${id}): block is on a fork older than our undo history would "
             "allow us to switch to: ${e}",
             ("num", block_message_to_send.block.block_num())
             ("id", block_message_to_send.block_id)
             ("e", (fc::exception)e));
        handle_message_exception = e;
        discontinue_fetching_blocks_from_peer = true;
      }
      catch (const fc::canceled_exception&)
      {
        throw;
      }
      catch (const fc::exception& e)
      {
        auto block_num = block_message_to_send.block.block_num();
        wlog("Failed to push sync block ${num} (id:${id}): client rejected sync block sent by peer: ${e}",
             ("num", block_num)
             ("id", block_message_to_send.block_id)
             ("e", e));
        if( e.code() == block_timestamp_in_future_exception::code_enum::code_value )
        {
           handle_message_exception = block_timestamp_in_future_exception( FC_LOG_MESSAGE( warn, "",
                ("block_header", static_cast<graphene::protocol::block_header>(block_message_to_send.block))
                ("block_num", block_num)
                ("block_id", block_message_to_send.block_id) ) );
        }
        else
           handle_message_exception = e;
      }

      // build up lists for any potentially-blocking operations we need to do, then do them
      // at the end of this function
      std::set<peer_connection_ptr> peers_with_newly_empty_item_lists;
      std::set<peer_connection_ptr> peers_we_need_to_sync_to;
      std::map<peer_connection_ptr, std::pair<std::string, fc::oexception> > peers_to_disconnect; // map peer -> pair<reason_string, exception>

      if( client_accepted_block )
      {
         --_total_num_of_unfetched_items;
         dlog("sync: client accpted the block, we now have only ${count} items left to fetch before we're in sync",
               ("count", _total_num_of_unfetched_items));
         bool is_fork_block = is_hard_fork_block(block_message_to_send.block.block_num());
         {
            fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());

            for (const peer_connection_ptr& peer : _active_connections)
            {
               bool disconnecting_this_peer = false;
               if (is_fork_block)
               {
                  // we just pushed a hard fork block.  Find out if this peer is running a client
                  // that will be unable to process future blocks
                  if (peer->last_known_fork_block_number != 0)
                  {
                     uint32_t next_fork_block_number = get_next_known_hard_fork_block_number(peer->last_known_fork_block_number);
                     if (next_fork_block_number != 0 &&
                           next_fork_block_number <= block_message_to_send.block.block_num())
                     {
                        std::ostringstream disconnect_reason_stream;
                        disconnect_reason_stream << "You need to upgrade your client due to hard fork at block " << block_message_to_send.block.block_num();
                        peers_to_disconnect[peer] = std::make_pair(disconnect_reason_stream.str(),
                              fc::oexception(fc::exception(FC_LOG_MESSAGE(error, "You need to upgrade your client due to hard fork at block ${block_number}",
                              ("block_number", block_message_to_send.block.block_num())))));
#ifdef ENABLE_DEBUG_ULOGS
                        ulog("Disconnecting from peer during sync because their version is too old.  Their version date: ${date}", ("date", peer->graphene_git_revision_unix_timestamp));
#endif
                        disconnecting_this_peer = true;
                     }
                  }
               }
               if (!disconnecting_this_peer &&
                     peer->ids_of_items_to_get.empty() && peer->ids_of_items_being_processed.empty())
               {
                  dlog( "Cannot pop first element off peer ${peer}'s list, its list is empty", ("peer", peer->get_remote_endpoint() ) );
                  // we don't know for sure that this peer has the item we just received.
                  // If peer is still syncing to us, we know they will ask us for
                  // sync item ids at least one more time and we'll notify them about
                  // the item then, so there's no need to do anything.  If we still need items
                  // from them, we'll be asking them for more items at some point, and
                  // that will clue them in that they are out of sync.  If we're fully in sync
                  // we need to kick off another round of synchronization with them so they can
                  // find out about the new item.
                  if (!peer->peer_needs_sync_items_from_us && !peer->we_need_sync_items_from_peer)
                  {
                     dlog("We will be restarting synchronization with peer ${peer}", ("peer", peer->get_remote_endpoint()));
                     peers_we_need_to_sync_to.insert(peer);
                  }
               }
               else if (!disconnecting_this_peer)
               {
                  auto items_being_processed_iter = peer->ids_of_items_being_processed.find(block_message_to_send.block_id);
                  if (items_being_processed_iter != peer->ids_of_items_being_processed.end())
                  {
                     peer->last_block_delegate_has_seen = block_message_to_send.block_id;
                     peer->last_block_time_delegate_has_seen = block_message_to_send.block.timestamp;

                     peer->ids_of_items_being_processed.erase(items_being_processed_iter);
                     dlog("Removed item from ${endpoint}'s list of items being processed, still processing ${len} blocks",
                           ("endpoint", peer->get_remote_endpoint())("len", peer->ids_of_items_being_processed.size()));

                     // if we just received the last item in our list from this peer, we will want to
                     // send another request to find out if we are in sync, but we can't do this yet
                     // (we don't want to allow a fiber swap in the middle of popping items off the list)
                     if (peer->ids_of_items_to_get.empty() &&
                           peer->number_of_unfetched_item_ids == 0 &&
                           peer->ids_of_items_being_processed.empty())
                     peers_with_newly_empty_item_lists.insert(peer);

                     // in this case, we know the peer was offering us this exact item, no need to
                     // try to inform them of its existence
                  }
               }
            } // for
         } // lock_guard
      }
      else
      {
        // invalid message received
        fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
        for (const peer_connection_ptr& peer : _active_connections)
        {
          if (peer->ids_of_items_being_processed.find(block_message_to_send.block_id)
                 != peer->ids_of_items_being_processed.end())
          {
            if (discontinue_fetching_blocks_from_peer)
            {
              wlog("inhibiting fetching sync blocks from peer ${endpoint} because it is on a fork that's too old",
                   ("endpoint", peer->get_remote_endpoint()));
              peer->inhibit_fetching_sync_blocks = true;
            }
            else
              peers_to_disconnect[peer] = std::make_pair(
                    std::string("You offered us a block that we reject as invalid"),
                    fc::oexception(handle_message_exception));
          }
        }
      }

      for (auto& peer_to_disconnect : peers_to_disconnect)
      {
        const peer_connection_ptr& peer = peer_to_disconnect.first;
        std::string reason_string;
        fc::oexception reason_exception;
        std::tie(reason_string, reason_exception) = peer_to_disconnect.second;
        wlog("disconnecting client ${endpoint} because it offered us the rejected block",
             ("endpoint", peer->get_remote_endpoint()));
        disconnect_from_peer(peer.get(), reason_string, true, reason_exception);
      }
      for (const peer_connection_ptr& peer : peers_with_newly_empty_item_lists)
        fetch_next_batch_of_item_ids_from_peer(peer.get());

      for (const peer_connection_ptr& peer : peers_we_need_to_sync_to)
        start_synchronizing_with_peer(peer);

      dlog("Leaving send_sync_block_to_node_delegate");

      if (// _suspend_fetching_sync_blocks && <-- you can use this if
                                               // "max_blocks_to_handle_at_once" == "max_sync_blocks_to_prefetch"
          !_node_is_shutting_down &&
          (!_process_backlog_of_sync_blocks_done.valid() || _process_backlog_of_sync_blocks_done.ready()))
        _process_backlog_of_sync_blocks_done = fc::async([=](){ process_backlog_of_sync_blocks(); },
                                                         "process_backlog_of_sync_blocks");
    }

    void node_impl::process_backlog_of_sync_blocks()
    {
      VERIFY_CORRECT_THREAD();
      // garbage-collect the list of async tasks here for lack of a better place
      for (auto calls_iter = _handle_message_calls_in_progress.begin();
            calls_iter != _handle_message_calls_in_progress.end();)
      {
        if (calls_iter->ready())
          calls_iter = _handle_message_calls_in_progress.erase(calls_iter);
        else
          ++calls_iter;
      }

      dlog("in process_backlog_of_sync_blocks");
      if (_handle_message_calls_in_progress.size() >= _max_blocks_to_handle_at_once)
      {
        dlog("leaving process_backlog_of_sync_blocks because we're already processing too many blocks");
        return; // we will be rescheduled when the next block finishes its processing
      }
      dlog("currently ${count} blocks in the process of being handled", ("count", _handle_message_calls_in_progress.size()));


      if (_suspend_fetching_sync_blocks)
      {
        dlog("resuming processing sync block backlog because we only ${count} blocks in progress",
             ("count", _handle_message_calls_in_progress.size()));
        _suspend_fetching_sync_blocks = false;
      }


      // when syncing with multiple peers, it's possible that we'll have hundreds of blocks ready to push
      // to the client at once.  This can be slow, and we need to limit the number we push at any given
      // time to allow network traffic to continue so we don't end up disconnecting from peers
      //fc::time_point start_time = fc::time_point::now();
      //fc::time_point when_we_should_yield = start_time + fc::seconds(1);

      bool block_processed_this_iteration;
      size_t blocks_processed = 0;

      std::set<peer_connection_ptr> peers_with_newly_empty_item_lists;
      std::set<peer_connection_ptr> peers_we_need_to_sync_to;
      std::map<peer_connection_ptr, fc::oexception> peers_with_rejected_block;

      do
      {
        std::copy(std::make_move_iterator(_new_received_sync_items.begin()),
                  std::make_move_iterator(_new_received_sync_items.end()),
                  std::front_inserter(_received_sync_items));
        _new_received_sync_items.clear();
        dlog("currently ${count} sync items to consider", ("count", _received_sync_items.size()));

        block_processed_this_iteration = false;
        for (auto received_block_iter = _received_sync_items.begin();
             received_block_iter != _received_sync_items.end();
             ++received_block_iter)
        {

          // find out if this block is the next block on the active chain or one of the forks
          bool potential_first_block = false;
          {
            fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
            for (const peer_connection_ptr& peer : _active_connections)
            {
               if (!peer->ids_of_items_to_get.empty() &&
                     peer->ids_of_items_to_get.front() == received_block_iter->block_id)
               {
                  potential_first_block = true;
                  peer->ids_of_items_to_get.pop_front();
                  peer->ids_of_items_being_processed.insert(received_block_iter->block_id);
               }
            }
          }

          // if it is, process it, remove it from all sync peers lists
          if (potential_first_block)
          {
            // we can get into an interesting situation near the end of synchronization.  We can be in
            // sync with one peer who is sending us the last block on the chain via a regular inventory
            // message, while at the same time still be synchronizing with a peer who is sending us the
            // block through the sync mechanism.  Further, we must request both blocks because
            // we don't know they're the same (for the peer in normal operation, it has only told us the
            // message id, for the peer in the sync case we only known the block_id).
            if (std::find(_most_recent_blocks_accepted.begin(), _most_recent_blocks_accepted.end(),
                          received_block_iter->block_id) == _most_recent_blocks_accepted.end())
            {
              graphene::net::block_message block_message_to_process = *received_block_iter;
              _received_sync_items.erase(received_block_iter);
              _handle_message_calls_in_progress.emplace_back(fc::async([this, block_message_to_process](){
                send_sync_block_to_node_delegate(block_message_to_process);
              }, "send_sync_block_to_node_delegate"));
              ++blocks_processed;
              block_processed_this_iteration = true;
            }
            else
            {
              dlog("Already received and accepted this block (presumably through normal inventory mechanism), treating it as accepted");
              std::vector< peer_connection_ptr > peers_needing_next_batch;
              fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
              for (const peer_connection_ptr& peer : _active_connections)
              {
                auto items_being_processed_iter = peer->ids_of_items_being_processed.find(received_block_iter->block_id);
                if (items_being_processed_iter != peer->ids_of_items_being_processed.end())
                {
                  peer->ids_of_items_being_processed.erase(items_being_processed_iter);
                  dlog("Removed item from ${endpoint}'s list of items being processed, still processing ${len} blocks",
                       ("endpoint", peer->get_remote_endpoint())("len", peer->ids_of_items_being_processed.size()));

                  // if we just processed the last item in our list from this peer, we will want to
                  // send another request to find out if we are now in sync (this is normally handled in
                  // send_sync_block_to_node_delegate)
                  if (peer->ids_of_items_to_get.empty() &&
                      peer->number_of_unfetched_item_ids == 0 &&
                      peer->ids_of_items_being_processed.empty())
                  {
                    dlog("We received last item in our list for peer ${endpoint}, setup to do a sync check", ("endpoint", peer->get_remote_endpoint()));
                    peers_needing_next_batch.push_back( peer );
                  }
                }
              }
              for( const peer_connection_ptr& peer : peers_needing_next_batch )
                fetch_next_batch_of_item_ids_from_peer(peer.get());
            }

            break; // start iterating _received_sync_items from the beginning
          } // end if potential_first_block
        } // end for each block in _received_sync_items

        if (_handle_message_calls_in_progress.size() >= _max_blocks_to_handle_at_once)
        {
          dlog("stopping processing sync block backlog because we have ${count} blocks in progress",
               ("count", _handle_message_calls_in_progress.size()));
          //ulog("stopping processing sync block backlog because we have ${count} blocks in progress, total on hand: ${received}",
          //     ("count", _handle_message_calls_in_progress.size())("received", _received_sync_items.size()));
          if (_received_sync_items.size() >= _max_sync_blocks_to_prefetch)
            _suspend_fetching_sync_blocks = true;
          break;
        }
      } while (block_processed_this_iteration);

      dlog("leaving process_backlog_of_sync_blocks, ${count} processed", ("count", blocks_processed));

      if (!_suspend_fetching_sync_blocks)
        trigger_fetch_sync_items_loop();
    }

    void node_impl::trigger_process_backlog_of_sync_blocks()
    {
      if (!_node_is_shutting_down &&
          (!_process_backlog_of_sync_blocks_done.valid() || _process_backlog_of_sync_blocks_done.ready()))
        _process_backlog_of_sync_blocks_done = fc::async( [this](){ process_backlog_of_sync_blocks(); },
                                                          "process_backlog_of_sync_blocks" );
    }

    void node_impl::process_block_during_syncing( peer_connection* originating_peer,
                                               const graphene::net::block_message& block_message_to_process,
                                               const message_hash_type& )
    {
      VERIFY_CORRECT_THREAD();
      dlog( "received a sync block from peer ${endpoint}", ("endpoint", originating_peer->get_remote_endpoint() ) );

      // add it to the front of _received_sync_items, then process _received_sync_items to try to
      // pass as many messages as possible to the client.
      _new_received_sync_items.push_front( block_message_to_process );
      trigger_process_backlog_of_sync_blocks();
    }

    void node_impl::process_block_when_in_sync( peer_connection* originating_peer,
                                               const graphene::net::block_message& block_message_to_process,
                                               const message_hash_type& message_hash )
    {
      fc::time_point message_receive_time = fc::time_point::now();

      dlog( "received a block from peer ${endpoint}, passing it to client",
            ("endpoint", originating_peer->get_remote_endpoint() ) );
      std::set<peer_connection_ptr> peers_to_disconnect;
      std::string disconnect_reason;
      fc::oexception disconnect_exception;
      fc::oexception restart_sync_exception;
      try
      {
        // we can get into an intersting situation near the end of synchronization.  We can be in
        // sync with one peer who is sending us the last block on the chain via a regular inventory
        // message, while at the same time still be synchronizing with a peer who is sending us the
        // block through the sync mechanism.  Further, we must request both blocks because
        // we don't know they're the same (for the peer in normal operation, it has only told us the
        // message id, for the peer in the sync case we only known the block_id).
        fc::time_point message_validated_time;
        if (std::find(_most_recent_blocks_accepted.begin(), _most_recent_blocks_accepted.end(),
                      block_message_to_process.block_id) == _most_recent_blocks_accepted.end())
        {
          std::vector<message_hash_type> contained_transaction_msg_ids;
          _delegate->handle_block(block_message_to_process, false, contained_transaction_msg_ids);
          message_validated_time = fc::time_point::now();
          dlog("Successfully pushed block ${num} (id:${id})",
                ("num", block_message_to_process.block.block_num())
                ("id", block_message_to_process.block_id));
          _most_recent_blocks_accepted.push_back(block_message_to_process.block_id);

          bool new_transaction_discovered = false;
          for (const item_hash_t& transaction_message_hash : contained_transaction_msg_ids)
          {
            /*size_t items_erased =*/
            _items_to_fetch.get<item_id_index>().erase(item_id(trx_message_type, transaction_message_hash));
            // there are two ways we could behave here: we could either act as if we received
            // the transaction outside the block and offer it to our peers, or we could just
            // forget about it (we would still advertise this block to our peers so they should
            // get the transaction through that mechanism).
            // We take the second approach, bring in the next if block to try the first approach
            //if (items_erased)
            //{
            //  new_transaction_discovered = true;
            //  _new_inventory.insert(item_id(trx_message_type, transaction_message_hash));
            //}
          }
          if (new_transaction_discovered)
            trigger_advertise_inventory_loop();
        }
        else
          dlog( "Already received and accepted this block (presumably through sync mechanism), treating it as accepted" );

        dlog( "client validated the block, advertising it to other peers" );

        item_id block_message_item_id(core_message_type_enum::block_message_type, message_hash);
        uint32_t block_number = block_message_to_process.block.block_num();
        fc::time_point_sec block_time = block_message_to_process.block.timestamp;
        {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& peer : _active_connections)
         {
            auto iter = peer->inventory_peer_advertised_to_us.find(block_message_item_id);
            if (iter != peer->inventory_peer_advertised_to_us.end())
            {
               // this peer offered us the item.  It will eventually expire from the peer's
               // inventory_peer_advertised_to_us list after some time has passed (currently 2 minutes).
               // For now, it will remain there, which will prevent us from offering the peer this
               // block back when we rebroadcast the block below
               peer->last_block_delegate_has_seen = block_message_to_process.block_id;
               peer->last_block_time_delegate_has_seen = block_time;
            }
            peer->clear_old_inventory();
         }
        }
        message_propagation_data propagation_data { message_receive_time, message_validated_time,
                                                    originating_peer->node_id };
        broadcast( block_message_to_process, propagation_data );
        _message_cache.block_accepted();

        if (is_hard_fork_block(block_number))
        {
          // we just pushed a hard fork block.  Find out if any of our peers are running clients
          // that will be unable to process future blocks
          fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
          for (const peer_connection_ptr& peer : _active_connections)
          {
            if (peer->last_known_fork_block_number != 0)
            {
              uint32_t next_fork_block_number = get_next_known_hard_fork_block_number(peer->last_known_fork_block_number);
              if (next_fork_block_number != 0 &&
                  next_fork_block_number <= block_number)
              {
                peers_to_disconnect.insert(peer);
#ifdef ENABLE_DEBUG_ULOGS
                ulog("Disconnecting from peer because their version is too old.  Their version date: ${date}", ("date", peer->graphene_git_revision_unix_timestamp));
#endif
              }
            }
          }
          if (!peers_to_disconnect.empty())
          {
            std::ostringstream disconnect_reason_stream;
            disconnect_reason_stream << "You need to upgrade your client due to hard fork at block " << block_number;
            disconnect_reason = disconnect_reason_stream.str();
            disconnect_exception = fc::exception(FC_LOG_MESSAGE(error, "You need to upgrade your client due to hard fork at block ${block_number}",
                                                                ("block_number", block_number)));
          }
        }
      }
      catch (const fc::canceled_exception&)
      {
        throw;
      }
      catch (const unlinkable_block_exception& e)
      {
        restart_sync_exception = e;
      }
      catch (const fc::exception& e)
      {
        // client rejected the block.  Disconnect the client and any other clients that offered us this block
        auto block_num = block_message_to_process.block.block_num();
        wlog("Failed to push block ${num} (id:${id}), client rejected block sent by peer: ${e}",
              ("num", block_num)
              ("id", block_message_to_process.block_id)
              ("e",e));

        if( e.code() == block_timestamp_in_future_exception::code_enum::code_value )
        {
           disconnect_exception = block_timestamp_in_future_exception( FC_LOG_MESSAGE( warn, "",
                ("block_header", static_cast<graphene::protocol::block_header>(block_message_to_process.block))
                ("block_num", block_num)
                ("block_id", block_message_to_process.block_id) ) );
        }
        else
           disconnect_exception = e;
        disconnect_reason = "You offered me a block that I have deemed to be invalid";

        peers_to_disconnect.insert( originating_peer->shared_from_this() );
        fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
        for (const peer_connection_ptr& peer : _active_connections)
          if (!peer->ids_of_items_to_get.empty() && peer->ids_of_items_to_get.front() == block_message_to_process.block_id)
            peers_to_disconnect.insert(peer);
      }

      if (restart_sync_exception)
      {
        wlog("Peer ${peer} sent me a block that didn't link to our blockchain.  Restarting sync mode with them to get the missing block. "
             "Error pushing block was: ${e}",
             ("peer", originating_peer->get_remote_endpoint())
             ("e", *restart_sync_exception));
        start_synchronizing_with_peer(originating_peer->shared_from_this());
      }

      for (const peer_connection_ptr& peer : peers_to_disconnect)
      {
        wlog("disconnecting client ${endpoint} because it offered us the rejected block",
             ("endpoint", peer->get_remote_endpoint()));
        disconnect_from_peer(peer.get(), disconnect_reason, true, *disconnect_exception);
      }
    }
    void node_impl::process_block_message(peer_connection* originating_peer,
                                          const message& message_to_process,
                                          const message_hash_type& message_hash)
    {
      VERIFY_CORRECT_THREAD();
      // find out whether we requested this item while we were synchronizing or during normal operation
      // (it's possible that we request an item during normal operation and then get kicked into sync
      // mode before we receive and process the item.  In that case, we should process the item as a normal
      // item to avoid confusing the sync code)
      graphene::net::block_message block_message_to_process(message_to_process.as<graphene::net::block_message>());
      auto item_iter = originating_peer->items_requested_from_peer.find(
                             item_id(graphene::net::block_message_type, message_hash));
      if (item_iter != originating_peer->items_requested_from_peer.end())
      {
        originating_peer->items_requested_from_peer.erase(item_iter);
        process_block_when_in_sync(originating_peer, block_message_to_process, message_hash);
        if (originating_peer->idle())
          trigger_fetch_items_loop();
        return;
      }
      else
      {
        // not during normal operation.  see if we requested it during sync
        auto sync_item_iter = originating_peer->sync_items_requested_from_peer.find( block_message_to_process.block_id);
        if (sync_item_iter != originating_peer->sync_items_requested_from_peer.end())
        {
          originating_peer->sync_items_requested_from_peer.erase(sync_item_iter);
          // if exceptions are throw here after removing the sync item from the list (above),
          // it could leave our sync in a stalled state.  Wrap a try/catch around the rest
          // of the function so we can log if this ever happens.
          try
          {
            originating_peer->last_sync_item_received_time = fc::time_point::now();
            _active_sync_requests.erase(block_message_to_process.block_id);
            process_block_during_syncing(originating_peer, block_message_to_process, message_hash);
            if (originating_peer->idle())
            {
              // we have finished fetching a batch of items, so we either need to grab another batch of items
              // or we need to get another list of item ids.
              if (originating_peer->number_of_unfetched_item_ids > 0 &&
                  originating_peer->ids_of_items_to_get.size() < GRAPHENE_NET_MIN_BLOCK_IDS_TO_PREFETCH)
                fetch_next_batch_of_item_ids_from_peer(originating_peer);
              else
                trigger_fetch_sync_items_loop();
            }
            return;
          }
          catch (const fc::canceled_exception& e)
          {
            throw;
          }
          catch (const fc::exception& e)
          {
            elog("Caught unexpected exception: ${e}", ("e", e));
            assert(false && "exceptions not expected here");
          }
          catch (const std::exception& e)
          {
            elog("Caught unexpected exception: ${e}", ("e", e.what()));
            assert(false && "exceptions not expected here");
          }
          catch (...)
          {
            elog("Caught unexpected exception, could break sync operation");
          }
        }
      }

      // if we get here, we didn't request the message, we must have a misbehaving peer
      wlog("received a block ${block_id} I didn't ask for from peer ${endpoint}, disconnecting from peer",
           ("endpoint", originating_peer->get_remote_endpoint())
           ("block_id", block_message_to_process.block_id));
      fc::exception detailed_error(FC_LOG_MESSAGE(error, "You sent me a block that I didn't ask for, block_id: ${block_id}",
                                                  ("block_id", block_message_to_process.block_id)
                                                  ("graphene_git_revision_sha", originating_peer->graphene_git_revision_sha)
                                                  ("graphene_git_revision_unix_timestamp", originating_peer->graphene_git_revision_unix_timestamp)
                                                  ("fc_git_revision_sha", originating_peer->fc_git_revision_sha)
                                                  ("fc_git_revision_unix_timestamp", originating_peer->fc_git_revision_unix_timestamp)));
      disconnect_from_peer(originating_peer, "You sent me a block that I didn't ask for", true, detailed_error);
    }

    void node_impl::on_current_time_request_message(peer_connection* originating_peer,
                                                    const current_time_request_message& current_time_request_message_received)
    {
      VERIFY_CORRECT_THREAD();
      fc::time_point request_received_time(fc::time_point::now());
      current_time_reply_message reply(current_time_request_message_received.request_sent_time,
                                       request_received_time);
      originating_peer->send_message(reply, offsetof(current_time_reply_message, reply_transmitted_time));
    }

    void node_impl::on_current_time_reply_message(peer_connection* originating_peer,
                                                  const current_time_reply_message& current_time_reply_message_received)
    {
      VERIFY_CORRECT_THREAD();
      fc::time_point reply_received_time = fc::time_point::now();
      constexpr uint8_t two = 2;
      originating_peer->clock_offset = fc::microseconds( ( (current_time_reply_message_received.request_received_time
                                                            - current_time_reply_message_received.request_sent_time)
                                                         + (current_time_reply_message_received.reply_transmitted_time
                                                            - reply_received_time) ).count() / two );
      originating_peer->round_trip_delay = ( reply_received_time
                                             - current_time_reply_message_received.request_sent_time )
                                         - ( current_time_reply_message_received.reply_transmitted_time
                                             - current_time_reply_message_received.request_received_time );
    }

    // this handles any message we get that doesn't require any special processing.
    // currently, this is any message other than block messages and p2p-specific
    // messages.  (transaction messages would be handled here, for example)
    // this just passes the message to the client, and does the bookkeeping
    // related to requesting and rebroadcasting the message.
    void node_impl::process_ordinary_message( peer_connection* originating_peer,
                                              const message& message_to_process,
                                              const message_hash_type& message_hash )
    {
      VERIFY_CORRECT_THREAD();
      fc::time_point message_receive_time = fc::time_point::now();

      // only process it if we asked for it
      auto iter = originating_peer->items_requested_from_peer.find(
                        item_id(message_to_process.msg_type.value(), message_hash) );
      if( iter == originating_peer->items_requested_from_peer.end() )
      {
        wlog( "received a message I didn't ask for from peer ${endpoint}, disconnecting from peer",
             ( "endpoint", originating_peer->get_remote_endpoint() ) );
        fc::exception detailed_error( FC_LOG_MESSAGE(error,
                            "You sent me a message that I didn't ask for, message_hash: ${message_hash}",
                            ( "message_hash", message_hash ) ) );
        disconnect_from_peer( originating_peer, "You sent me a message that I didn't request", true, detailed_error );
        return;
      }
      else
      {
        originating_peer->items_requested_from_peer.erase( iter );
        if (originating_peer->idle())
          trigger_fetch_items_loop();

        // Next: have the delegate process the message
        fc::time_point message_validated_time;
        try
        {
          if (message_to_process.msg_type.value() == trx_message_type)
          {
            trx_message transaction_message_to_process = message_to_process.as<trx_message>();
            dlog( "passing message containing transaction ${trx} to client",
                  ("trx", transaction_message_to_process.trx.id()) );
            _delegate->handle_transaction(transaction_message_to_process);
          }
          else
            _delegate->handle_message( message_to_process );
          message_validated_time = fc::time_point::now();
        }
        catch ( const fc::canceled_exception& )
        {
          throw;
        }
        catch ( const fc::exception& e )
        {
          switch( e.code() )
          {
          // log common exceptions in debug level
          case graphene::chain::duplicate_transaction::code_enum::code_value :
          case graphene::chain::limit_order_create_kill_unfilled::code_enum::code_value :
          case graphene::chain::limit_order_create_market_not_whitelisted::code_enum::code_value :
          case graphene::chain::limit_order_create_market_blacklisted::code_enum::code_value :
          case graphene::chain::limit_order_create_selling_asset_unauthorized::code_enum::code_value :
          case graphene::chain::limit_order_create_receiving_asset_unauthorized::code_enum::code_value :
          case graphene::chain::limit_order_create_insufficient_balance::code_enum::code_value :
          case graphene::chain::limit_order_update_nonexist_order::code_enum::code_value :
          case graphene::chain::limit_order_update_owner_mismatch::code_enum::code_value :
          case graphene::chain::limit_order_cancel_nonexist_order::code_enum::code_value :
          case graphene::chain::limit_order_cancel_owner_mismatch::code_enum::code_value :
          case graphene::chain::liquidity_pool_exchange_unfillable_price::code_enum::code_value :
             dlog( "client rejected message sent by peer ${peer}, ${e}",
                   ("peer", originating_peer->get_remote_endpoint() )("e", e) );
             break;
          // log rarer exceptions in warn level
          default:
             wlog( "client rejected message sent by peer ${peer}, ${e}",
                   ("peer", originating_peer->get_remote_endpoint() )("e", e) );
             break;
          }
          // record it so we don't try to fetch this item again
          _recently_failed_items.insert( peer_connection::timestamped_item_id(
                item_id( message_to_process.msg_type.value(), message_hash ), fc::time_point::now() ) );
          return;
        }

        // finally, if the delegate validated the message, broadcast it to our other peers
        message_propagation_data propagation_data { message_receive_time, message_validated_time,
                                                    originating_peer->node_id };
        broadcast( message_to_process, propagation_data );
      }
    }

    void node_impl::start_synchronizing_with_peer( const peer_connection_ptr& peer )
    {
      VERIFY_CORRECT_THREAD();
      peer->ids_of_items_to_get.clear();
      peer->number_of_unfetched_item_ids = 0;
      peer->we_need_sync_items_from_peer = true;
      peer->last_block_delegate_has_seen = item_hash_t();
      peer->last_block_time_delegate_has_seen = _delegate->get_block_time(item_hash_t());
      peer->inhibit_fetching_sync_blocks = false;
      fetch_next_batch_of_item_ids_from_peer( peer.get() );
    }

    void node_impl::start_synchronizing()
    {
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for( const peer_connection_ptr& peer : _active_connections )
        start_synchronizing_with_peer( peer );
    }

    void node_impl::new_peer_just_added( const peer_connection_ptr& peer )
    {
      VERIFY_CORRECT_THREAD();
      peer->send_message(current_time_request_message(),
                         offsetof(current_time_request_message, request_sent_time));
      start_synchronizing_with_peer( peer );
      if( _active_connections.size() != _last_reported_number_of_conns )
      {
        _last_reported_number_of_conns = (uint32_t)_active_connections.size();
        _delegate->connection_count_changed( _last_reported_number_of_conns );
      }
      // If it is an inbound connection, try to verify its inbound endpoint
      if( peer_connection_direction::inbound == peer->direction )
      {
         for( const auto& potential_inbound_endpoint : peer->potential_inbound_endpoints )
            _add_once_node_list.emplace_back( potential_inbound_endpoint.first );
      }
    }

    void node_impl::close()
    {
      VERIFY_CORRECT_THREAD();

      try
      {
        _potential_peer_db.close();
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while closing P2P peer database, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while closing P2P peer database, ignoring" );
      }

      // First, stop accepting incoming network connections
      try
      {
        _tcp_server.close();
        dlog("P2P TCP server closed");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while closing P2P TCP server, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while closing P2P TCP server, ignoring" );
      }

      try
      {
        _accept_loop_complete.cancel_and_wait("node_impl::close()");
        dlog("P2P accept loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating P2P accept loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating P2P accept loop, ignoring" );
      }

      // terminate all of our long-running loops (these run continuously instead of rescheduling themselves)
      try
      {
        _p2p_network_connect_loop_done.cancel("node_impl::close()");
        // cancel() is currently broken, so we need to wake up the task to allow it to finish
        trigger_p2p_network_connect_loop();
        _p2p_network_connect_loop_done.wait();
        dlog("P2P connect loop terminated");
      }
      catch ( const fc::canceled_exception& )
      {
        dlog("P2P connect loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating P2P connect loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating P2P connect loop, ignoring" );
      }

      try
      {
        _process_backlog_of_sync_blocks_done.cancel_and_wait("node_impl::close()");
        dlog("Process backlog of sync items task terminated");
      }
      catch ( const fc::canceled_exception& )
      {
        dlog("Process backlog of sync items task terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Process backlog of sync items task, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Process backlog of sync items task, ignoring" );
      }

      size_t handle_message_call_count = 0;
      while( true )
      {
        auto it = _handle_message_calls_in_progress.begin();
        if( it == _handle_message_calls_in_progress.end() )
           break;
        if( it->ready() || it->error() || it->canceled() )
        {
           _handle_message_calls_in_progress.erase( it );
           continue;
        }
        ++handle_message_call_count;
        try
        {
          it->cancel_and_wait("node_impl::close()");
          dlog("handle_message call #${count} task terminated", ("count", handle_message_call_count));
        }
        catch ( const fc::canceled_exception& )
        {
          dlog("handle_message call #${count} task terminated", ("count", handle_message_call_count));
        }
        catch ( const fc::exception& e )
        {
          wlog("Exception thrown while terminating handle_message call #${count} task, ignoring: ${e}", ("e", e)("count", handle_message_call_count));
        }
        catch (...)
        {
          wlog("Exception thrown while terminating handle_message call #${count} task, ignoring",("count", handle_message_call_count));
        }
      }

      try
      {
        _fetch_sync_items_loop_done.cancel("node_impl::close()");
        // cancel() is currently broken, so we need to wake up the task to allow it to finish
        trigger_fetch_sync_items_loop();
        _fetch_sync_items_loop_done.wait();
        dlog("Fetch sync items loop terminated");
      }
      catch ( const fc::canceled_exception& )
      {
        dlog("Fetch sync items loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Fetch sync items loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Fetch sync items loop, ignoring" );
      }

      try
      {
        _fetch_item_loop_done.cancel("node_impl::close()");
        // cancel() is currently broken, so we need to wake up the task to allow it to finish
        trigger_fetch_items_loop();
        _fetch_item_loop_done.wait();
        dlog("Fetch items loop terminated");
      }
      catch ( const fc::canceled_exception& )
      {
        dlog("Fetch items loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Fetch items loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Fetch items loop, ignoring" );
      }

      try
      {
        _advertise_inventory_loop_done.cancel("node_impl::close()");
        // cancel() is currently broken, so we need to wake up the task to allow it to finish
        trigger_advertise_inventory_loop();
        _advertise_inventory_loop_done.wait();
        dlog("Advertise inventory loop terminated");
      }
      catch ( const fc::canceled_exception& )
      {
        dlog("Advertise inventory loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Advertise inventory loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Advertise inventory loop, ignoring" );
      }


      // Next, terminate our existing connections.  First, close all of the connections nicely.
      // This will close the sockets and may result in calls to our "on_connection_closing"
      // method to inform us that the connection really closed (or may not if we manage to cancel
      // the read loop before it gets an EOF).
      // operate off copies of the lists in case they change during iteration
      std::list<peer_connection_ptr> all_peers;
      auto p_back = [&all_peers](const peer_connection_ptr& conn) { all_peers.push_back(conn); };
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         std::for_each(_active_connections.begin(), _active_connections.end(), p_back);
      }
      {
         fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
         std::for_each(_handshaking_connections.begin(), _handshaking_connections.end(), p_back);
      }
      {
         fc::scoped_lock<fc::mutex> lock(_closing_connections.get_mutex());
         std::for_each(_closing_connections.begin(), _closing_connections.end(), p_back);
      }

      for (const peer_connection_ptr& peer : all_peers)
      {
        try
        {
          peer->destroy_connection();
        }
        catch ( const fc::exception& e )
        {
          wlog( "Exception thrown while closing peer connection, ignoring: ${e}", ("e", e) );
        }
        catch (...)
        {
          wlog( "Exception thrown while closing peer connection, ignoring" );
        }
      }

      // and delete all of the peer_connection objects
      _active_connections.clear();
      _handshaking_connections.clear();
      _closing_connections.clear();
      all_peers.clear();

      {
#ifdef USE_PEERS_TO_DELETE_MUTEX
        fc::scoped_lock<fc::mutex> lock(_peers_to_delete_mutex);
#endif
        try
        {
          _delayed_peer_deletion_task_done.cancel_and_wait("node_impl::close()");
          dlog("Delayed peer deletion task terminated");
        }
        catch ( const fc::exception& e )
        {
          wlog( "Exception thrown while terminating Delayed peer deletion task, ignoring: ${e}", ("e", e) );
        }
        catch (...)
        {
          wlog( "Exception thrown while terminating Delayed peer deletion task, ignoring" );
        }
        _peers_to_delete.clear();
      }

      // Now that there are no more peers that can call methods on us, there should be no
      // chance for one of our loops to be rescheduled, so we can safely terminate all of
      // our loops now
      try
      {
        _kill_inactive_conns_loop_done.cancel_and_wait("node_impl::close()");
        dlog("Kill inactive connections loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Terminate inactive connections loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Terminate inactive connections loop, ignoring" );
      }

      try
      {
        _fetch_updated_peer_lists_loop_done.cancel_and_wait("node_impl::close()");
        dlog("Fetch updated peer lists loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Fetch updated peer lists loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Fetch updated peer lists loop, ignoring" );
      }

      try
      {
        _update_seed_nodes_loop_done.cancel_and_wait("node_impl::close()");
        dlog("Update seed nodes loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Update seed nodes loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Update seed nodes loop, ignoring" );
      }

      try
      {
        _bandwidth_monitor_loop_done.cancel_and_wait("node_impl::close()");
        dlog("Bandwidth monitor loop terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Bandwidth monitor loop, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Bandwidth monitor loop, ignoring" );
      }

      try
      {
        _dump_node_status_task_done.cancel_and_wait("node_impl::close()");
        dlog("Dump node status task terminated");
      }
      catch ( const fc::exception& e )
      {
        wlog( "Exception thrown while terminating Dump node status task, ignoring: ${e}", ("e", e) );
      }
      catch (...)
      {
        wlog( "Exception thrown while terminating Dump node status task, ignoring" );
      }
    } // node_impl::close()

    void node_impl::accept_connection_task( peer_connection_ptr new_peer )
    {
      VERIFY_CORRECT_THREAD();
      new_peer->accept_connection(); // this blocks until the secure connection is fully negotiated
      send_hello_message(new_peer);
    }

    void node_impl::accept_loop()
    {
      VERIFY_CORRECT_THREAD();
      while ( !_accept_loop_complete.canceled() )
      {
        peer_connection_ptr new_peer(peer_connection::make_shared(this));

        try
        {
          _tcp_server.accept( new_peer->get_socket() );
          ilog( "accepted inbound connection from ${remote_endpoint}",
                ("remote_endpoint", new_peer->get_socket().remote_endpoint() ) );
          if (_node_is_shutting_down)
            return;
          new_peer->connection_initiation_time = fc::time_point::now();
          _handshaking_connections.insert( new_peer );
          _rate_limiter.add_tcp_socket( &new_peer->get_socket() );
          std::weak_ptr<peer_connection> new_weak_peer(new_peer);
          new_peer->accept_or_connect_task_done = fc::async( [this, new_weak_peer]() {
            peer_connection_ptr new_peer(new_weak_peer.lock());
            assert(new_peer);
            if (!new_peer)
              return;
            accept_connection_task(new_peer);
          }, "accept_connection_task" );

          // limit the rate at which we accept connections to mitigate DOS attacks
          fc::usleep( fc::milliseconds(10) );
        } FC_CAPTURE_AND_LOG( (0) ) // GCOVR_EXCL_LINE
      }
    } // accept_loop()

    void node_impl::send_hello_message(const peer_connection_ptr& peer)
    {
      VERIFY_CORRECT_THREAD();
      peer->negotiation_status = peer_connection::connection_negotiation_status::hello_sent;

      fc::sha256::encoder shared_secret_encoder;
      fc::sha512 shared_secret = peer->get_shared_secret();
      shared_secret_encoder.write(shared_secret.data(), sizeof(shared_secret));
      fc::ecc::compact_signature signature
            = _node_configuration.private_key.sign_compact(shared_secret_encoder.result());

      // In the hello messsage, we send three things:
      // * inbound IP address
      // * inbound port
      // * outbound port
      //
      // If we don't accept incoming connections, we send nothing.
      //
      // The peer we're connecting to may assume we're firewalled if the
      // IP address and outbound port we send don't match the values it sees on its remote endpoint,
      // but it is not always true, E.G. if the peer itself is behind a reverse proxy.
      //
      // Note: we no longer perform remote firewall check (ask the peer to check whether we are firewalled),
      //       thus we don't know our external IP address,
      //       nor we know whether we're behind NAT or a reverse proxy that will allow incoming connections.
      //       However, if the "p2p-inbound-endpoint" node startup option is configured, we send that instead.

      fc::ip::address inbound_address; // default 0.0.0.0
      uint16_t inbound_port = 0;
      uint16_t outbound_port = 0;
      if( _node_configuration.accept_incoming_connections )
      {
         fc::ip::endpoint local_endpoint = peer->get_socket().local_endpoint();
         inbound_address = local_endpoint.get_address();
         inbound_port = _actual_listening_endpoint.port();
         outbound_port = local_endpoint.port();
         if( _node_configuration.inbound_endpoint.valid() )
         {
            if( _node_configuration.inbound_endpoint->get_address() != fc::ip::address() )
               inbound_address = _node_configuration.inbound_endpoint->get_address();
            inbound_port = _node_configuration.inbound_endpoint->port();
         }
      }

      hello_message hello(_user_agent_string,
                          core_protocol_version,
                          inbound_address,
                          inbound_port,
                          outbound_port,
                          _node_public_key,
                          signature,
                          _chain_id,
                          generate_hello_user_data());

      peer->send_message(message(hello));
    }

    void node_impl::connect_to_task(peer_connection_ptr new_peer,
                                    const fc::ip::endpoint& remote_endpoint)
    {
      VERIFY_CORRECT_THREAD();

      // create or find the database entry for the new peer
      auto updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep(remote_endpoint);
      updated_peer_record.last_connection_disposition = last_connection_failed;
      updated_peer_record.last_connection_attempt_time = fc::time_point::now();;
      _potential_peer_db.update_entry(updated_peer_record);

      fc::oexception connect_failed_exception;

      try
      {
        ilog("Connecting to peer ${peer}", ("peer", remote_endpoint));
        // blocks until the connection is established and secure connection is negotiated
        auto bind_to_endpoint = _node_configuration.accept_incoming_connections ? _actual_listening_endpoint
                                                                                : fc::optional<fc::ip::endpoint>();
        new_peer->connect_to( remote_endpoint, bind_to_endpoint );

        // we connected to the peer.  guess they're not firewalled....
        new_peer->is_firewalled = firewalled_state::not_firewalled;

        // connection succeeded, we've started handshaking.  record that in our database
        updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep(remote_endpoint);
        updated_peer_record.last_connection_disposition = last_connection_handshaking_failed;
        updated_peer_record.number_of_successful_connection_attempts++;
        updated_peer_record.last_seen_time = fc::time_point::now();
        _potential_peer_db.update_entry(updated_peer_record);
      }
      catch (const fc::exception& except)
      {
        connect_failed_exception = except;
      }

      if( connect_failed_exception )
      {
        // connection failed.  record that in our database
        updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep(remote_endpoint);
        updated_peer_record.last_connection_disposition = last_connection_failed;
        updated_peer_record.number_of_failed_connection_attempts++;
        if (new_peer->connection_closed_error)
          updated_peer_record.last_error = *new_peer->connection_closed_error;
        else
          updated_peer_record.last_error = *connect_failed_exception;
        _potential_peer_db.update_entry(updated_peer_record);

        // If this is for inbound endpoint verification,
        // here we could try to find the original connection and update its firewalled state,
        // but it doesn't seem necessary.

        // if the connection failed, we want to disconnect now.
        _handshaking_connections.erase(new_peer);
        _terminating_connections.erase(new_peer);
        _active_connections.erase(new_peer);
        _closing_connections.erase(new_peer);

        display_current_connections();
        trigger_p2p_network_connect_loop();
        schedule_peer_for_deletion(new_peer);

        throw *connect_failed_exception;
      }
      else
      {
        // connection was successful and we want to stay connected
        fc::ip::endpoint local_endpoint = new_peer->get_local_endpoint();
        new_peer->inbound_address = local_endpoint.get_address();
        new_peer->inbound_port = _node_configuration.accept_incoming_connections ? _actual_listening_endpoint.port()
                                                                                 : 0;
        new_peer->outbound_port = local_endpoint.port();

        new_peer->our_state = peer_connection::our_connection_state::just_connected;
        new_peer->their_state = peer_connection::their_connection_state::just_connected;
        send_hello_message(new_peer);
        ilog("Sent \"hello\" to peer ${peer}", ("peer", new_peer->get_remote_endpoint()));
      }
    }

    // methods implementing node's public interface
    void node_impl::set_node_delegate(std::shared_ptr<node_delegate> del, fc::thread* thread_for_delegate_calls)
    {
      VERIFY_CORRECT_THREAD();
      _delegate.reset();
      if (del)
        _delegate = std::make_unique<statistics_gathering_node_delegate_wrapper>(del, thread_for_delegate_calls);
      if( _delegate )
        _chain_id = del->get_chain_id();
    }

    void node_impl::load_configuration( const fc::path& configuration_directory )
    {
      VERIFY_CORRECT_THREAD();
      _node_configuration_directory = configuration_directory;
      fc::path configuration_file_name( _node_configuration_directory / NODE_CONFIGURATION_FILENAME );
      bool node_configuration_loaded = false;
      if( fc::exists(configuration_file_name ) )
      {
        try
        {
          _node_configuration = fc::json::from_file( configuration_file_name ).as<detail::node_configuration>(GRAPHENE_NET_MAX_NESTED_OBJECTS);
          ilog( "Loaded configuration from file ${filename}", ("filename", configuration_file_name ) );

          if( _node_configuration.private_key == fc::ecc::private_key() )
            _node_configuration.private_key = fc::ecc::private_key::generate();

          node_configuration_loaded = true;
        }
        catch ( fc::parse_error_exception& parse_error )
        {
          elog( "malformed node configuration file ${filename}: ${error}",
               ( "filename", configuration_file_name )("error", parse_error.to_detail_string() ) );
        }
        catch ( fc::exception& except )
        {
          elog( "unexpected exception while reading configuration file ${filename}: ${error}",
               ( "filename", configuration_file_name )("error", except.to_detail_string() ) );
        }
      }

      if( !node_configuration_loaded )
      {
        _node_configuration = detail::node_configuration();

#ifdef GRAPHENE_TEST_NETWORK
        uint32_t port = GRAPHENE_NET_TEST_P2P_PORT;
#else
        uint32_t port = GRAPHENE_NET_DEFAULT_P2P_PORT;
#endif
        _node_configuration.listen_endpoint.set_port( port );

        ilog( "generating new private key for this node" );
        _node_configuration.private_key = fc::ecc::private_key::generate();
        save_node_configuration();
      }

      _node_public_key = _node_configuration.private_key.get_public_key().serialize();

      fc::path potential_peer_database_file_name(_node_configuration_directory / POTENTIAL_PEER_DATABASE_FILENAME);
      try
      {
        _potential_peer_db.open(potential_peer_database_file_name);

        // push back the time on all peers loaded from the database so we will be able to retry them immediately
        // Note: this step is almost useless because we didn't multiply _peer_connection_retry_timeout
        //       by number_of_failed_connection_attempts. However, it is probably desired as we don't want
        //       to try to connect to a large number of dead nodes at startup.
        //       As of writing, _peer_connection_retry_timeout is 30 seconds, pushing the time back that much
        //       won't have much impact in production.
        //       TODO Perhaps just remove it.
        for (peer_database::iterator itr = _potential_peer_db.begin(); itr != _potential_peer_db.end(); ++itr)
        {
          potential_peer_record updated_peer_record = *itr;
          updated_peer_record.last_connection_attempt_time = std::min<fc::time_point_sec>(updated_peer_record.last_connection_attempt_time,
                                                                                          fc::time_point::now() - fc::seconds(_peer_connection_retry_timeout));
          _potential_peer_db.update_entry(updated_peer_record);
        }

        trigger_p2p_network_connect_loop();
      }
      catch (fc::exception& except)
      {
        elog("unable to open peer database ${filename}: ${error}",
             ("filename", potential_peer_database_file_name)("error", except.to_detail_string()));
        throw;
      }
    }

    void node_impl::listen_to_p2p_network()
    {
      VERIFY_CORRECT_THREAD();
      if (!_node_configuration.accept_incoming_connections)
      {
        wlog("accept_incoming_connections is false, p2p network will not accept any incoming connections");
        return;
      }

      assert(_node_public_key != fc::ecc::public_key_data());

      fc::ip::endpoint listen_endpoint = _node_configuration.listen_endpoint;
      if( listen_endpoint.port() != 0 )
      {
        // if the user specified a port, we only want to bind to it if it's not already
        // being used by another application.  During normal operation, we set the
        // SO_REUSEADDR/SO_REUSEPORT flags so that we can bind outbound sockets to the
        // same local endpoint as we're listening on here.  On some platforms, setting
        // those flags will prevent us from detecting that other applications are
        // listening on that port.  We'd like to detect that, so we'll set up a temporary
        // tcp server without that flag to see if we can listen on that port.
        // Note: There is a race condition where another application may start listening
        //       on the same port just after the temporary tcp server is destroyed and
        //       before we try to listen with the real tcp server.
        //       This happens frequently when running multiple test cases at the same
        //       time, but less likely in production.
        bool first = true;
        for( ;; )
        {
          bool listen_failed = false;

          try
          {
            fc::tcp_server temporary_server;
            if( listen_endpoint.get_address() != fc::ip::address() )
              temporary_server.listen( listen_endpoint );
            else
              temporary_server.listen( listen_endpoint.port() );
            break;
          }
          catch ( const fc::exception&)
          {
            listen_failed = true;
          }

          if (listen_failed)
          {
            if( _node_configuration.wait_if_endpoint_is_busy )
            {
              std::ostringstream error_message_stream;
              if( first )
              {
                error_message_stream << "Unable to listen for connections on port " << listen_endpoint.port()
                                     << ", retrying in a few seconds\n";
                error_message_stream << "You can wait for it to become available, or restart this program using\n";
                error_message_stream << "the --p2p-endpoint option to specify another port\n";
                first = false;
              }
              else
              {
                error_message_stream << "\nStill waiting for port " << listen_endpoint.port() << " to become available\n";
              }
              std::string error_message = error_message_stream.str();
              wlog(error_message);
              std::cout << "\033[31m" << error_message;  
              _delegate->error_encountered( error_message, fc::oexception() );
              fc::usleep( fc::seconds(5 ) );
            }
            else // don't wait, just find a random port
            {
              wlog( "unable to bind on the requested endpoint ${endpoint}, which probably means that endpoint is already in use",
                   ( "endpoint", listen_endpoint ) );
              listen_endpoint.set_port( 0 );
            }
          } // if (listen_failed)
        } // for(;;)
      } // if (listen_endpoint.port() != 0)
      else // port is 0
      {
        // if they requested a random port, we'll just assume it's available
        // (it may not be due to ip address, but we'll detect that in the next step)
      }

      _tcp_server.set_reuse_address();
      try
      {
        if( listen_endpoint.get_address() != fc::ip::address() )
          _tcp_server.listen( listen_endpoint );
        else
          _tcp_server.listen( listen_endpoint.port() );
        _actual_listening_endpoint = _tcp_server.get_local_endpoint();
        ilog( "listening for connections on endpoint ${endpoint} (our first choice)",
              ( "endpoint", _actual_listening_endpoint ) );
      }
      catch ( fc::exception& e )
      {
        FC_RETHROW_EXCEPTION( e, error, "unable to listen on ${endpoint}", ("endpoint",listen_endpoint ) );
      }
    }

    void node_impl::connect_to_p2p_network(node_impl_ptr self)
    {
      VERIFY_CORRECT_THREAD();
      assert(_node_public_key != fc::ecc::public_key_data());

      assert(!_accept_loop_complete.valid() &&
             !_p2p_network_connect_loop_done.valid() &&
             !_update_seed_nodes_loop_done.valid() &&
             !_fetch_sync_items_loop_done.valid() &&
             !_fetch_item_loop_done.valid() &&
             !_advertise_inventory_loop_done.valid() &&
             !_kill_inactive_conns_loop_done.valid() &&
             !_fetch_updated_peer_lists_loop_done.valid() &&
             !_bandwidth_monitor_loop_done.valid() &&
             !_dump_node_status_task_done.valid());
      if (_node_configuration.accept_incoming_connections)
        _accept_loop_complete = fc::async( [this](){ accept_loop(); }, "accept_loop");

      _p2p_network_connect_loop_done = fc::async( [this]() { p2p_network_connect_loop(); },
                                                  "p2p_network_connect_loop" );
      _fetch_sync_items_loop_done = fc::async( [this]() { fetch_sync_items_loop(); }, "fetch_sync_items_loop" );
      _fetch_item_loop_done = fc::async( [this]() { fetch_items_loop(); }, "fetch_items_loop" );
      _advertise_inventory_loop_done = fc::async( [this]() { advertise_inventory_loop(); },
                                                  "advertise_inventory_loop" );
      _kill_inactive_conns_loop_done = fc::async( [this,self]() { kill_inactive_conns_loop(self); },
                                                  "kill_inactive_conns_loop" );
      _fetch_updated_peer_lists_loop_done = fc::async([this](){ fetch_updated_peer_lists_loop(); },
                                                                "fetch_updated_peer_lists_loop");
      _bandwidth_monitor_loop_done = fc::async([this](){ bandwidth_monitor_loop(); }, "bandwidth_monitor_loop");
      _dump_node_status_task_done = fc::async([this](){ dump_node_status_task(); }, "dump_node_status_task");
      schedule_next_update_seed_nodes_task();
    }

    void node_impl::add_node(const fc::ip::endpoint& ep)
    {
      VERIFY_CORRECT_THREAD();
      // if we're connecting to them, we believe they're not firewalled
      auto updated_peer_record = _potential_peer_db.lookup_or_create_entry_for_ep(ep);

      // if we've recently connected to this peer, reset the last_connection_attempt_time to allow
      // us to immediately retry this peer
      // Note: to make it work, we need to multiply _peer_connection_retry_timeout
      //         by number_of_failed_connection_attempts.
      //       However, this step is almost useless because we will immediately try to connect anyway
      //         due to _add_once_node_list.
      //       On the other hand, if we connected to the peer already but it was not in the peer database somehow,
      //         this step makes sure that it will be added to the peer database.
      auto delay_until_retry = fc::seconds( (updated_peer_record.number_of_failed_connection_attempts + 1)
                                            * _peer_connection_retry_timeout );
      updated_peer_record.last_connection_attempt_time
            = std::min<fc::time_point_sec>( updated_peer_record.last_connection_attempt_time,
                                            fc::time_point::now() - delay_until_retry );
      _add_once_node_list.push_back(updated_peer_record);
      _potential_peer_db.update_entry(updated_peer_record);
      trigger_p2p_network_connect_loop();
    }

   void node_impl::add_seed_node(const std::string& endpoint_string)
   {
      VERIFY_CORRECT_THREAD();
      _seed_nodes.insert( endpoint_string );
      resolve_seed_node_and_add( endpoint_string );
   }

   /**
    * @brief Helper to convert a string to a collection of endpoints
    *
    * This converts a string (i.e. "bitshares.eu:665535") to a collection of endpoints.
    * NOTE: Throws an exception if not in correct format or was unable to resolve URL.
    *
    * @param in the incoming string
    * @returns a vector of endpoints
    */
   static std::vector<fc::ip::endpoint> resolve_string_to_ip_endpoints(const std::string& in)
   {
      try
      {
         std::string::size_type colon_pos = in.find(':');
         if (colon_pos == std::string::npos)
            FC_THROW("Missing required port number in endpoint string \"${endpoint_string}\"",
                  ("endpoint_string", in));
         std::string port_string = in.substr(colon_pos + 1);
         try
         {
            uint16_t port = boost::lexical_cast<uint16_t>(port_string);

            std::string hostname = in.substr(0, colon_pos);
            std::vector<fc::ip::endpoint> endpoints = fc::resolve(hostname, port);
            if (endpoints.empty())
               FC_THROW_EXCEPTION( fc::unknown_host_exception,
                     "The host name can not be resolved: ${hostname}",
                     ("hostname", hostname) );
            return endpoints;
         }
         catch (const boost::bad_lexical_cast&)
         {
            FC_THROW("Bad port: ${port}", ("port", port_string));
         }
      }
      FC_CAPTURE_AND_RETHROW((in)) // GCOVR_EXCL_LINE
   }

   void node_impl::resolve_seed_node_and_add(const std::string& endpoint_string)
   {
      VERIFY_CORRECT_THREAD();
      std::vector<fc::ip::endpoint> endpoints;
      ilog("Resolving seed node ${endpoint}", ("endpoint", endpoint_string));
      try
      {
         endpoints = resolve_string_to_ip_endpoints(endpoint_string);
      }
      catch(...)
      {
         wlog( "Unable to resolve endpoint during attempt to add seed node ${ep}", ("ep", endpoint_string) );
      }
      for (const fc::ip::endpoint& endpoint : endpoints)
      {
         ilog("Adding seed node ${endpoint}", ("endpoint", endpoint));
         add_node(endpoint);
      }
   }

    void node_impl::initiate_connect_to(const peer_connection_ptr& new_peer)
    {
      new_peer->get_socket().open();
      new_peer->get_socket().set_reuse_address();
      new_peer->connection_initiation_time = fc::time_point::now();
      _handshaking_connections.insert(new_peer);
      _rate_limiter.add_tcp_socket(&new_peer->get_socket());

      if (_node_is_shutting_down)
        return;

      std::weak_ptr<peer_connection> new_weak_peer(new_peer);
      new_peer->accept_or_connect_task_done = fc::async([this, new_weak_peer](){
        peer_connection_ptr new_peer(new_weak_peer.lock());
        assert(new_peer);
        if (!new_peer)
          return;
        connect_to_task(new_peer, *new_peer->get_remote_endpoint());
      }, "connect_to_task");
    }

    void node_impl::connect_to_endpoint(const fc::ip::endpoint& remote_endpoint)
    {
      VERIFY_CORRECT_THREAD();
      if( is_connected_to_endpoint(remote_endpoint) )
        FC_THROW_EXCEPTION(already_connected_to_requested_peer, "already connected to requested endpoint ${endpoint}",
                           ("endpoint", remote_endpoint));

      dlog("node_impl::connect_to_endpoint(${endpoint})", ("endpoint", remote_endpoint));
      peer_connection_ptr new_peer(peer_connection::make_shared(this));
      new_peer->set_remote_endpoint(remote_endpoint);
      initiate_connect_to(new_peer);
    }

   peer_connection_ptr node_impl::get_active_conn_for_endpoint( const fc::ip::endpoint& remote_endpoint ) const
   {
      VERIFY_CORRECT_THREAD();
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for( const peer_connection_ptr& active_peer : _active_connections )
      {
         // Note: for outbound connections, checking by remote_endpoint is OK,
         //         and we will ignore the inbound address and port it sends to us when handshaking.
         //       For an inbound active connection, we want to verify its inbound endpoint, if it happens to be
         //         the same as remote_endpoint but not yet verified, we consider it as not connected.
         //       * If verification succeeds, we will mark it as "verified" and won't try to connect again.
         //       * We may fail to verify if it is firewalled, in this case number_of_failed_connection_attempts
         //         will increase, so we will not reconnect soon, but will wait longer and longer.
         fc::optional<fc::ip::endpoint> endpoint_for_this_peer = active_peer->get_remote_endpoint();
         if( peer_connection_direction::outbound == active_peer->direction
             && endpoint_for_this_peer && *endpoint_for_this_peer == remote_endpoint )
            return active_peer;
         // Note: if it is an inbound connection and its inbound endpoint is verified already,
         //       the inbound endpoint should be in additional_inbound_endpoints
         if( active_peer->additional_inbound_endpoints.find( remote_endpoint )
             != active_peer->additional_inbound_endpoints.end() )
            return active_peer;
      }
      return peer_connection_ptr();
   }

   peer_connection_ptr node_impl::get_connection_for_endpoint( const fc::ip::endpoint& remote_endpoint ) const
   {
      VERIFY_CORRECT_THREAD();
      peer_connection_ptr active_ptr = get_active_conn_for_endpoint( remote_endpoint );
      if ( active_ptr != peer_connection_ptr() )
         return active_ptr;
      fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
      for( const peer_connection_ptr& handshaking_peer : _handshaking_connections )
      {
         // For an inbound handshaking connection, there is a race condition since we might not know its node_id yet,
         // so be stricter here.
         // Even so, there may be situations that we end up having multiple active connections with them.
         fc::optional<fc::ip::endpoint> endpoint_for_this_peer = handshaking_peer->get_remote_endpoint();
         if( endpoint_for_this_peer && *endpoint_for_this_peer == remote_endpoint )
            return handshaking_peer;
         // Note: if it is an inbound connection and its inbound endpoint is verified already,
         //       the inbound endpoint should be in additional_inbound_endpoints
         if( handshaking_peer->additional_inbound_endpoints.find( remote_endpoint )
             != handshaking_peer->additional_inbound_endpoints.end() )
            return handshaking_peer;
      }
      return peer_connection_ptr();
   }

    bool node_impl::is_connected_to_endpoint( const fc::ip::endpoint& remote_endpoint ) const
    {
      VERIFY_CORRECT_THREAD();
      return get_connection_for_endpoint( remote_endpoint ) != peer_connection_ptr();
    }

    void node_impl::move_peer_to_active_list(const peer_connection_ptr& peer)
    {
      VERIFY_CORRECT_THREAD();
      _active_connections.insert(peer);
      _handshaking_connections.erase(peer);
      _closing_connections.erase(peer);
      _terminating_connections.erase(peer);
    }

    void node_impl::move_peer_to_closing_list(const peer_connection_ptr& peer)
    {
      VERIFY_CORRECT_THREAD();
      _active_connections.erase(peer);
      _handshaking_connections.erase(peer);
      _closing_connections.insert(peer);
      _terminating_connections.erase(peer);
    }

    void node_impl::move_peer_to_terminating_list(const peer_connection_ptr& peer)
    {
      VERIFY_CORRECT_THREAD();
      _active_connections.erase(peer);
      _handshaking_connections.erase(peer);
      _closing_connections.erase(peer);
      _terminating_connections.insert(peer);
    }

    void node_impl::dump_node_status()
    {
      VERIFY_CORRECT_THREAD();
      ilog( "----------------- PEER STATUS UPDATE --------------------" );
      ilog( " number of peers: ${active} active, ${handshaking} handshaking, ${closing} closing. "
            " attempting to maintain ${desired} - ${maximum} peers",
           ( "active", _active_connections.size() )("handshaking", _handshaking_connections.size() )
           ( "closing", _closing_connections.size() )
           ( "desired", _desired_number_of_connections )("maximum", _maximum_number_of_connections ) );
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for( const peer_connection_ptr& peer : _active_connections )
         {
            ilog( "       active peer ${endpoint} [${direction}] (${inbound_ep} ${is_firewalled}) "
                  "peer_is_in_sync_with_us:${in_sync_with_us} we_are_in_sync_with_peer:${in_sync_with_them}",
                  ( "endpoint", peer->get_remote_endpoint() )
                  ( "direction", peer->direction )
                  ( "inbound_ep", peer->get_endpoint_for_connecting() )
                  ( "is_firewalled", peer->is_firewalled)
                  ( "in_sync_with_us", !peer->peer_needs_sync_items_from_us )
                  ( "in_sync_with_them", !peer->we_need_sync_items_from_peer ) );
            if( peer->we_need_sync_items_from_peer )
               ilog( "              above peer has ${count} sync items we might need",
                     ("count", peer->ids_of_items_to_get.size() ) );
            if (peer->inhibit_fetching_sync_blocks)
               ilog( "              we are not fetching sync blocks from the above peer "
                     "(inhibit_fetching_sync_blocks == true)" );

         }
      }
      {
         fc::scoped_lock<fc::mutex> lock(_handshaking_connections.get_mutex());
         for( const peer_connection_ptr& peer : _handshaking_connections )
         {
            ilog( "  handshaking peer ${endpoint} [${direction}] in state ours(${our_state}) theirs(${their_state})",
                  ( "endpoint", peer->get_remote_endpoint() )
                  ( "direction", peer->direction )
                  ( "our_state", peer->our_state )( "their_state", peer->their_state ) );
         }
      }
      ilog( "--------- MEMORY USAGE ------------" );
      ilog( "node._active_sync_requests size: ${size}", ("size", _active_sync_requests.size() ) );
      ilog( "node._received_sync_items size: ${size}", ("size", _received_sync_items.size() ) );
      ilog( "node._new_received_sync_items size: ${size}", ("size", _new_received_sync_items.size() ) );
      ilog( "node._items_to_fetch size: ${size}", ("size", _items_to_fetch.size() ) );
      ilog( "node._new_inventory size: ${size}", ("size", _new_inventory.size() ) );
      ilog( "node._message_cache size: ${size}", ("size", _message_cache.size() ) );
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for( const peer_connection_ptr& peer : _active_connections )
      {
        ilog( "  peer ${endpoint}", ("endpoint", peer->get_remote_endpoint() ) );
        ilog( "    peer.ids_of_items_to_get size: ${size}", ("size", peer->ids_of_items_to_get.size() ) );
        ilog( "    peer.inventory_peer_advertised_to_us size: ${size}", ("size", peer->inventory_peer_advertised_to_us.size() ) );
        ilog( "    peer.inventory_advertised_to_peer size: ${size}", ("size", peer->inventory_advertised_to_peer.size() ) );
        ilog( "    peer.items_requested_from_peer size: ${size}", ("size", peer->items_requested_from_peer.size() ) );
        ilog( "    peer.sync_items_requested_from_peer size: ${size}", ("size", peer->sync_items_requested_from_peer.size() ) );
      }
      ilog( "--------- END MEMORY USAGE ------------" );
    }

    void node_impl::disconnect_from_peer( peer_connection* peer_to_disconnect,
                                          const std::string& reason_for_disconnect,
                                          bool caused_by_error /* = false */,
                                          const fc::oexception& error /* = fc::oexception() */ )
    {
      VERIFY_CORRECT_THREAD();
      move_peer_to_closing_list(peer_to_disconnect->shared_from_this());

      if (peer_to_disconnect->they_have_requested_close)
      {
        // the peer has already told us that it's ready to close the connection, so just close the connection
        peer_to_disconnect->close_connection();
      }
      else if( peer_to_disconnect->we_have_requested_close )
      {
         dlog( "Disconnecting again from ${peer} for ${reason}, ignore",
              ("peer",peer_to_disconnect->get_remote_endpoint()) ("reason",reason_for_disconnect));
         return;
      }
      else
      {
        // we're the first to try to want to close the connection
        fc::optional<fc::ip::endpoint> inbound_endpoint = peer_to_disconnect->get_endpoint_for_connecting();
        if( inbound_endpoint.valid() && inbound_endpoint->port() != 0 )
        {
          fc::optional<potential_peer_record> updated_peer_record
                = _potential_peer_db.lookup_entry_for_endpoint(*inbound_endpoint);
          if (updated_peer_record)
          {
            updated_peer_record->last_seen_time = fc::time_point::now();
            if (error)
              updated_peer_record->last_error = error;
            else
              updated_peer_record->last_error = fc::exception(FC_LOG_MESSAGE(info, reason_for_disconnect.c_str()));
            _potential_peer_db.update_entry(*updated_peer_record);
          }
        }
        peer_to_disconnect->we_have_requested_close = true;
        peer_to_disconnect->connection_closed_time = fc::time_point::now();

        closing_connection_message closing_message( reason_for_disconnect, caused_by_error, error );
        peer_to_disconnect->send_message( closing_message );
      }

      // notify the user.  This will be useful in testing, but we might want to remove it later.
      // It makes good sense to notify the user if other nodes think she is behaving badly, but
      // if we're just detecting and dissconnecting other badly-behaving nodes, they don't really care.
      if (caused_by_error)
      {
        std::ostringstream error_message;
        error_message << "I am disconnecting peer " << fc::variant( peer_to_disconnect->get_remote_endpoint(), GRAPHENE_NET_MAX_NESTED_OBJECTS ).as_string() <<
                         " for reason: " << reason_for_disconnect;
        _delegate->error_encountered(error_message.str(), fc::oexception());
        dlog(error_message.str());
      }
      else
        dlog("Disconnecting from ${peer} for ${reason}", ("peer",peer_to_disconnect->get_remote_endpoint()) ("reason",reason_for_disconnect));
    }

    void node_impl::set_listen_endpoint( const fc::ip::endpoint& ep, bool wait_if_not_available )
    {
      VERIFY_CORRECT_THREAD();
      _node_configuration.listen_endpoint = ep;
      _node_configuration.wait_if_endpoint_is_busy = wait_if_not_available;
      save_node_configuration();
    }

    void node_impl::set_inbound_endpoint( const fc::ip::endpoint& ep )
    {
      VERIFY_CORRECT_THREAD();
      _node_configuration.inbound_endpoint = ep;
      save_node_configuration();
    }

    void node_impl::set_accept_incoming_connections(bool accept)
    {
      VERIFY_CORRECT_THREAD();
      _node_configuration.accept_incoming_connections = accept;
      save_node_configuration();
    }

   void node_impl::set_connect_to_new_peers( bool connect )
   {
      VERIFY_CORRECT_THREAD();
      _node_configuration.connect_to_new_peers = connect;
      save_node_configuration();
   }

    fc::ip::endpoint node_impl::get_actual_listening_endpoint() const
    {
      VERIFY_CORRECT_THREAD();
      return _actual_listening_endpoint;
    }

    std::vector<peer_status> node_impl::get_connected_peers() const
    {
      VERIFY_CORRECT_THREAD();
      std::vector<peer_status> statuses;
      fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
      for (const peer_connection_ptr& peer : _active_connections)
      {
        peer_status this_peer_status;
        this_peer_status.version = 0;
        fc::optional<fc::ip::endpoint> endpoint = peer->get_remote_endpoint();
        if (endpoint)
          this_peer_status.host = *endpoint;
        fc::mutable_variant_object peer_details;
        peer_details["addr"] = endpoint ? (std::string)*endpoint : std::string();
        peer_details["addrlocal"] = (std::string)peer->get_local_endpoint();
        peer_details["services"] = "00000001";
        peer_details["lastsend"] = peer->get_last_message_sent_time().sec_since_epoch();
        peer_details["lastrecv"] = peer->get_last_message_received_time().sec_since_epoch();
        peer_details["bytessent"] = peer->get_total_bytes_sent();
        peer_details["bytesrecv"] = peer->get_total_bytes_received();
        peer_details["conntime"] = peer->get_connection_time();
        peer_details["pingtime"] = "";
        peer_details["pingwait"] = "";
        peer_details["version"] = "";
        peer_details["subver"] = peer->user_agent;
        peer_details["inbound"] = peer->direction == peer_connection_direction::inbound;
        peer_details["firewall_status"] = fc::variant( peer->is_firewalled, 1 );
        peer_details["startingheight"] = "";
        peer_details["banscore"] = "";
        peer_details["syncnode"] = "";

        if (peer->fc_git_revision_sha)
        {
          std::string revision_string = *peer->fc_git_revision_sha;
          if (*peer->fc_git_revision_sha == fc::git_revision_sha)
            revision_string += " (same as ours)";
          else
            revision_string += " (different from ours)";
          peer_details["fc_git_revision_sha"] = revision_string;

        }
        if (peer->fc_git_revision_unix_timestamp)
        {
          peer_details["fc_git_revision_unix_timestamp"] = *peer->fc_git_revision_unix_timestamp;
          std::string age_string = fc::get_approximate_relative_time_string( *peer->fc_git_revision_unix_timestamp);
          if (*peer->fc_git_revision_unix_timestamp == fc::time_point_sec(fc::git_revision_unix_timestamp))
            age_string += " (same as ours)";
          else if (*peer->fc_git_revision_unix_timestamp > fc::time_point_sec(fc::git_revision_unix_timestamp))
            age_string += " (newer than ours)";
          else
            age_string += " (older than ours)";
          peer_details["fc_git_revision_age"] = age_string;
        }

        if (peer->platform)
          peer_details["platform"] = *peer->platform;

        // provide these for debugging
        // warning: these are just approximations, if the peer is "downstream" of us, they may
        // have received blocks from other peers that we are unaware of
        peer_details["current_head_block"] = fc::variant( peer->last_block_delegate_has_seen, 1 );
        peer_details["current_head_block_number"] = _delegate->get_block_number(peer->last_block_delegate_has_seen);
        peer_details["current_head_block_time"] = peer->last_block_time_delegate_has_seen;

        peer_details["peer_needs_sync_items_from_us"] = peer->peer_needs_sync_items_from_us;
        peer_details["we_need_sync_items_from_peer"] = peer->we_need_sync_items_from_peer;

        this_peer_status.info = peer_details;
        statuses.push_back(this_peer_status);
      }
      return statuses;
    }

    uint32_t node_impl::get_connection_count() const
    {
      VERIFY_CORRECT_THREAD();
      return (uint32_t)_active_connections.size();
    }

    void node_impl::broadcast( const message& item_to_broadcast, const message_propagation_data& propagation_data )
    {
      VERIFY_CORRECT_THREAD();
      message_hash_type hash_of_message_contents;
      if( item_to_broadcast.msg_type.value() == graphene::net::block_message_type )
      {
        graphene::net::block_message block_message_to_broadcast = item_to_broadcast.as<graphene::net::block_message>();
        hash_of_message_contents = block_message_to_broadcast.block_id; // for debugging
        _most_recent_blocks_accepted.push_back( block_message_to_broadcast.block_id );
      }
      else if( item_to_broadcast.msg_type.value() == graphene::net::trx_message_type )
      {
        graphene::net::trx_message transaction_message_to_broadcast = item_to_broadcast.as<graphene::net::trx_message>();
        hash_of_message_contents = transaction_message_to_broadcast.trx.id(); // for debugging
        dlog( "broadcasting trx: ${trx}", ("trx", transaction_message_to_broadcast) );
      }
      message_hash_type hash_of_item_to_broadcast = item_to_broadcast.id();

      _message_cache.cache_message( item_to_broadcast, hash_of_item_to_broadcast, propagation_data, hash_of_message_contents );
      _new_inventory.insert( item_id(item_to_broadcast.msg_type.value(), hash_of_item_to_broadcast ) );
      trigger_advertise_inventory_loop();
    }

    void node_impl::broadcast( const message& item_to_broadcast )
    {
      VERIFY_CORRECT_THREAD();
      // this version is called directly from the client
      message_propagation_data propagation_data{fc::time_point::now(), fc::time_point::now(), _node_id};
      broadcast( item_to_broadcast, propagation_data );
    }

    void node_impl::sync_from(const item_id& current_head_block, const std::vector<uint32_t>& hard_fork_block_numbers)
    {
      VERIFY_CORRECT_THREAD();
      _most_recent_blocks_accepted.clear();
      _sync_item_type = current_head_block.item_type;
      _most_recent_blocks_accepted.push_back(current_head_block.item_hash);
      _hard_fork_block_numbers = hard_fork_block_numbers;
    }

    bool node_impl::is_connected() const
    {
      VERIFY_CORRECT_THREAD();
      return !_active_connections.empty();
    }

    std::vector<potential_peer_record> node_impl::get_potential_peers() const
    {
      VERIFY_CORRECT_THREAD();
      std::vector<potential_peer_record> result;
      // use explicit iterators here, for some reason the mac compiler can't used ranged-based for loops here
      for (peer_database::iterator itr = _potential_peer_db.begin(); itr != _potential_peer_db.end(); ++itr)
        result.push_back(*itr);
      return result;
    }

    void node_impl::set_advanced_node_parameters(const fc::variant_object& params)
    {
      VERIFY_CORRECT_THREAD();
      if (params.contains("peer_connection_retry_timeout"))
        _peer_connection_retry_timeout = params["peer_connection_retry_timeout"].as<uint32_t>(1);
      if (params.contains("desired_number_of_connections"))
        _desired_number_of_connections = params["desired_number_of_connections"].as<uint32_t>(1);
      if (params.contains("maximum_number_of_connections"))
        _maximum_number_of_connections = params["maximum_number_of_connections"].as<uint32_t>(1);
      if (params.contains("max_addresses_to_handle_at_once"))
        _max_addrs_to_handle_at_once = params["max_addresses_to_handle_at_once"].as<uint32_t>(1);
      if (params.contains("max_blocks_to_handle_at_once"))
        _max_blocks_to_handle_at_once = params["max_blocks_to_handle_at_once"].as<uint32_t>(1);
      if (params.contains("max_sync_blocks_to_prefetch"))
        _max_sync_blocks_to_prefetch = params["max_sync_blocks_to_prefetch"].as<uint32_t>(1);
      if (params.contains("max_sync_blocks_per_peer"))
        _max_sync_blocks_per_peer = params["max_sync_blocks_per_peer"].as<uint32_t>(1);

      _desired_number_of_connections = std::min(_desired_number_of_connections, _maximum_number_of_connections);

      while (_active_connections.size() > _maximum_number_of_connections)
        disconnect_from_peer(_active_connections.begin()->get(),
                             "I have too many connections open");
      trigger_p2p_network_connect_loop();
    }

    fc::variant_object node_impl::get_advanced_node_parameters()
    {
      VERIFY_CORRECT_THREAD();
      fc::mutable_variant_object result;
      result["peer_connection_retry_timeout"] = _peer_connection_retry_timeout;
      result["desired_number_of_connections"] = _desired_number_of_connections;
      result["maximum_number_of_connections"] = _maximum_number_of_connections;
      result["max_addresses_to_handle_at_once"] = _max_addrs_to_handle_at_once;
      result["max_blocks_to_handle_at_once"] = _max_blocks_to_handle_at_once;
      result["max_sync_blocks_to_prefetch"] = _max_sync_blocks_to_prefetch;
      result["max_sync_blocks_per_peer"] = _max_sync_blocks_per_peer;
      return result;
    }

    message_propagation_data node_impl::get_tx_propagation_data(
          const graphene::net::transaction_id_type& transaction_id ) const
    {
      VERIFY_CORRECT_THREAD();
      return _message_cache.get_message_propagation_data( transaction_id );
    }

    message_propagation_data node_impl::get_block_propagation_data(
          const graphene::net::block_id_type& block_id ) const
    {
      VERIFY_CORRECT_THREAD();
      return _message_cache.get_message_propagation_data( block_id );
    }

    node_id_t node_impl::get_node_id() const
    {
      VERIFY_CORRECT_THREAD();
      return _node_id;
    }
    void node_impl::set_allowed_peers(const std::vector<node_id_t>& allowed_peers)
    {
      VERIFY_CORRECT_THREAD();
#ifdef ENABLE_P2P_DEBUGGING_API
      _allowed_peers.clear();
      _allowed_peers.insert(allowed_peers.begin(), allowed_peers.end());
      std::list<peer_connection_ptr> peers_to_disconnect;
      if (!_allowed_peers.empty())
      {
         fc::scoped_lock<fc::mutex> lock(_active_connections.get_mutex());
         for (const peer_connection_ptr& peer : _active_connections)
            if (_allowed_peers.find(peer->node_id) == _allowed_peers.end())
               peers_to_disconnect.push_back(peer);
      }
      for (const peer_connection_ptr& peer : peers_to_disconnect)
        disconnect_from_peer(peer.get(), "My allowed_peers list has changed, and you're no longer allowed.  Bye.");
#endif // ENABLE_P2P_DEBUGGING_API
    }
    void node_impl::clear_peer_database()
    {
      VERIFY_CORRECT_THREAD();
      _potential_peer_db.clear();
    }

    void node_impl::set_total_bandwidth_limit( uint32_t upload_bytes_per_second, uint32_t download_bytes_per_second )
    {
      VERIFY_CORRECT_THREAD();
      _rate_limiter.set_upload_limit( upload_bytes_per_second );
      _rate_limiter.set_download_limit( download_bytes_per_second );
    }

    fc::variant_object node_impl::get_call_statistics() const
    {
      VERIFY_CORRECT_THREAD();
      return _delegate->get_call_statistics();
    }

    fc::variant_object node_impl::network_get_info() const
    {
      VERIFY_CORRECT_THREAD();
      fc::mutable_variant_object info;
      info["listening_on"] = std::string( _actual_listening_endpoint );
      info["node_public_key"] = fc::variant( _node_public_key, 1 );
      info["node_id"] = fc::variant( _node_id, 1 );
      return info;
    }
    fc::variant_object node_impl::network_get_usage_stats() const
    {
      VERIFY_CORRECT_THREAD();
      std::vector<uint32_t> network_usage_by_second;
      network_usage_by_second.reserve(_avg_net_read_speed_seconds.size());
      std::transform(_avg_net_read_speed_seconds.begin(), _avg_net_read_speed_seconds.end(),
                     _avg_net_write_speed_seconds.begin(),
                     std::back_inserter(network_usage_by_second),
                     std::plus<uint32_t>());

      std::vector<uint32_t> network_usage_by_minute;
      network_usage_by_minute.reserve(_avg_net_read_speed_minutes.size());
      std::transform(_avg_net_read_speed_minutes.begin(), _avg_net_read_speed_minutes.end(),
                     _avg_net_write_speed_minutes.begin(),
                     std::back_inserter(network_usage_by_minute),
                     std::plus<uint32_t>());

      std::vector<uint32_t> network_usage_by_hour;
      network_usage_by_hour.reserve(_avg_net_read_speed_hours.size());
      std::transform(_avg_net_read_speed_hours.begin(), _avg_net_read_speed_hours.end(),
                     _avg_net_write_speed_hours.begin(),
                     std::back_inserter(network_usage_by_hour),
                     std::plus<uint32_t>());

      fc::mutable_variant_object result;
      result["usage_by_second"] = fc::variant( network_usage_by_second, 2 );
      result["usage_by_minute"] = fc::variant( network_usage_by_minute, 2 );
      result["usage_by_hour"]   = fc::variant( network_usage_by_hour, 2 );
      return result;
    }

    bool node_impl::is_hard_fork_block(uint32_t block_number) const
    {
      return std::binary_search(_hard_fork_block_numbers.begin(), _hard_fork_block_numbers.end(), block_number);
    }
    uint32_t node_impl::get_next_known_hard_fork_block_number(uint32_t block_number) const
    {
      auto iter = std::upper_bound(_hard_fork_block_numbers.begin(), _hard_fork_block_numbers.end(),
                                   block_number);
      return iter != _hard_fork_block_numbers.end() ? *iter : 0;
    }

  }  // end namespace detail



  /////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // implement node functions, they call the matching function in to detail::node_impl in the correct thread //

#ifdef P2P_IN_DEDICATED_THREAD
# define INVOKE_IN_IMPL(method_name, ...) \
    return my->_thread->async([&](){ return my->method_name(__VA_ARGS__); }, "thread invoke for method " BOOST_PP_STRINGIZE(method_name)).wait()
#else
# define INVOKE_IN_IMPL(method_name, ...) \
    return my->method_name(__VA_ARGS__)
#endif // P2P_IN_DEDICATED_THREAD

  node::node(const std::string& user_agent) :
    my(new detail::node_impl(user_agent), detail::node_impl_deleter())
  {
    // nothing else to do
  }

  node::~node()
  {
    // nothing to do
  }

  void node::set_node_delegate( std::shared_ptr<node_delegate> del ) const
  {
    fc::thread* delegate_thread = &fc::thread::current();
    INVOKE_IN_IMPL(set_node_delegate, del, delegate_thread);
  }

  void node::load_configuration( const fc::path& configuration_directory ) const
  {
    INVOKE_IN_IMPL(load_configuration, configuration_directory);
  }

  void node::listen_to_p2p_network() const
  {
    INVOKE_IN_IMPL(listen_to_p2p_network);
  }

  void node::connect_to_p2p_network() const
  {
    INVOKE_IN_IMPL(connect_to_p2p_network, my);
  }

  void node::add_node( const fc::ip::endpoint& ep ) const
  {
    INVOKE_IN_IMPL(add_node, ep);
  }

  void node::connect_to_endpoint( const fc::ip::endpoint& remote_endpoint ) const
  {
    INVOKE_IN_IMPL(connect_to_endpoint, remote_endpoint);
  }

  void node::set_listen_endpoint(const fc::ip::endpoint& ep , bool wait_if_not_available) const
  {
    INVOKE_IN_IMPL(set_listen_endpoint, ep, wait_if_not_available);
  }

  void node::set_inbound_endpoint(const fc::ip::endpoint& ep ) const
  {
    INVOKE_IN_IMPL( set_inbound_endpoint, ep );
  }

  void node::set_accept_incoming_connections(bool accept) const
  {
    INVOKE_IN_IMPL(set_accept_incoming_connections, accept);
  }

  void node::set_connect_to_new_peers( bool connect ) const
  {
     INVOKE_IN_IMPL( set_connect_to_new_peers, connect );
  }

  fc::ip::endpoint node::get_actual_listening_endpoint() const
  {
    INVOKE_IN_IMPL(get_actual_listening_endpoint);
  }

  std::vector<peer_status> node::get_connected_peers() const
  {
    INVOKE_IN_IMPL(get_connected_peers);
  }

  uint32_t node::get_connection_count() const
  {
    INVOKE_IN_IMPL(get_connection_count);
  }

  void node::broadcast( const message& msg ) const
  {
    INVOKE_IN_IMPL(broadcast, msg);
  }

  void node::sync_from(const item_id& current_head_block, const std::vector<uint32_t>& hard_fork_block_numbers) const
  {
    INVOKE_IN_IMPL(sync_from, current_head_block, hard_fork_block_numbers);
  }

  bool node::is_connected() const
  {
    INVOKE_IN_IMPL(is_connected);
  }

  std::vector<potential_peer_record> node::get_potential_peers()const
  {
    INVOKE_IN_IMPL(get_potential_peers);
  }

  void node::set_advanced_node_parameters( const fc::variant_object& params ) const
  {
    INVOKE_IN_IMPL(set_advanced_node_parameters, params);
  }

  fc::variant_object node::get_advanced_node_parameters() const
  {
    INVOKE_IN_IMPL(get_advanced_node_parameters);
  }

  message_propagation_data node::get_tx_propagation_data(
        const graphene::net::transaction_id_type& transaction_id ) const
  {
    INVOKE_IN_IMPL(get_tx_propagation_data, transaction_id);
  }

  message_propagation_data node::get_block_propagation_data( const graphene::net::block_id_type& block_id ) const
  {
    INVOKE_IN_IMPL(get_block_propagation_data, block_id);
  }

  node_id_t node::get_node_id() const
  {
    INVOKE_IN_IMPL(get_node_id);
  }

  void node::set_allowed_peers( const std::vector<node_id_t>& allowed_peers ) const
  {
    INVOKE_IN_IMPL(set_allowed_peers, allowed_peers);
  }

  void node::clear_peer_database() const
  {
    INVOKE_IN_IMPL(clear_peer_database);
  }

  void node::set_total_bandwidth_limit(uint32_t upload_bytes_per_second,
                                       uint32_t download_bytes_per_second) const
  {
    INVOKE_IN_IMPL(set_total_bandwidth_limit, upload_bytes_per_second, download_bytes_per_second);
  }

  fc::variant_object node::get_call_statistics() const
  {
    INVOKE_IN_IMPL(get_call_statistics);
  }

  fc::variant_object node::network_get_info() const
  {
    INVOKE_IN_IMPL(network_get_info);
  }

  fc::variant_object node::network_get_usage_stats() const
  {
    INVOKE_IN_IMPL(network_get_usage_stats);
  }

  void node::close() const
  {
    INVOKE_IN_IMPL(close);
  }

  namespace detail
  {
#define ROLLING_WINDOW_SIZE 1000
#define INITIALIZE_ACCUMULATOR(r, data, method_name) \
      , BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))(boost::accumulators::tag::rolling_window::window_size = ROLLING_WINDOW_SIZE) \
      , BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_before_accumulator))(boost::accumulators::tag::rolling_window::window_size = ROLLING_WINDOW_SIZE) \
      , BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_after_accumulator))(boost::accumulators::tag::rolling_window::window_size = ROLLING_WINDOW_SIZE)


    statistics_gathering_node_delegate_wrapper::statistics_gathering_node_delegate_wrapper(
            std::shared_ptr<node_delegate> delegate, fc::thread* thread_for_delegate_calls) :
      _node_delegate(delegate),
      _thread(thread_for_delegate_calls)
      BOOST_PP_SEQ_FOR_EACH(INITIALIZE_ACCUMULATOR, unused, NODE_DELEGATE_METHOD_NAMES)
    {}
#undef INITIALIZE_ACCUMULATOR

    fc::variant_object statistics_gathering_node_delegate_wrapper::get_call_statistics()
    {
      fc::mutable_variant_object statistics;
      std::ostringstream note;
      note << "All times are in microseconds, mean is the average of the last " << ROLLING_WINDOW_SIZE << " call times";
      statistics["_note"] = note.str();

#define ADD_STATISTICS_FOR_METHOD(r, data, method_name) \
      fc::mutable_variant_object BOOST_PP_CAT(method_name, _stats); \
      BOOST_PP_CAT(method_name, _stats)["min"] = boost::accumulators::min(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["mean"] = boost::accumulators::rolling_mean(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["max"] = boost::accumulators::max(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["sum"] = boost::accumulators::sum(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_before_min"] = boost::accumulators::min(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_before_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_before_mean"] = boost::accumulators::rolling_mean(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_before_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_before_max"] = boost::accumulators::max(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_before_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_before_sum"] = boost::accumulators::sum(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_before_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_after_min"] = boost::accumulators::min(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_after_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_after_mean"] = boost::accumulators::rolling_mean(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_after_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_after_max"] = boost::accumulators::max(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_after_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["delay_after_sum"] = boost::accumulators::sum(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _delay_after_accumulator))); \
      BOOST_PP_CAT(method_name, _stats)["count"] = boost::accumulators::count(BOOST_PP_CAT(_, BOOST_PP_CAT(method_name, _execution_accumulator))); \
      statistics[BOOST_PP_STRINGIZE(method_name)] = BOOST_PP_CAT(method_name, _stats);

      BOOST_PP_SEQ_FOR_EACH(ADD_STATISTICS_FOR_METHOD, unused, NODE_DELEGATE_METHOD_NAMES)
#undef ADD_STATISTICS_FOR_METHOD

      return statistics;
    }

// define VERBOSE_NODE_DELEGATE_LOGGING to log whenever the node delegate throws exceptions
//#define VERBOSE_NODE_DELEGATE_LOGGING
#ifdef VERBOSE_NODE_DELEGATE_LOGGING
#  define INVOKE_AND_COLLECT_STATISTICS(method_name, ...) \
    try \
    { \
      std::shared_ptr<call_statistics_collector> statistics_collector = std::make_shared<call_statistics_collector>( \
                                                     #method_name, \
                                                     &_ ## method_name ## _execution_accumulator, \
                                                     &_ ## method_name ## _delay_before_accumulator, \
                                                     &_ ## method_name ## _delay_after_accumulator); \
      if (_thread->is_current()) \
      { \
        call_statistics_collector::actual_execution_measurement_helper helper(statistics_collector); \
        return _node_delegate->method_name(__VA_ARGS__); \
      } \
      else \
        return _thread->async([&, statistics_collector](){ \
          call_statistics_collector::actual_execution_measurement_helper helper(statistics_collector); \
          return _node_delegate->method_name(__VA_ARGS__); \
        }, "invoke " BOOST_STRINGIZE(method_name)).wait(); \
    } \
    catch (const fc::exception& e) \
    { \
      dlog("node_delegate threw fc::exception: ${e}", ("e", e)); \
      throw; \
    } \
    catch (const std::exception& e) \
    { \
      dlog("node_delegate threw std::exception: ${e}", ("e", e.what())); \
      throw; \
    } \
    catch (...) \
    { \
      dlog("node_delegate threw unrecognized exception"); \
      throw; \
    }
#else
#  define INVOKE_AND_COLLECT_STATISTICS(method_name, ...) \
    std::shared_ptr<call_statistics_collector> statistics_collector = std::make_shared<call_statistics_collector>( \
                                                   #method_name, \
                                                   &_ ## method_name ## _execution_accumulator, \
                                                   &_ ## method_name ## _delay_before_accumulator, \
                                                   &_ ## method_name ## _delay_after_accumulator); \
    if (_thread->is_current()) \
    { \
      call_statistics_collector::actual_execution_measurement_helper helper(statistics_collector); \
      return _node_delegate->method_name(__VA_ARGS__); \
    } \
    else \
      return _thread->async([&, statistics_collector](){ \
        call_statistics_collector::actual_execution_measurement_helper helper(statistics_collector); \
        return _node_delegate->method_name(__VA_ARGS__); \
      }, "invoke " BOOST_STRINGIZE(method_name)).wait()
#endif

    bool statistics_gathering_node_delegate_wrapper::has_item( const net::item_id& id )
    {
      INVOKE_AND_COLLECT_STATISTICS(has_item, id);
    }

    void statistics_gathering_node_delegate_wrapper::handle_message( const message& message_to_handle )
    {
      INVOKE_AND_COLLECT_STATISTICS(handle_message, message_to_handle);
    }

    bool statistics_gathering_node_delegate_wrapper::handle_block( const graphene::net::block_message& block_message,
             bool sync_mode, std::vector<message_hash_type>& contained_transaction_msg_ids)
    {
      INVOKE_AND_COLLECT_STATISTICS(handle_block, block_message, sync_mode, contained_transaction_msg_ids);
    }

    void statistics_gathering_node_delegate_wrapper::handle_transaction( const graphene::net::trx_message& transaction_message )
    {
      INVOKE_AND_COLLECT_STATISTICS(handle_transaction, transaction_message);
    }

    std::vector<item_hash_t> statistics_gathering_node_delegate_wrapper::get_block_ids(const std::vector<item_hash_t>& blockchain_synopsis,
                                                                                       uint32_t& remaining_item_count,
                                                                                       uint32_t limit /* = 2000 */)
    {
      INVOKE_AND_COLLECT_STATISTICS(get_block_ids, blockchain_synopsis, remaining_item_count, limit);
    }

    message statistics_gathering_node_delegate_wrapper::get_item( const item_id& id )
    {
      INVOKE_AND_COLLECT_STATISTICS(get_item, id);
    }

    chain_id_type statistics_gathering_node_delegate_wrapper::get_chain_id() const
    {
      INVOKE_AND_COLLECT_STATISTICS(get_chain_id);
    }

    std::vector<item_hash_t> statistics_gathering_node_delegate_wrapper::get_blockchain_synopsis(const item_hash_t& reference_point, uint32_t number_of_blocks_after_reference_point)
    {
      INVOKE_AND_COLLECT_STATISTICS(get_blockchain_synopsis, reference_point, number_of_blocks_after_reference_point);
    }

    void statistics_gathering_node_delegate_wrapper::sync_status( uint32_t item_type, uint32_t item_count )
    {
      INVOKE_AND_COLLECT_STATISTICS(sync_status, item_type, item_count);
    }

    void statistics_gathering_node_delegate_wrapper::connection_count_changed( uint32_t c )
    {
      INVOKE_AND_COLLECT_STATISTICS(connection_count_changed, c);
    }

    uint32_t statistics_gathering_node_delegate_wrapper::get_block_number(const item_hash_t& block_id)
    {
      // this function doesn't need to block,
      ASSERT_TASK_NOT_PREEMPTED();
      return _node_delegate->get_block_number(block_id);
    }

    fc::time_point_sec statistics_gathering_node_delegate_wrapper::get_block_time(const item_hash_t& block_id)
    {
      INVOKE_AND_COLLECT_STATISTICS(get_block_time, block_id);
    }

    item_hash_t statistics_gathering_node_delegate_wrapper::get_head_block_id() const
    {
      INVOKE_AND_COLLECT_STATISTICS(get_head_block_id);
    }

    uint32_t statistics_gathering_node_delegate_wrapper::estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const
    {
      INVOKE_AND_COLLECT_STATISTICS(estimate_last_known_fork_from_git_revision_timestamp, unix_timestamp);
    }

    void statistics_gathering_node_delegate_wrapper::error_encountered(const std::string& message, const fc::oexception& error)
    {
      INVOKE_AND_COLLECT_STATISTICS(error_encountered, message, error);
    }

    uint8_t statistics_gathering_node_delegate_wrapper::get_current_block_interval_in_seconds() const
    {
      INVOKE_AND_COLLECT_STATISTICS(get_current_block_interval_in_seconds);
    }

#undef INVOKE_AND_COLLECT_STATISTICS

  } // end namespace detail

   void node::add_seed_nodes(const std::vector<std::string>& seeds) const
   {
      for(const std::string& endpoint_string : seeds )
      {
         try {
            add_seed_node(endpoint_string);
         } catch( const fc::exception& e ) {
            wlog( "caught exception ${e} while adding seed node ${endpoint}",
                  ("e", e.to_detail_string())("endpoint", endpoint_string) );
         }
      }
   }

   void node::add_seed_node(const std::string& in) const
   {
      INVOKE_IN_IMPL(add_seed_node, in);
   }

   void node::set_advertise_algorithm( const std::string& algo,
         const std::vector<std::string>& advertise_or_exclude_list ) const
   {
      INVOKE_IN_IMPL( set_advertise_algorithm, algo, advertise_or_exclude_list );
   }

} } // end namespace graphene::net
