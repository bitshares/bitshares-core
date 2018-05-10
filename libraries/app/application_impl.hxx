#pragma once

#include <fc/network/http/websocket.hpp>
#include <graphene/app/application.hpp>
#include <graphene/app/api_access.hpp>
#include <graphene/chain/genesis_state.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/net/message.hpp>

namespace graphene { namespace app { namespace detail {


class application_impl : public net::node_delegate
   {
   public:
      fc::optional<fc::temp_file> _lock_file;
      bool _is_block_producer = false;
      bool _force_validate = false;
      application_options _app_options;

      void reset_p2p_node(const fc::path& data_dir);

      std::vector<fc::ip::endpoint> resolve_string_to_ip_endpoints(const std::string& endpoint_string);

      void new_connection( const fc::http::websocket_connection_ptr& c );

      void reset_websocket_server();

      void reset_websocket_tls_server();

      explicit application_impl(application* self)
         : _self(self),
           _chain_db(std::make_shared<chain::database>())
      {
      }

      ~application_impl()
      {
      }

      void set_dbg_init_key( graphene::chain::genesis_state_type& genesis, const std::string& init_key );

      void startup();

      fc::optional< api_access_info > get_api_access_info(const string& username)const;

      void set_api_access_info(const string& username, api_access_info&& permissions);

      /**
       * If delegate has the item, the network has no need to fetch it.
       */
      virtual bool has_item(const net::item_id& id) override;

      /**
       * @brief allows the application to validate an item prior to broadcasting to peers.
       *
       * @param sync_mode true if the message was fetched through the sync process, false during normal operation
       * @returns true if this message caused the blockchain to switch forks, false if it did not
       *
       * @throws exception if error validating the item, otherwise the item is safe to broadcast on.
       */
      virtual bool handle_block(const graphene::net::block_message& blk_msg, bool sync_mode,
                                std::vector<fc::uint160_t>& contained_transaction_message_ids) override;

      virtual void handle_transaction(const graphene::net::trx_message& transaction_message) override;

      void handle_message(const graphene::net::message& message_to_process);

      bool is_included_block(const graphene::chain::block_id_type& block_id);

      /**
       * Assuming all data elements are ordered in some way, this method should
       * return up to limit ids that occur *after* the last ID in synopsis that
       * we recognize.
       *
       * On return, remaining_item_count will be set to the number of items
       * in our blockchain after the last item returned in the result,
       * or 0 if the result contains the last item in the blockchain
       */
      virtual std::vector<graphene::net::item_hash_t> get_block_ids(const std::vector<graphene::net::item_hash_t>& blockchain_synopsis,
                                                     uint32_t& remaining_item_count,
                                                     uint32_t limit) override;

      /**
       * Given the hash of the requested data, fetch the body.
       */
      virtual graphene::net::message get_item(const graphene::net::item_id& id) override;

      virtual graphene::chain::chain_id_type get_chain_id()const override;

      /**
       * Returns a synopsis of the blockchain used for syncing.  This consists of a list of
       * block hashes at intervals exponentially increasing towards the genesis block.
       * When syncing to a peer, the peer uses this data to determine if we're on the same
       * fork as they are, and if not, what blocks they need to send us to get us on their
       * fork.
       *
       * In the over-simplified case, this is a straighforward synopsis of our current
       * preferred blockchain; when we first connect up to a peer, this is what we will be sending.
       * It looks like this:
       *   If the blockchain is empty, it will return the empty list.
       *   If the blockchain has one block, it will return a list containing just that block.
       *   If it contains more than one block:
       *     the first element in the list will be the hash of the highest numbered block that
       *         we cannot undo
       *     the second element will be the hash of an item at the half way point in the undoable
       *         segment of the blockchain
       *     the third will be ~3/4 of the way through the undoable segment of the block chain
       *     the fourth will be at ~7/8...
       *       &c.
       *     the last item in the list will be the hash of the most recent block on our preferred chain
       * so if the blockchain had 26 blocks labeled a - z, the synopsis would be:
       *    a n u x z
       * the idea being that by sending a small (<30) number of block ids, we can summarize a huge
       * blockchain.  The block ids are more dense near the end of the chain where because we are
       * more likely to be almost in sync when we first connect, and forks are likely to be short.
       * If the peer we're syncing with in our example is on a fork that started at block 'v',
       * then they will reply to our synopsis with a list of all blocks starting from block 'u',
       * the last block they know that we had in common.
       *
       * In the real code, there are several complications.
       *
       * First, as an optimization, we don't usually send a synopsis of the entire blockchain, we
       * send a synopsis of only the segment of the blockchain that we have undo data for.  If their
       * fork doesn't build off of something in our undo history, we would be unable to switch, so there's
       * no reason to fetch the blocks.
       *
       * Second, when a peer replies to our initial synopsis and gives us a list of the blocks they think
       * we are missing, they only send a chunk of a few thousand blocks at once.  After we get those
       * block ids, we need to request more blocks by sending another synopsis (we can't just say "send me
       * the next 2000 ids" because they may have switched forks themselves and they don't track what
       * they've sent us).  For faster performance, we want to get a fairly long list of block ids first,
       * then start downloading the blocks.
       * The peer doesn't handle these follow-up block id requests any different from the initial request;
       * it treats the synopsis we send as our blockchain and bases its response entirely off that.  So to
       * get the response we want (the next chunk of block ids following the last one they sent us, or,
       * failing that, the shortest fork off of the last list of block ids they sent), we need to construct
       * a synopsis as if our blockchain was made up of:
       *    1. the blocks in our block chain up to the fork point (if there is a fork) or the head block (if no fork)
       *    2. the blocks we've already pushed from their fork (if there's a fork)
       *    3. the block ids they've previously sent us
       * Segment 3 is handled in the p2p code, it just tells us the number of blocks it has (in
       * number_of_blocks_after_reference_point) so we can leave space in the synopsis for them.
       * We're responsible for constructing the synopsis of Segments 1 and 2 from our active blockchain and
       * fork database.  The reference_point parameter is the last block from that peer that has been
       * successfully pushed to the blockchain, so that tells us whether the peer is on a fork or on
       * the main chain.
       */
      virtual std::vector<graphene::net::item_hash_t> get_blockchain_synopsis(const graphene::net::item_hash_t& reference_point,
                                                               uint32_t number_of_blocks_after_reference_point) override;

      /**
       * Call this after the call to handle_message succeeds.
       *
       * @param item_type the type of the item we're synchronizing, will be the same as item passed to the sync_from() call
       * @param item_count the number of items known to the node that haven't been sent to handle_item() yet.
       *                   After `item_count` more calls to handle_item(), the node will be in sync
       */
      virtual void sync_status(uint32_t item_type, uint32_t item_count) override;

      /**
       * Call any time the number of connected peers changes.
       */
      virtual void connection_count_changed(uint32_t c) override;

      virtual uint32_t get_block_number(const graphene::net::item_hash_t& block_id) override;

      /**
       * Returns the time a block was produced (if block_id = 0, returns genesis time).
       * If we don't know about the block, returns time_point_sec::min()
       */
      virtual fc::time_point_sec get_block_time(const graphene::net::item_hash_t& block_id) override;

      virtual graphene::net::item_hash_t get_head_block_id() const override;

      virtual uint32_t estimate_last_known_fork_from_git_revision_timestamp(uint32_t unix_timestamp) const override;

      virtual void error_encountered(const std::string& message, const fc::oexception& error) override;

      uint8_t get_current_block_interval_in_seconds() const override;

      application* _self;

      fc::path _data_dir;
      const boost::program_options::variables_map* _options = nullptr;
      api_access _apiaccess;

      std::shared_ptr<graphene::chain::database>            _chain_db;
      std::shared_ptr<graphene::net::node>                  _p2p_network;
      std::shared_ptr<fc::http::websocket_server>      _websocket_server;
      std::shared_ptr<fc::http::websocket_tls_server>  _websocket_tls_server;

      std::map<string, std::shared_ptr<abstract_plugin>> _active_plugins;
      std::map<string, std::shared_ptr<abstract_plugin>> _available_plugins;

      bool _is_finished_syncing = false;
   };

}}} // namespace graphene namespace app namespace detail
