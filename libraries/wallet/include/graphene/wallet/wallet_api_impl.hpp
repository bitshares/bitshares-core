#pragma once
#include <string>
#include <iomanip>

#include <boost/range/adaptors.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/api.hpp>
#include <fc/popcount.hpp>
#include <fc/git_revision.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/sstream.hpp>

#include <boost/container/flat_map.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/address.hpp>
#include <graphene/app/api_objects.hpp>
#include <graphene/app/api.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/utilities/git_revision.hpp>

#include "api_documentation.hpp"
#include "wallet_structs.hpp"
#include "reflect_util.hpp"

#ifndef WIN32
# include <sys/types.h>
# include <sys/stat.h>
#endif

namespace graphene { namespace wallet { 

typedef uint16_t transaction_handle_type;
class wallet_api;

namespace detail {

using std::ostream;
using std::stringstream;
using std::to_string;
using std::setprecision;
using std::fixed;
using std::ios;
using std::setiosflags;
using std::setw;
using std::endl;
using std::string;

using boost::container::flat_map;

using namespace graphene::protocol;
using namespace graphene::chain;
using namespace graphene::app;

static const string ENC_HEADER( "-----BEGIN BITSHARES SIGNED MESSAGE-----\n" );
static const string ENC_META(   "-----BEGIN META-----\n" );
static const string ENC_SIG(    "-----BEGIN SIGNATURE-----\n" );
static const string ENC_FOOTER( "-----END BITSHARES SIGNED MESSAGE-----" );

template<class T>
fc::optional<T> maybe_id( const string& name_or_id )
{
   if( std::isdigit( name_or_id.front() ) )
   {
      try
      {
         return fc::variant(name_or_id, 1).as<T>(1);
      }
      catch (const fc::exception&)
      { // not an ID
      }
   }
   return fc::optional<T>();
}

string address_to_shorthash( const graphene::protocol::address& addr );

fc::ecc::private_key derive_private_key( const std::string& prefix_string, int sequence_number );

string normalize_brain_key( string s );

struct op_prototype_visitor
{
   typedef void result_type;

   int t = 0;
   flat_map< std::string, graphene::protocol::operation >& name2op;

   op_prototype_visitor(
      int _t,
      flat_map< std::string, graphene::protocol::operation >& _prototype_ops
      ):t(_t), name2op(_prototype_ops) {}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      string name = fc::get_typename<Type>::name();
      size_t p = name.rfind(':');
      if( p != string::npos )
         name = name.substr( p+1 );
      name2op[ name ] = Type();
   }
};

class htlc_hash_to_string_visitor
{
public:
   typedef std::string result_type;

   result_type operator()( const fc::ripemd160& hash )const
   {
      return "RIPEMD160 " + hash.str();
   }

   result_type operator()( const fc::sha1& hash )const
   {
      return "SHA1 " + hash.str();
   }

   result_type operator()( const fc::sha256& hash )const
   {
      return "SHA256 " + hash.str();
   }
};

class wallet_api_impl
{
public:
   api_documentation method_documentation;
private:
   void claim_registered_account(const graphene::chain::account_object& account)
   {
      bool import_keys = false;
      auto it = _wallet.pending_account_registrations.find( account.name );
      FC_ASSERT( it != _wallet.pending_account_registrations.end() );
      for (const std::string& wif_key : it->second) {
         if( !import_key( account.name, wif_key ) )
         {
            // somebody else beat our pending registration, there is
            //    nothing we can do except log it and move on
            elog( "account ${name} registered by someone else first!",
                  ("name", account.name) );
            // might as well remove it from pending regs,
            //    because there is now no way this registration
            //    can become valid (even in the extremely rare
            //    possibility of migrating to a fork where the
            //    name is available, the user can always
            //    manually re-register)
         } else {
            import_keys = true;
         }
      }
      _wallet.pending_account_registrations.erase( it );

      if( import_keys ) {
         save_wallet_file();
      }
   }

   // after a witness registration succeeds, this saves the private key in the wallet permanently
   //
   void claim_registered_witness(const std::string& witness_name)
   {
      auto iter = _wallet.pending_witness_registrations.find(witness_name);
      FC_ASSERT(iter != _wallet.pending_witness_registrations.end());
      std::string wif_key = iter->second;

      // get the list key id this key is registered with in the chain
      fc::optional<fc::ecc::private_key> witness_private_key = wif_to_key(wif_key);
      FC_ASSERT(witness_private_key);

      auto pub_key = witness_private_key->get_public_key();
      _keys[pub_key] = wif_key;
      _wallet.pending_witness_registrations.erase(iter);
   }

   fc::mutex _resync_mutex;
   void resync()
   {
      fc::scoped_lock<fc::mutex> lock(_resync_mutex);
      // this method is used to update wallet_data annotations
      //   e.g. wallet has been restarted and was not notified
      //   of events while it was down
      //
      // everything that is done "incremental style" when a push
      //   notification is received, should also be done here
      //   "batch style" by querying the blockchain

      if( !_wallet.pending_account_registrations.empty() )
      {
         // make a vector of the account names pending registration
         std::vector<string> pending_account_names =
               boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_account_registrations));

         // look those up on the blockchain
         std::vector<fc::optional<graphene::chain::account_object >>
               pending_account_objects = _remote_db->lookup_account_names( pending_account_names );

         // if any of them exist, claim them
         for( const fc::optional<graphene::chain::account_object>& optional_account : pending_account_objects )
            if( optional_account )
               claim_registered_account(*optional_account);
      }

      if (!_wallet.pending_witness_registrations.empty())
      {
         // make a vector of the owner accounts for witnesses pending registration
         std::vector<string> pending_witness_names =
               boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_witness_registrations));

         // look up the owners on the blockchain
         std::vector<fc::optional<graphene::chain::account_object>> owner_account_objects =
               _remote_db->lookup_account_names(pending_witness_names);

         // if any of them have registered witnesses, claim them
         for( const fc::optional<graphene::chain::account_object>& optional_account : owner_account_objects )
            if (optional_account)
            {
               std::string account_id = account_id_to_string(optional_account->id);
               fc::optional<witness_object> witness_obj = _remote_db->get_witness_by_account(account_id);
               if (witness_obj)
                  claim_registered_witness(optional_account->name);
            }
      }
   }

   void enable_umask_protection()
   {
#ifdef __unix__
      _old_umask = umask( S_IRWXG | S_IRWXO );
#endif
   }

   void disable_umask_protection()
   {
#ifdef __unix__
      umask( _old_umask );
#endif
   }

   void init_prototype_ops()
   {
      operation op;
      for( int t=0; t<op.count(); t++ )
      {
         op.set_which( t );
         op.visit( op_prototype_visitor(t, _prototype_ops) );
      }
      return;
   }

   map<transaction_handle_type, signed_transaction> _builder_transactions;

   // if the user executes the same command twice in quick succession,
   // we might generate the same transaction id, and cause the second
   // transaction to be rejected.  This can be avoided by altering the
   // second transaction slightly (bumping up the expiration time by
   // a second).  Keep track of recent transaction ids we've generated
   // so we can know if we need to do this
   struct recently_generated_transaction_record
   {
      fc::time_point_sec generation_time;
      graphene::chain::transaction_id_type transaction_id;
   };
   struct timestamp_index{};
   typedef boost::multi_index_container<
         recently_generated_transaction_record,
         boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
               boost::multi_index::member<
                  recently_generated_transaction_record,
                  graphene::chain::transaction_id_type,
                  &recently_generated_transaction_record::transaction_id
               >,
               std::hash<graphene::chain::transaction_id_type>
            >,
            boost::multi_index::ordered_non_unique<
               boost::multi_index::tag<timestamp_index>,
               boost::multi_index::member<
                  recently_generated_transaction_record,
                  fc::time_point_sec,
                  &recently_generated_transaction_record::generation_time
            >
         >
       >
     > recently_generated_transaction_set_type;
   recently_generated_transaction_set_type _recently_generated_transactions;

public:
   wallet_api& self;
   wallet_api_impl( wallet_api& s, const wallet_data& initial_data, fc::api<login_api> rapi )
      : self(s),
        _chain_id(initial_data.chain_id),
        _remote_api(rapi),
        _remote_db(rapi->database()),
        _remote_net_broadcast(rapi->network_broadcast()),
        _remote_hist(rapi->history()),
        _custom_operations(rapi->custom())
   {
      chain_id_type remote_chain_id = _remote_db->get_chain_id();
      if( remote_chain_id != _chain_id )
      {
         FC_THROW( "Remote server gave us an unexpected chain_id",
            ("remote_chain_id", remote_chain_id)
            ("chain_id", _chain_id) );
      }
      init_prototype_ops();

      _remote_db->set_block_applied_callback( [this](const variant& block_id )
      {
         on_block_applied( block_id );
      } );

      _wallet.chain_id = _chain_id;
      _wallet.ws_server = initial_data.ws_server;
      _wallet.ws_user = initial_data.ws_user;
      _wallet.ws_password = initial_data.ws_password;
   }
   virtual ~wallet_api_impl()
   {
      try
      {
         _remote_db->cancel_all_subscriptions();
      }
      catch (const fc::exception& e)
      {
         // Right now the wallet_api has no way of knowing if the connection to the
         // witness has already disconnected (via the witness node exiting first).
         // If it has exited, cancel_all_subscriptsions() will throw and there's
         // nothing we can do about it.
         // dlog("Caught exception ${e} while canceling database subscriptions", ("e", e));
      }
   }

   void encrypt_keys()
   {
      if( !is_locked() )
      {
         plain_keys data;
         data.keys = _keys;
         data.checksum = _checksum;
         auto plain_txt = fc::raw::pack(data);
         _wallet.cipher_keys = fc::aes_encrypt( data.checksum, plain_txt );
      }
   }

   void on_block_applied( const variant& block_id )
   {
      fc::async([this]{resync();}, "Resync after block");
   }

   bool copy_wallet_file( string destination_filename )
   {
      fc::path src_path = get_wallet_filename();
      if( !fc::exists( src_path ) )
         return false;
      fc::path dest_path = destination_filename + _wallet_filename_extension;
      int suffix = 0;
      while( fc::exists(dest_path) )
      {
         ++suffix;
         dest_path = destination_filename + "-" + to_string( suffix ) + _wallet_filename_extension;
      }
      wlog( "backing up wallet ${src} to ${dest}",
            ("src", src_path)
            ("dest", dest_path) );

      fc::path dest_parent = fc::absolute(dest_path).parent_path();
      try
      {
         enable_umask_protection();
         if( !fc::exists( dest_parent ) )
            fc::create_directories( dest_parent );
         fc::copy( src_path, dest_path );
         disable_umask_protection();
      }
      catch(...)
      {
         disable_umask_protection();
         throw;
      }
      return true;
   }

   bool is_locked()const
   {
      return _checksum == fc::sha512();
   }

   template<typename ID>
   graphene::db::object_downcast_t<ID> get_object(ID id)const
   {
      auto ob = _remote_db->get_objects({id}, {}).front();
      return ob.template as<graphene::db::object_downcast_t<ID>>( GRAPHENE_MAX_NESTED_OBJECTS );
   }

   void set_operation_fees( signed_transaction& tx, const fee_schedule& s  )
   {
      for( auto& op : tx.operations )
         s.set_fee(op);
   }

   variant info() const
   {
      auto chain_props = get_chain_properties();
      auto global_props = get_global_properties();
      auto dynamic_props = get_dynamic_global_properties();
      fc::mutable_variant_object result;
      result["head_block_num"] = dynamic_props.head_block_number;
      result["head_block_id"] = fc::variant(dynamic_props.head_block_id, 1);
      result["head_block_age"] = fc::get_approximate_relative_time_string(dynamic_props.time,
                                                                          time_point_sec(time_point::now()),
                                                                          " old");
      result["next_maintenance_time"] =
            fc::get_approximate_relative_time_string(dynamic_props.next_maintenance_time);
      result["chain_id"] = chain_props.chain_id;
      stringstream participation;
      participation << fixed << std::setprecision(2) << (100.0*fc::popcount(dynamic_props.recent_slots_filled)) / 128.0;
      result["participation"] = participation.str();
      result["active_witnesses"] = fc::variant(global_props.active_witnesses, GRAPHENE_MAX_NESTED_OBJECTS);
      result["active_committee_members"] =
            fc::variant(global_props.active_committee_members, GRAPHENE_MAX_NESTED_OBJECTS);
      return result;
   }

   variant_object about() const
   {
      string client_version( graphene::utilities::git_revision_description );
      const size_t pos = client_version.find( '/' );
      if( pos != string::npos && client_version.size() > pos )
         client_version = client_version.substr( pos + 1 );

      fc::mutable_variant_object result;
      //result["blockchain_name"]        = BLOCKCHAIN_NAME;
      //result["blockchain_description"] = BTS_BLOCKCHAIN_DESCRIPTION;
      result["client_version"]           = client_version;
      result["graphene_revision"]        = graphene::utilities::git_revision_sha;
      result["graphene_revision_age"]    = fc::get_approximate_relative_time_string( fc::time_point_sec(
                                                 graphene::utilities::git_revision_unix_timestamp ) );
      result["fc_revision"]              = fc::git_revision_sha;
      result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec(
                                                 fc::git_revision_unix_timestamp ) );
      result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
      result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
      result["openssl_version"]          = OPENSSL_VERSION_TEXT;

      std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
      std::string os = "osx";
#elif defined(__linux__)
      std::string os = "linux";
#elif defined(_MSC_VER)
      std::string os = "win32";
#else
      std::string os = "other";
#endif
      result["build"] = os + " " + bitness;

      return result;
   }

   chain_property_object get_chain_properties() const
   {
      return _remote_db->get_chain_properties();
   }
   global_property_object get_global_properties() const
   {
      return _remote_db->get_global_properties();
   }
   dynamic_global_property_object get_dynamic_global_properties() const
   {
      return _remote_db->get_dynamic_global_properties();
   }
   std::string account_id_to_string(account_id_type id) const
   {
      std::string account_id = fc::to_string(id.space_id)
                               + "." + fc::to_string(id.type_id)
                               + "." + fc::to_string(id.instance.value);
      return account_id;
   }
   account_object get_account(account_id_type id) const
   {
      std::string account_id = account_id_to_string(id);

      auto rec = _remote_db->get_accounts({account_id}, {}).front();
      FC_ASSERT(rec);
      return *rec;
   }
   account_object get_account(string account_name_or_id) const
   {
      FC_ASSERT( account_name_or_id.size() > 0 );

      if( auto id = maybe_id<account_id_type>(account_name_or_id) )
      {
         // It's an ID
         return get_account(*id);
      } else {
         auto rec = _remote_db->lookup_account_names({account_name_or_id}).front();
         FC_ASSERT( rec && rec->name == account_name_or_id );
         return *rec;
      }
   }
   account_id_type get_account_id(string account_name_or_id) const
   {
      return get_account(account_name_or_id).get_id();
   }
   std::string asset_id_to_string(asset_id_type id) const
   {
      std::string asset_id = fc::to_string(id.space_id) +
                             "." + fc::to_string(id.type_id) +
                             "." + fc::to_string(id.instance.value);
      return asset_id;
   }
   optional<extended_asset_object> find_asset(asset_id_type id)const
   {
      auto rec = _remote_db->get_assets({asset_id_to_string(id)}, {}).front();
      return rec;
   }
   optional<extended_asset_object> find_asset(string asset_symbol_or_id)const
   {
      FC_ASSERT( asset_symbol_or_id.size() > 0 );

      if( auto id = maybe_id<asset_id_type>(asset_symbol_or_id) )
      {
         // It's an ID
         return find_asset(*id);
      } else {
         // It's a symbol
         auto rec = _remote_db->lookup_asset_symbols({asset_symbol_or_id}).front();
         if( rec )
         {
            if( rec->symbol != asset_symbol_or_id )
               return optional<asset_object>();
         }
         return rec;
      }
   }
   extended_asset_object get_asset(asset_id_type id)const
   {
      auto opt = find_asset(id);
      FC_ASSERT(opt);
      return *opt;
   }
   extended_asset_object get_asset(string asset_symbol_or_id)const
   {
      auto opt = find_asset(asset_symbol_or_id);
      FC_ASSERT(opt);
      return *opt;
   }

   fc::optional<htlc_object> get_htlc(string htlc_id) const
   {
      htlc_id_type id;
      fc::from_variant(htlc_id, id);
      auto obj = _remote_db->get_objects( { id }, {}).front();
      if ( !obj.is_null() )
      {
         return fc::optional<htlc_object>(obj.template as<htlc_object>(GRAPHENE_MAX_NESTED_OBJECTS));
      }
      return fc::optional<htlc_object>();
   }

   asset_id_type get_asset_id(string asset_symbol_or_id) const
   {
      FC_ASSERT( asset_symbol_or_id.size() > 0 );
      vector<optional<extended_asset_object>> opt_asset;
      if( std::isdigit( asset_symbol_or_id.front() ) )
         return fc::variant(asset_symbol_or_id, 1).as<asset_id_type>( 1 );
      opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol_or_id} );
      FC_ASSERT( (opt_asset.size() > 0) && (opt_asset[0].valid()) );
      return opt_asset[0]->id;
   }

   string                            get_wallet_filename() const
   {
      return _wallet_filename;
   }

   fc::ecc::private_key              get_private_key(const public_key_type& id)const
   {
      auto it = _keys.find(id);
      FC_ASSERT( it != _keys.end() );

      fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
      FC_ASSERT( privkey );
      return *privkey;
   }

   fc::ecc::private_key get_private_key_for_account(const account_object& account)const
   {
      vector<public_key_type> active_keys = account.active.get_keys();
      if (active_keys.size() != 1)
         FC_THROW("Expecting a simple authority with one active key");
      return get_private_key(active_keys.front());
   }

   // imports the private key into the wallet, and associate it in some way (?) with the
   // given account name.
   // @returns true if the key matches a current active/owner/memo key for the named
   //          account, false otherwise (but it is stored either way)
   bool import_key(string account_name_or_id, string wif_key)
   {
      fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
      if (!optional_private_key)
         FC_THROW("Invalid private key");
      graphene::chain::public_key_type wif_pub_key = optional_private_key->get_public_key();

      account_object account = get_account( account_name_or_id );

      // make a list of all current public keys for the named account
      flat_set<public_key_type> all_keys_for_account;
      std::vector<public_key_type> active_keys = account.active.get_keys();
      std::vector<public_key_type> owner_keys = account.owner.get_keys();
      std::copy(active_keys.begin(), active_keys.end(),
            std::inserter(all_keys_for_account, all_keys_for_account.end()));
      std::copy(owner_keys.begin(), owner_keys.end(),
            std::inserter(all_keys_for_account, all_keys_for_account.end()));
      all_keys_for_account.insert(account.options.memo_key);

      _keys[wif_pub_key] = wif_key;

      _wallet.update_account(account);

      _wallet.extra_keys[account.id].insert(wif_pub_key);

      return all_keys_for_account.find(wif_pub_key) != all_keys_for_account.end();
   }

   vector< signed_transaction > import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast );

   bool load_wallet_file(string wallet_filename = "")
   {
      // TODO:  Merge imported wallet with existing wallet,
      //        instead of replacing it
      if( wallet_filename == "" )
         wallet_filename = _wallet_filename;

      if( ! fc::exists( wallet_filename ) )
         return false;

      _wallet = fc::json::from_file( wallet_filename ).as< wallet_data >( 2 * GRAPHENE_MAX_NESTED_OBJECTS );
      if( _wallet.chain_id != _chain_id )
         FC_THROW( "Wallet chain ID does not match",
            ("wallet.chain_id", _wallet.chain_id)
            ("chain_id", _chain_id) );

      size_t account_pagination = 100;
      vector< std::string > account_ids_to_send;
      size_t n = _wallet.my_accounts.size();
      account_ids_to_send.reserve( std::min( account_pagination, n ) );
      auto it = _wallet.my_accounts.begin();

      for( size_t start=0; start<n; start+=account_pagination )
      {
         size_t end = std::min( start+account_pagination, n );
         assert( end > start );
         account_ids_to_send.clear();
         std::vector< account_object > old_accounts;
         for( size_t i=start; i<end; i++ )
         {
            assert( it != _wallet.my_accounts.end() );
            old_accounts.push_back( *it );
            std::string account_id = account_id_to_string(old_accounts.back().id);
            account_ids_to_send.push_back( account_id );
            ++it;
         }
         std::vector< optional< account_object > > accounts = _remote_db->get_accounts(account_ids_to_send, {});
         // server response should be same length as request
         FC_ASSERT( accounts.size() == account_ids_to_send.size() );
         size_t i = 0;
         for( const optional< account_object >& acct : accounts )
         {
            account_object& old_acct = old_accounts[i];
            if( !acct.valid() )
            {
               elog( "Could not find account ${id} : \"${name}\" does not exist on the chain!",
                     ("id", old_acct.id)("name", old_acct.name) );
               i++;
               continue;
            }
            // this check makes sure the server didn't send results
            // in a different order, or accounts we didn't request
            FC_ASSERT( acct->id == old_acct.id );
            if( fc::json::to_string(*acct) != fc::json::to_string(old_acct) )
            {
               wlog( "Account ${id} : \"${name}\" updated on chain", ("id", acct->id)("name", acct->name) );
            }
            _wallet.update_account( *acct );
            i++;
         }
      }

      return true;
   }

   /**
    * Get the required public keys to sign the transaction which had been
    * owned by us
    *
    * NOTE, if `erase_existing_sigs` set to true, the original trasaction's
    * signatures will be erased
    *
    * @param tx           The transaction to be signed
    * @param erase_existing_sigs
    *        The transaction could have been partially signed already,
    *        if set to false, the corresponding public key of existing
    *        signatures won't be returned.
    *        If set to true, the existing signatures will be erased and
    *        all required keys returned.
   */
   set<public_key_type> get_owned_required_keys( signed_transaction &tx,
                                                    bool erase_existing_sigs = true)
   {
      set<public_key_type> pks = _remote_db->get_potential_signatures( tx );
      flat_set<public_key_type> owned_keys;
      owned_keys.reserve( pks.size() );
      std::copy_if( pks.begin(), pks.end(),
                    std::inserter( owned_keys, owned_keys.end() ),
                    [this]( const public_key_type &pk ) {
                       return _keys.find( pk ) != _keys.end();
                    } );

      if ( erase_existing_sigs )
         tx.signatures.clear();

      return _remote_db->get_required_signatures( tx, owned_keys );
   }

   signed_transaction add_transaction_signature( signed_transaction tx,
                                                 bool broadcast )
   {
      set<public_key_type> approving_key_set = get_owned_required_keys(tx, false);

      if ( ( ( tx.ref_block_num == 0 && tx.ref_block_prefix == 0 ) ||
             tx.expiration == fc::time_point_sec() ) &&
           tx.signatures.empty() )
      {
         auto dyn_props = get_dynamic_global_properties();
         auto parameters = get_global_properties().parameters;
         fc::time_point_sec now = dyn_props.time;
         tx.set_reference_block( dyn_props.head_block_id );
         tx.set_expiration( now + parameters.maximum_time_until_expiration );
      }
      for ( const public_key_type &key : approving_key_set )
         tx.sign( get_private_key( key ), _chain_id );

      if ( broadcast )
      {
         try
         {
            _remote_net_broadcast->broadcast_transaction( tx );
         }
         catch ( const fc::exception &e )
         {
            elog( "Caught exception while broadcasting tx ${id}:  ${e}",
                  ( "id", tx.id().str() )( "e", e.to_detail_string() ) );
            FC_THROW( "Caught exception while broadcasting tx" );
         }
      }

      return tx;
   }

   void quit()
   {
      ilog( "Quitting Cli Wallet ..." );

      throw fc::canceled_exception();
   }

   void save_wallet_file(string wallet_filename = "")
   {
      //
      // Serialize in memory, then save to disk
      //
      // This approach lessens the risk of a partially written wallet
      // if exceptions are thrown in serialization
      //

      encrypt_keys();

      if( wallet_filename == "" )
         wallet_filename = _wallet_filename;

      wlog( "saving wallet to file ${fn}", ("fn", wallet_filename) );

      string data = fc::json::to_pretty_string( _wallet );

      try
      {
         enable_umask_protection();
         //
         // Parentheses on the following declaration fails to compile,
         // due to the Most Vexing Parse.  Thanks, C++
         //
         // http://en.wikipedia.org/wiki/Most_vexing_parse
         //
         std::string tmp_wallet_filename = wallet_filename + ".tmp";
         fc::ofstream outfile{ fc::path( tmp_wallet_filename ) };
         outfile.write( data.c_str(), data.length() );
         outfile.flush();
         outfile.close();

         wlog( "saved successfully wallet to tmp file ${fn}", ("fn", tmp_wallet_filename) );

         std::string wallet_file_content;
         fc::read_file_contents(tmp_wallet_filename, wallet_file_content);

         if (wallet_file_content == data) {
            wlog( "validated successfully tmp wallet file ${fn}", ("fn", tmp_wallet_filename) );

            fc::rename( tmp_wallet_filename, wallet_filename );

            wlog( "renamed successfully tmp wallet file ${fn}", ("fn", tmp_wallet_filename) );
         }
         else
         {
            FC_THROW("tmp wallet file cannot be validated ${fn}", ("fn", tmp_wallet_filename) );
         }

         wlog( "successfully saved wallet to file ${fn}", ("fn", wallet_filename) );

         disable_umask_protection();
      }
      catch(...)
      {
         string ws_password = _wallet.ws_password;
         _wallet.ws_password = "";
         wlog("wallet file content is next: ${data}", ("data", fc::json::to_pretty_string( _wallet ) ) );
         _wallet.ws_password = ws_password;

         disable_umask_protection();
         throw;
      }
   }

   transaction_handle_type begin_builder_transaction()
   {
      int trx_handle = _builder_transactions.empty()? 0
                                                    : (--_builder_transactions.end())->first + 1;
      _builder_transactions[trx_handle];
      return trx_handle;
   }
   void add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op)
   {
      FC_ASSERT(_builder_transactions.count(transaction_handle));
      _builder_transactions[transaction_handle].operations.emplace_back(op);
   }
   void replace_operation_in_builder_transaction(transaction_handle_type handle,
                                                 uint32_t operation_index,
                                                 const operation& new_op)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      signed_transaction& trx = _builder_transactions[handle];
      FC_ASSERT( operation_index < trx.operations.size());
      trx.operations[operation_index] = new_op;
   }
   asset set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset = GRAPHENE_SYMBOL)
   {
      FC_ASSERT(_builder_transactions.count(handle));

      auto fee_asset_obj = get_asset(fee_asset);
      asset total_fee = fee_asset_obj.amount(0);

      auto gprops = _remote_db->get_global_properties().parameters;
      if( fee_asset_obj.get_id() != asset_id_type() )
      {
         for( auto& op : _builder_transactions[handle].operations )
            total_fee += gprops.get_current_fees().set_fee( op, fee_asset_obj.options.core_exchange_rate );

         FC_ASSERT((total_fee * fee_asset_obj.options.core_exchange_rate).amount <=
                   get_object(fee_asset_obj.dynamic_asset_data_id).fee_pool,
                   "Cannot pay fees in ${asset}, as this asset's fee pool is insufficiently funded.",
                   ("asset", fee_asset_obj.symbol));
      } else {
         for( auto& op : _builder_transactions[handle].operations )
            total_fee += gprops.get_current_fees().set_fee( op );
      }

      return total_fee;
   }
   transaction preview_builder_transaction(transaction_handle_type handle)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      return _builder_transactions[handle];
   }
   signed_transaction sign_builder_transaction(transaction_handle_type transaction_handle, bool broadcast = true)
   {
      FC_ASSERT(_builder_transactions.count(transaction_handle));

      return _builder_transactions[transaction_handle] =
            sign_transaction(_builder_transactions[transaction_handle], broadcast);
   }

   pair<transaction_id_type,signed_transaction> broadcast_transaction(signed_transaction tx)
   {
       try {
           _remote_net_broadcast->broadcast_transaction(tx);
       }
       catch (const fc::exception& e) {
           elog("Caught exception while broadcasting tx ${id}:  ${e}",
                ("id", tx.id().str())("e", e.to_detail_string()));
           throw;
       }
       return std::make_pair(tx.id(),tx);
   }

   signed_transaction propose_builder_transaction(
      transaction_handle_type handle,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      proposal_create_operation op;
      op.expiration_time = expiration;
      signed_transaction& trx = _builder_transactions[handle];
      std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                     [](const operation& op) -> op_wrapper { return op; });
      if( review_period_seconds )
         op.review_period_seconds = review_period_seconds;
      trx.operations = {op};
      _remote_db->get_global_properties().parameters.get_current_fees().set_fee( trx.operations.front() );

      return trx = sign_transaction(trx, broadcast);
   }

   signed_transaction propose_builder_transaction2(
      transaction_handle_type handle,
      string account_name_or_id,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      proposal_create_operation op;
      op.fee_paying_account = get_account(account_name_or_id).get_id();
      op.expiration_time = expiration;
      signed_transaction& trx = _builder_transactions[handle];
      std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                     [](const operation& op) -> op_wrapper { return op; });
      if( review_period_seconds )
         op.review_period_seconds = review_period_seconds;
      trx.operations = {op};
      _remote_db->get_global_properties().parameters.get_current_fees().set_fee( trx.operations.front() );

      return trx = sign_transaction(trx, broadcast);
   }

   void remove_builder_transaction(transaction_handle_type handle)
   {
      _builder_transactions.erase(handle);
   }

   signed_transaction register_account(string name, public_key_type owner, public_key_type active,
         string  registrar_account, string  referrer_account, uint32_t referrer_percent,
         bool broadcast = false);
   

   signed_transaction upgrade_account(string name, bool broadcast);


   // This function generates derived keys starting with index 0 and keeps incrementing
   // the index until it finds a key that isn't registered in the block chain.  To be
   // safer, it continues checking for a few more keys to make sure there wasn't a short gap
   // caused by a failed registration or the like.
   int find_first_unused_derived_key_index(const fc::ecc::private_key& parent_key)
   {
      int first_unused_index = 0;
      int number_of_consecutive_unused_keys = 0;
      for (int key_index = 0; ; ++key_index)
      {
         fc::ecc::private_key derived_private_key = derive_private_key(key_to_wif(parent_key), key_index);
         graphene::chain::public_key_type derived_public_key = derived_private_key.get_public_key();
         if( _keys.find(derived_public_key) == _keys.end() )
         {
            if (number_of_consecutive_unused_keys)
            {
               ++number_of_consecutive_unused_keys;
               if (number_of_consecutive_unused_keys > 5)
                  return first_unused_index;
            }
            else
            {
               first_unused_index = key_index;
               number_of_consecutive_unused_keys = 1;
            }
         }
         else
         {
            // key_index is used
            first_unused_index = 0;
            number_of_consecutive_unused_keys = 0;
         }
      }
   }

   signed_transaction create_account_with_private_key(fc::ecc::private_key owner_privkey,
                                                      string account_name,
                                                      string registrar_account,
                                                      string referrer_account,
                                                      bool broadcast = false,
                                                      bool save_wallet = true)
   { try {
         int active_key_index = find_first_unused_derived_key_index(owner_privkey);
         fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(owner_privkey), active_key_index);

         int memo_key_index = find_first_unused_derived_key_index(active_privkey);
         fc::ecc::private_key memo_privkey = derive_private_key( key_to_wif(active_privkey), memo_key_index);

         graphene::chain::public_key_type owner_pubkey = owner_privkey.get_public_key();
         graphene::chain::public_key_type active_pubkey = active_privkey.get_public_key();
         graphene::chain::public_key_type memo_pubkey = memo_privkey.get_public_key();

         account_create_operation account_create_op;

         // TODO:  process when pay_from_account is ID

         account_object registrar_account_object = get_account( registrar_account );

         account_id_type registrar_account_id = registrar_account_object.id;

         account_object referrer_account_object = get_account( referrer_account );
         account_create_op.referrer = referrer_account_object.id;
         account_create_op.referrer_percent = referrer_account_object.referrer_rewards_percentage;

         account_create_op.registrar = registrar_account_id;
         account_create_op.name = account_name;
         account_create_op.owner = authority(1, owner_pubkey, 1);
         account_create_op.active = authority(1, active_pubkey, 1);
         account_create_op.options.memo_key = memo_pubkey;

         // current_fee_schedule()
         // find_account(pay_from_account)

         // account_create_op.fee = account_create_op.calculate_fee(db.current_fee_schedule());

         signed_transaction tx;
         tx.operations.push_back( account_create_op );
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         // we do not insert owner_privkey here because
         //    it is intended to only be used for key recovery
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( active_privkey ));
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( memo_privkey ));
         if( save_wallet )
            save_wallet_file();
         return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account)(broadcast) ) }

   signed_transaction create_account_with_brain_key(string brain_key, string account_name, string registrar_account,
         string referrer_account, bool broadcast = false, bool save_wallet = true);

   signed_transaction create_asset(string issuer,
                                   string symbol,
                                   uint8_t precision,
                                   asset_options common,
                                   fc::optional<bitasset_options> bitasset_opts,
                                   bool broadcast = false)
   { try {
      account_object issuer_account = get_account( issuer );
      FC_ASSERT(!find_asset(symbol).valid(), "Asset with that symbol already exists!");

      asset_create_operation create_op;
      create_op.issuer = issuer_account.id;
      create_op.symbol = symbol;
      create_op.precision = precision;
      create_op.common_options = common;
      create_op.bitasset_opts = bitasset_opts;

      signed_transaction tx;
      tx.operations.push_back( create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (issuer)(symbol)(precision)(common)(bitasset_opts)(broadcast) ) }

   signed_transaction update_asset(string symbol,
                                   optional<string> new_issuer,
                                   asset_options new_options,
                                   bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
         FC_THROW("No asset with that symbol exists!");
      optional<account_id_type> new_issuer_account_id;
      if (new_issuer)
      {
         FC_THROW( "The use of 'new_issuer' is no longer supported. Please use `update_asset_issuer' instead!" );
      }

      asset_update_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_issuer = new_issuer_account_id;
      update_op.new_options = new_options;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_issuer)(new_options)(broadcast) ) }

   signed_transaction update_asset_issuer(string symbol,
                                   string new_issuer,
                                   bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      account_object new_issuer_account = get_account(new_issuer);

      asset_update_issuer_operation update_issuer;
      update_issuer.issuer = asset_to_update->issuer;
      update_issuer.asset_to_update = asset_to_update->id;
      update_issuer.new_issuer = new_issuer_account.id;

      signed_transaction tx;
      tx.operations.push_back( update_issuer );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_issuer)(broadcast) ) }

   signed_transaction update_bitasset(string symbol,
                                      bitasset_options new_options,
                                      bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_update_bitasset_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_options = new_options;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_options)(broadcast) ) }

   signed_transaction update_asset_feed_producers(string symbol,
                                                  flat_set<string> new_feed_producers,
                                                  bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_update_feed_producers_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_feed_producers.reserve(new_feed_producers.size());
      std::transform(new_feed_producers.begin(), new_feed_producers.end(),
                     std::inserter(update_op.new_feed_producers, update_op.new_feed_producers.end()),
                     [this](const std::string& account_name_or_id){ return get_account_id(account_name_or_id); });

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_feed_producers)(broadcast) ) }

   signed_transaction publish_asset_feed(string publishing_account,
                                         string symbol,
                                         price_feed feed,
                                         bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_publish_feed_operation publish_op;
      publish_op.publisher = get_account_id(publishing_account);
      publish_op.asset_id = asset_to_update->id;
      publish_op.feed = feed;

      signed_transaction tx;
      tx.operations.push_back( publish_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (publishing_account)(symbol)(feed)(broadcast) ) }

   signed_transaction fund_asset_fee_pool(string from,
                                          string symbol,
                                          string amount,
                                          bool broadcast /* = false */)
   { try {
      account_object from_account = get_account(from);
      optional<asset_object> asset_to_fund = find_asset(symbol);
      if (!asset_to_fund)
        FC_THROW("No asset with that symbol exists!");
      asset_object core_asset = get_asset(asset_id_type());

      asset_fund_fee_pool_operation fund_op;
      fund_op.from_account = from_account.id;
      fund_op.asset_id = asset_to_fund->id;
      fund_op.amount = core_asset.amount_from_string(amount).amount;

      signed_transaction tx;
      tx.operations.push_back( fund_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (from)(symbol)(amount)(broadcast) ) }

   signed_transaction claim_asset_fee_pool(string symbol,
                                           string amount,
                                           bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_pool_to_claim = find_asset(symbol);
      if (!asset_pool_to_claim)
        FC_THROW("No asset with that symbol exists!");
      asset_object core_asset = get_asset(asset_id_type());

      asset_claim_pool_operation claim_op;
      claim_op.issuer = asset_pool_to_claim->issuer;
      claim_op.asset_id = asset_pool_to_claim->id;
      claim_op.amount_to_claim = core_asset.amount_from_string(amount).amount;

      signed_transaction tx;
      tx.operations.push_back( claim_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(amount)(broadcast) ) }


   signed_transaction reserve_asset(string from,
                                 string amount,
                                 string symbol,
                                 bool broadcast /* = false */)
   { try {
      account_object from_account = get_account(from);
      optional<asset_object> asset_to_reserve = find_asset(symbol);
      if (!asset_to_reserve)
        FC_THROW("No asset with that symbol exists!");

      asset_reserve_operation reserve_op;
      reserve_op.payer = from_account.id;
      reserve_op.amount_to_reserve = asset_to_reserve->amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( reserve_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (from)(amount)(symbol)(broadcast) ) }

   signed_transaction global_settle_asset(string symbol,
                                          price settle_price,
                                          bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_settle = find_asset(symbol);
      if (!asset_to_settle)
        FC_THROW("No asset with that symbol exists!");

      asset_global_settle_operation settle_op;
      settle_op.issuer = asset_to_settle->issuer;
      settle_op.asset_to_settle = asset_to_settle->id;
      settle_op.settle_price = settle_price;

      signed_transaction tx;
      tx.operations.push_back( settle_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(settle_price)(broadcast) ) }

   signed_transaction settle_asset(string account_to_settle,
                                   string amount_to_settle,
                                   string symbol,
                                   bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_settle = find_asset(symbol);
      if (!asset_to_settle)
        FC_THROW("No asset with that symbol exists!");

      asset_settle_operation settle_op;
      settle_op.account = get_account_id(account_to_settle);
      settle_op.amount = asset_to_settle->amount_from_string(amount_to_settle);

      signed_transaction tx;
      tx.operations.push_back( settle_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_settle)(amount_to_settle)(symbol)(broadcast) ) }

   signed_transaction bid_collateral(string bidder_name,
                                     string debt_amount, string debt_symbol,
                                     string additional_collateral,
                                     bool broadcast )
   { try {
      optional<asset_object> debt_asset = find_asset(debt_symbol);
      if (!debt_asset)
        FC_THROW("No asset with that symbol exists!");

      FC_ASSERT(debt_asset->bitasset_data_id.valid(), "Not a bitasset, bidding not possible.");
      const asset_object& collateral =
            get_asset(get_object(*debt_asset->bitasset_data_id).options.short_backing_asset);

      bid_collateral_operation op;
      op.bidder = get_account_id(bidder_name);
      op.debt_covered = debt_asset->amount_from_string(debt_amount);
      op.additional_collateral = collateral.amount_from_string(additional_collateral);

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (bidder_name)(debt_amount)(debt_symbol)(additional_collateral)(broadcast) ) }

   signed_transaction whitelist_account(string authorizing_account,
                                        string account_to_list,
                                        account_whitelist_operation::account_listing new_listing_status,
                                        bool broadcast /* = false */)
   { try {
      account_whitelist_operation whitelist_op;
      whitelist_op.authorizing_account = get_account_id(authorizing_account);
      whitelist_op.account_to_list = get_account_id(account_to_list);
      whitelist_op.new_listing = new_listing_status;

      signed_transaction tx;
      tx.operations.push_back( whitelist_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (authorizing_account)(account_to_list)(new_listing_status)(broadcast) ) }

   signed_transaction create_committee_member(string owner_account, string url,
                                      bool broadcast /* = false */)
   { try {

      committee_member_create_operation committee_member_create_op;
      committee_member_create_op.committee_member_account = get_account_id(owner_account);
      committee_member_create_op.url = url;
      if (_remote_db->get_committee_member_by_account(owner_account))
         FC_THROW("Account ${owner_account} is already a committee_member", ("owner_account", owner_account));

      signed_transaction tx;
      tx.operations.push_back( committee_member_create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (owner_account)(broadcast) ) }

   witness_object get_witness(string owner_account)
   {
      try
      {
         fc::optional<witness_id_type> witness_id = maybe_id<witness_id_type>(owner_account);
         if (witness_id)
         {
            std::vector<witness_id_type> ids_to_get;
            ids_to_get.push_back(*witness_id);
            std::vector<fc::optional<witness_object>> witness_objects = _remote_db->get_witnesses(ids_to_get);
            if (witness_objects.front())
               return *witness_objects.front();
            FC_THROW("No witness is registered for id ${id}", ("id", owner_account));
         }
         else
         {
            // then maybe it's the owner account
            try
            {
               std::string owner_account_id = account_id_to_string(get_account_id(owner_account));
               fc::optional<witness_object> witness = _remote_db->get_witness_by_account(owner_account_id);
               if (witness)
                  return *witness;
               else
                  FC_THROW("No witness is registered for account ${account}", ("account", owner_account));
            }
            catch (const fc::exception&)
            {
               FC_THROW("No account or witness named ${account}", ("account", owner_account));
            }
         }
      }
      FC_CAPTURE_AND_RETHROW( (owner_account) )
   }

   committee_member_object get_committee_member(string owner_account)
   {
      try
      {
         fc::optional<committee_member_id_type> committee_member_id =
               maybe_id<committee_member_id_type>(owner_account);
         if (committee_member_id)
         {
            std::vector<committee_member_id_type> ids_to_get;
            ids_to_get.push_back(*committee_member_id);
            std::vector<fc::optional<committee_member_object>> committee_member_objects =
                  _remote_db->get_committee_members(ids_to_get);
            if (committee_member_objects.front())
               return *committee_member_objects.front();
            FC_THROW("No committee_member is registered for id ${id}", ("id", owner_account));
         }
         else
         {
            // then maybe it's the owner account
            try
            {
               fc::optional<committee_member_object> committee_member =
                     _remote_db->get_committee_member_by_account(owner_account);
               if (committee_member)
                  return *committee_member;
               else
                  FC_THROW("No committee_member is registered for account ${account}", ("account", owner_account));
            }
            catch (const fc::exception&)
            {
               FC_THROW("No account or committee_member named ${account}", ("account", owner_account));
            }
         }
      }
      FC_CAPTURE_AND_RETHROW( (owner_account) )
   }

   signed_transaction create_witness(string owner_account,
                                     string url,
                                     bool broadcast /* = false */)
   { try {
      account_object witness_account = get_account(owner_account);
      fc::ecc::private_key active_private_key = get_private_key_for_account(witness_account);
      int witness_key_index = find_first_unused_derived_key_index(active_private_key);
      fc::ecc::private_key witness_private_key =
            derive_private_key(key_to_wif(active_private_key), witness_key_index);
      graphene::chain::public_key_type witness_public_key = witness_private_key.get_public_key();

      witness_create_operation witness_create_op;
      witness_create_op.witness_account = witness_account.id;
      witness_create_op.block_signing_key = witness_public_key;
      witness_create_op.url = url;

      if (_remote_db->get_witness_by_account(account_id_to_string(witness_create_op.witness_account)))
         FC_THROW("Account ${owner_account} is already a witness", ("owner_account", owner_account));

      signed_transaction tx;
      tx.operations.push_back( witness_create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      _wallet.pending_witness_registrations[owner_account] = key_to_wif(witness_private_key);

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (owner_account)(broadcast) ) }

   signed_transaction update_witness(string witness_name,
                                     string url,
                                     string block_signing_key,
                                     bool broadcast /* = false */)
   { try {
      witness_object witness = get_witness(witness_name);
      account_object witness_account = get_account( witness.witness_account );

      witness_update_operation witness_update_op;
      witness_update_op.witness = witness.id;
      witness_update_op.witness_account = witness_account.id;
      if( url != "" )
         witness_update_op.new_url = url;
      if( block_signing_key != "" )
         witness_update_op.new_signing_key = public_key_type( block_signing_key );

      signed_transaction tx;
      tx.operations.push_back( witness_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (witness_name)(url)(block_signing_key)(broadcast) ) }

   template<typename WorkerInit>
   static WorkerInit _create_worker_initializer( const variant& worker_settings )
   {
      WorkerInit result;
      from_variant( worker_settings, result, GRAPHENE_MAX_NESTED_OBJECTS );
      return result;
   }

   signed_transaction create_worker(
      string owner_account,
      time_point_sec work_begin_date,
      time_point_sec work_end_date,
      share_type daily_pay,
      string name,
      string url,
      variant worker_settings,
      bool broadcast
      )
   {
      worker_initializer init;
      std::string wtype = worker_settings["type"].get_string();

      // TODO:  Use introspection to do this dispatch
      if( wtype == "burn" )
         init = _create_worker_initializer< burn_worker_initializer >( worker_settings );
      else if( wtype == "refund" )
         init = _create_worker_initializer< refund_worker_initializer >( worker_settings );
      else if( wtype == "vesting" )
         init = _create_worker_initializer< vesting_balance_worker_initializer >( worker_settings );
      else
      {
         FC_ASSERT( false, "unknown worker[\"type\"] value" );
      }

      worker_create_operation op;
      op.owner = get_account( owner_account ).id;
      op.work_begin_date = work_begin_date;
      op.work_end_date = work_end_date;
      op.daily_pay = daily_pay;
      op.name = name;
      op.url = url;
      op.initializer = init;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction update_worker_votes( string account, worker_vote_delta delta, bool broadcast );

   static htlc_hash do_hash( const string& algorithm, const std::string& hash )
   {
      string name_upper;
      std::transform( algorithm.begin(), algorithm.end(), std::back_inserter(name_upper), ::toupper);
      if( name_upper == "RIPEMD160" )
         return fc::ripemd160( hash );
      if( name_upper == "SHA256" )
         return fc::sha256( hash );
      if( name_upper == "SHA1" )
         return fc::sha1( hash );
      FC_THROW_EXCEPTION( fc::invalid_arg_exception, "Unknown algorithm '${a}'", ("a",algorithm) );
   }

   signed_transaction htlc_create( string source, string destination, string amount, string asset_symbol,
         string hash_algorithm, const std::string& preimage_hash, uint32_t preimage_size,
         const uint32_t claim_period_seconds, bool broadcast = false );

   signed_transaction htlc_redeem( string htlc_id, string issuer, const std::vector<char>& preimage, bool broadcast );

   signed_transaction htlc_extend ( string htlc_id, string issuer, const uint32_t seconds_to_add, bool broadcast);

   signed_transaction account_store_map(string account, string catalog, bool remove,
         flat_map<string, optional<string>> key_values, bool broadcast);

   vector< vesting_balance_object_with_info > get_vesting_balances( string account_name )
   { try {
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>( account_name );
      std::vector<vesting_balance_object_with_info> result;
      fc::time_point_sec now = _remote_db->get_dynamic_global_properties().time;

      if( vbid )
      {
         result.emplace_back( get_object(*vbid), now );
         return result;
      }

      vector< vesting_balance_object > vbos = _remote_db->get_vesting_balances( account_name );
      if( vbos.size() == 0 )
         return result;

      for( const vesting_balance_object& vbo : vbos )
         result.emplace_back( vbo, now );

      return result;
   } FC_CAPTURE_AND_RETHROW( (account_name) )
   }

   signed_transaction withdraw_vesting(
      string witness_name,
      string amount,
      string asset_symbol,
      bool broadcast = false )
   { try {
      asset_object asset_obj = get_asset( asset_symbol );
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>(witness_name);
      if( !vbid )
      {
         witness_object wit = get_witness( witness_name );
         FC_ASSERT( wit.pay_vb );
         vbid = wit.pay_vb;
      }

      vesting_balance_object vbo = get_object( *vbid );
      vesting_balance_withdraw_operation vesting_balance_withdraw_op;

      vesting_balance_withdraw_op.vesting_balance = *vbid;
      vesting_balance_withdraw_op.owner = vbo.owner;
      vesting_balance_withdraw_op.amount = asset_obj.amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( vesting_balance_withdraw_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (witness_name)(amount) )
   }

   signed_transaction vote_for_committee_member(string voting_account,
                                        string committee_member,
                                        bool approve,
                                        bool broadcast /* = false */)
   { try {
      account_object voting_account_object = get_account(voting_account);
      fc::optional<committee_member_object> committee_member_obj =
            _remote_db->get_committee_member_by_account(committee_member);
      if (!committee_member_obj)
         FC_THROW("Account ${committee_member} is not registered as a committee_member",
                  ("committee_member", committee_member));
      if (approve)
      {
         auto insert_result = voting_account_object.options.votes.insert(committee_member_obj->vote_id);
         if (!insert_result.second)
            FC_THROW("Account ${account} was already voting for committee_member ${committee_member}",
                     ("account", voting_account)("committee_member", committee_member));
      }
      else
      {
         unsigned votes_removed = voting_account_object.options.votes.erase(committee_member_obj->vote_id);
         if (!votes_removed)
            FC_THROW("Account ${account} is already not voting for committee_member ${committee_member}",
                     ("account", voting_account)("committee_member", committee_member));
      }
      account_update_operation account_update_op;
      account_update_op.account = voting_account_object.id;
      account_update_op.new_options = voting_account_object.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (voting_account)(committee_member)(approve)(broadcast) ) }

   signed_transaction vote_for_witness(string voting_account,
                                        string witness,
                                        bool approve,
                                        bool broadcast /* = false */)
   { try {
      account_object voting_account_object = get_account(voting_account);

      fc::optional<witness_object> witness_obj = _remote_db->get_witness_by_account(witness);
      if (!witness_obj)
         FC_THROW("Account ${witness} is not registered as a witness", ("witness", witness));
      if (approve)
      {
         auto insert_result = voting_account_object.options.votes.insert(witness_obj->vote_id);
         if (!insert_result.second)
            FC_THROW("Account ${account} was already voting for witness ${witness}",
                     ("account", voting_account)("witness", witness));
      }
      else
      {
         unsigned votes_removed = voting_account_object.options.votes.erase(witness_obj->vote_id);
         if (!votes_removed)
            FC_THROW("Account ${account} is already not voting for witness ${witness}",
                     ("account", voting_account)("witness", witness));
      }
      account_update_operation account_update_op;
      account_update_op.account = voting_account_object.id;
      account_update_op.new_options = voting_account_object.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (voting_account)(witness)(approve)(broadcast) ) }

   signed_transaction set_voting_proxy(string account_to_modify,
                                       optional<string> voting_account,
                                       bool broadcast /* = false */)
   { try {
      account_object account_object_to_modify = get_account(account_to_modify);
      if (voting_account)
      {
         account_id_type new_voting_account_id = get_account_id(*voting_account);
         if (account_object_to_modify.options.voting_account == new_voting_account_id)
            FC_THROW("Voting proxy for ${account} is already set to ${voter}",
                     ("account", account_to_modify)("voter", *voting_account));
         account_object_to_modify.options.voting_account = new_voting_account_id;
      }
      else
      {
         if (account_object_to_modify.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT)
            FC_THROW("Account ${account} is already voting for itself", ("account", account_to_modify));
         account_object_to_modify.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
      }

      account_update_operation account_update_op;
      account_update_op.account = account_object_to_modify.id;
      account_update_op.new_options = account_object_to_modify.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_modify)(voting_account)(broadcast) ) }

   signed_transaction set_desired_witness_and_committee_member_count(string account_to_modify,
                                                             uint16_t desired_number_of_witnesses,
                                                             uint16_t desired_number_of_committee_members,
                                                             bool broadcast /* = false */)
   { try {
      account_object account_object_to_modify = get_account(account_to_modify);

      if (account_object_to_modify.options.num_witness == desired_number_of_witnesses &&
          account_object_to_modify.options.num_committee == desired_number_of_committee_members)
         FC_THROW("Account ${account} is already voting for ${witnesses} witnesses"
                  " and ${committee_members} committee_members",
                  ("account", account_to_modify)("witnesses", desired_number_of_witnesses)
                  ("committee_members",desired_number_of_witnesses));
      account_object_to_modify.options.num_witness = desired_number_of_witnesses;
      account_object_to_modify.options.num_committee = desired_number_of_committee_members;

      account_update_operation account_update_op;
      account_update_op.account = account_object_to_modify.id;
      account_update_op.new_options = account_object_to_modify.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_modify)(desired_number_of_witnesses)
                             (desired_number_of_committee_members)(broadcast) ) }

   signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false)
   {

      set<public_key_type> approving_key_set = get_owned_required_keys(tx);

      auto dyn_props = get_dynamic_global_properties();
      tx.set_reference_block( dyn_props.head_block_id );

      // first, some bookkeeping, expire old items from _recently_generated_transactions
      // since transactions include the head block id, we just need the index for keeping transactions unique
      // when there are multiple transactions in the same block.  choose a time period that should be at
      // least one block long, even in the worst case.  2 minutes ought to be plenty.
      fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
      auto oldest_transaction_record_iter =
            _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
      auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
      _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);

      uint32_t expiration_time_offset = 0;
      for (;;)
      {
         tx.set_expiration( dyn_props.time + fc::seconds(30 + expiration_time_offset) );
         tx.clear_signatures();

         for( const public_key_type& key : approving_key_set )
            tx.sign( get_private_key(key), _chain_id );

         graphene::chain::transaction_id_type this_transaction_id = tx.id();
         auto iter = _recently_generated_transactions.find(this_transaction_id);
         if (iter == _recently_generated_transactions.end())
         {
            // we haven't generated this transaction before, the usual case
            recently_generated_transaction_record this_transaction_record;
            this_transaction_record.generation_time = dyn_props.time;
            this_transaction_record.transaction_id = this_transaction_id;
            _recently_generated_transactions.insert(this_transaction_record);
            break;
         }

         // else we've generated a dupe, increment expiration time and re-sign it
         ++expiration_time_offset;
      }

      if( broadcast )
      {
         try
         {
            _remote_net_broadcast->broadcast_transaction( tx );
         }
         catch (const fc::exception& e)
         {
            elog("Caught exception while broadcasting tx ${id}:  ${e}",
                 ("id", tx.id().str())("e", e.to_detail_string()) );
            throw;
         }
      }

      return tx;
   }

   flat_set<public_key_type> get_transaction_signers(const signed_transaction &tx) const
   {
      return tx.get_signature_keys(_chain_id);
   }

   vector<flat_set<account_id_type>> get_key_references(const vector<public_key_type> &keys) const
   {
       return _remote_db->get_key_references(keys);
   }

   memo_data sign_memo(string from, string to, string memo);

   string read_memo(const memo_data& md);

   signed_message sign_message(string signer, string message);

   bool verify_message( const string& message, const string& account, int block, const string& time,
         const compact_signature& sig );

   bool verify_signed_message( const signed_message& message );

   bool verify_encapsulated_message( const string& message );

   signed_transaction sell_asset(string seller_account,
                                 string amount_to_sell,
                                 string symbol_to_sell,
                                 string min_to_receive,
                                 string symbol_to_receive,
                                 uint32_t timeout_sec = 0,
                                 bool   fill_or_kill = false,
                                 bool   broadcast = false)
   {
      account_object seller   = get_account( seller_account );

      limit_order_create_operation op;
      op.seller = seller.id;
      op.amount_to_sell = get_asset(symbol_to_sell).amount_from_string(amount_to_sell);
      op.min_to_receive = get_asset(symbol_to_receive).amount_from_string(min_to_receive);
      if( timeout_sec )
         op.expiration = fc::time_point::now() + fc::seconds(timeout_sec);
      op.fill_or_kill = fill_or_kill;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction borrow_asset(string seller_name, string amount_to_borrow, string asset_symbol,
                                       string amount_of_collateral, bool broadcast = false)
   {
      account_object seller = get_account(seller_name);
      asset_object mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      asset_object collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.get_current_fees());
      trx.validate();

      return sign_transaction(trx, broadcast);
   }

   signed_transaction borrow_asset_ext( string seller_name, string amount_to_borrow, string asset_symbol,
                                        string amount_of_collateral,
                                        call_order_update_operation::extensions_type extensions,
                                        bool broadcast = false)
   {
      account_object seller = get_account(seller_name);
      asset_object mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      asset_object collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);
      op.extensions = extensions;

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.get_current_fees());
      trx.validate();

      return sign_transaction(trx, broadcast);
   }

   signed_transaction cancel_order(limit_order_id_type order_id, bool broadcast = false)
   { try {
         FC_ASSERT(!is_locked());
         signed_transaction trx;

         limit_order_cancel_operation op;
         op.fee_paying_account = get_object(order_id).seller;
         op.order = order_id;
         trx.operations = {op};
         set_operation_fees( trx, _remote_db->get_global_properties().parameters.get_current_fees());

         trx.validate();
         return sign_transaction(trx, broadcast);
   } FC_CAPTURE_AND_RETHROW((order_id)) }

   signed_transaction transfer(string from, string to, string amount,
         string asset_symbol, string memo, bool broadcast = false);

   signed_transaction issue_asset(string to_account, string amount, string symbol,
                                  string memo, bool broadcast = false)
   {
      auto asset_obj = get_asset(symbol);

      account_object to = get_account(to_account);
      account_object issuer = get_account(asset_obj.issuer);

      asset_issue_operation issue_op;
      issue_op.issuer           = asset_obj.issuer;
      issue_op.asset_to_issue   = asset_obj.amount_from_string(amount);
      issue_op.issue_to_account = to.id;

      if( memo.size() )
      {
         issue_op.memo = memo_data();
         issue_op.memo->from = issuer.options.memo_key;
         issue_op.memo->to = to.options.memo_key;
         issue_op.memo->set_message(get_private_key(issuer.options.memo_key),
                                    to.options.memo_key, memo);
      }

      signed_transaction tx;
      tx.operations.push_back(issue_op);
      set_operation_fees(tx,_remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const;

   signed_transaction propose_parameter_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_values,
      bool broadcast = false)
   {
      FC_ASSERT( !changed_values.contains("current_fees") );

      const chain_parameters& current_params = get_global_properties().parameters;
      chain_parameters new_params = current_params;
      fc::reflector<chain_parameters>::visit(
         fc::from_variant_visitor<chain_parameters>( changed_values, new_params, GRAPHENE_MAX_NESTED_OBJECTS )
         );

      committee_member_update_global_parameters_operation update_op;
      update_op.new_parameters = new_params;

      proposal_create_operation prop_op;

      prop_op.expiration_time = expiration_time;
      prop_op.review_period_seconds = current_params.committee_proposal_review_period;
      prop_op.fee_paying_account = get_account(proposing_account).id;

      prop_op.proposed_ops.emplace_back( update_op );
      current_params.get_current_fees().set_fee( prop_op.proposed_ops.back().op );

      signed_transaction tx;
      tx.operations.push_back(prop_op);
      set_operation_fees(tx, current_params.get_current_fees());
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   signed_transaction propose_fee_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_fees,
      bool broadcast = false)
   {
      const chain_parameters& current_params = get_global_properties().parameters;
      const fee_schedule_type& current_fees = current_params.get_current_fees();

      flat_map< int, fee_parameters > fee_map;
      fee_map.reserve( current_fees.parameters.size() );
      for( const fee_parameters& op_fee : current_fees.parameters )
         fee_map[ op_fee.which() ] = op_fee;
      uint32_t scale = current_fees.scale;

      for( const auto& item : changed_fees )
      {
         const string& key = item.key();
         if( key == "scale" )
         {
            int64_t _scale = item.value().as_int64();
            FC_ASSERT( _scale >= 0 );
            FC_ASSERT( _scale <= std::numeric_limits<uint32_t>::max() );
            scale = uint32_t( _scale );
            continue;
         }
         // is key a number?
         auto is_numeric = [&key]() -> bool
         {
            size_t n = key.size();
            for( size_t i=0; i<n; i++ )
            {
               if( !isdigit( key[i] ) )
                  return false;
            }
            return true;
         };

         int which;
         if( is_numeric() )
            which = std::stoi( key );
         else
         {
            const auto& n2w = _operation_which_map.name_to_which;
            auto it = n2w.find( key );
            FC_ASSERT( it != n2w.end(), "unknown operation" );
            which = it->second;
         }

         fee_parameters fp = from_which_variant< fee_parameters >( which, item.value(), GRAPHENE_MAX_NESTED_OBJECTS );
         fee_map[ which ] = fp;
      }

      fee_schedule_type new_fees;

      for( const std::pair< int, fee_parameters >& item : fee_map )
         new_fees.parameters.insert( item.second );
      new_fees.scale = scale;

      chain_parameters new_params = current_params;
      new_params.get_mutable_fees() = new_fees;

      committee_member_update_global_parameters_operation update_op;
      update_op.new_parameters = new_params;

      proposal_create_operation prop_op;

      prop_op.expiration_time = expiration_time;
      prop_op.review_period_seconds = current_params.committee_proposal_review_period;
      prop_op.fee_paying_account = get_account(proposing_account).id;

      prop_op.proposed_ops.emplace_back( update_op );
      current_params.get_current_fees().set_fee( prop_op.proposed_ops.back().op );

      signed_transaction tx;
      tx.operations.push_back(prop_op);
      set_operation_fees(tx, current_params.get_current_fees());
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   signed_transaction approve_proposal(
      const string& fee_paying_account,
      const string& proposal_id,
      const approval_delta& delta,
      bool broadcast = false)
   {
      proposal_update_operation update_op;

      update_op.fee_paying_account = get_account(fee_paying_account).id;
      update_op.proposal = fc::variant(proposal_id, 1).as<proposal_id_type>( 1 );
      // make sure the proposal exists
      get_object( update_op.proposal );

      for( const std::string& name : delta.active_approvals_to_add )
         update_op.active_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.active_approvals_to_remove )
         update_op.active_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_add )
         update_op.owner_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_remove )
         update_op.owner_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& k : delta.key_approvals_to_add )
         update_op.key_approvals_to_add.insert( public_key_type( k ) );
      for( const std::string& k : delta.key_approvals_to_remove )
         update_op.key_approvals_to_remove.insert( public_key_type( k ) );

      signed_transaction tx;
      tx.operations.push_back(update_op);
      set_operation_fees(tx, get_global_properties().parameters.get_current_fees());
      tx.validate();
      return sign_transaction(tx, broadcast);
   }

   void dbg_make_uia(string creator, string symbol)
   {
      asset_options opts;
      opts.flags &= ~(white_list | disable_force_settle | global_settle);
      opts.issuer_permissions = opts.flags;
      opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
      create_asset(get_account(creator).name, symbol, 2, opts, {}, true);
   }

   void dbg_make_mia(string creator, string symbol)
   {
      asset_options opts;
      opts.flags &= ~white_list;
      opts.issuer_permissions = opts.flags;
      opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
      bitasset_options bopts;
      create_asset(get_account(creator).name, symbol, 2, opts, bopts, true);
   }

   void dbg_push_blocks( const std::string& src_filename, uint32_t count )
   {
      use_debug_api();
      (*_remote_debug)->debug_push_blocks( src_filename, count );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_generate_blocks( const std::string& debug_wif_key, uint32_t count )
   {
      use_debug_api();
      (*_remote_debug)->debug_generate_blocks( debug_wif_key, count );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_stream_json_objects( const std::string& filename )
   {
      use_debug_api();
      (*_remote_debug)->debug_stream_json_objects( filename );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_update_object( const fc::variant_object& update )
   {
      use_debug_api();
      (*_remote_debug)->debug_update_object( update );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void use_network_node_api()
   {
      if( _remote_net_node )
         return;
      try
      {
         _remote_net_node = _remote_api->network_node();
      }
      catch( const fc::exception& e )
      {
         std::cerr << "\nCouldn't get network node API.  You probably are not configured\n"
         "to access the network API on the witness_node you are\n"
         "connecting to.  Please follow the instructions in README.md to set up an apiaccess file.\n"
         "\n";
         throw;
      }
   }

   void use_debug_api()
   {
      if( _remote_debug )
         return;
      try
      {
        _remote_debug = _remote_api->debug();
      }
      catch( const fc::exception& e )
      {
         std::cerr << "\nCouldn't get debug node API.  You probably are not configured\n"
         "to access the debug API on the node you are connecting to.\n"
         "\n"
         "To fix this problem:\n"
         "- Please ensure you are running debug_node, not witness_node.\n"
         "- Please follow the instructions in README.md to set up an apiaccess file.\n"
         "\n";
      }
   }

   void network_add_nodes( const vector<string>& nodes )
   {
      use_network_node_api();
      for( const string& node_address : nodes )
      {
         (*_remote_net_node)->add_node( fc::ip::endpoint::from_string( node_address ) );
      }
   }

   vector< variant > network_get_connected_peers()
   {
      use_network_node_api();
      const auto peers = (*_remote_net_node)->get_connected_peers();
      vector< variant > result;
      result.reserve( peers.size() );
      for( const auto& peer : peers )
      {
         variant v;
         fc::to_variant( peer, v, GRAPHENE_MAX_NESTED_OBJECTS );
         result.push_back( v );
      }
      return result;
   }

   void flood_network(string prefix, uint32_t number_of_transactions)
   {
      try
      {
         const account_object& master = *_wallet.my_accounts.get<by_name>().lower_bound("import");
         int number_of_accounts = number_of_transactions / 3;
         number_of_transactions -= number_of_accounts;
         try {
            dbg_make_uia(master.name, "SHILL");
         } catch(...) {/* Ignore; the asset probably already exists.*/}

         fc::time_point start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            std::ostringstream brain_key;
            brain_key << "brain key for account " << prefix << i;
            signed_transaction trx = create_account_with_brain_key(
                  brain_key.str(), prefix + fc::to_string(i), master.name, master.name,
                  /* broadcast = */ true, /* save wallet = */ false);
         }
         fc::time_point end = fc::time_point::now();
         ilog("Created ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts)("time", (end - start).count() / 1000));

         start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            signed_transaction trx = transfer(master.name, prefix + fc::to_string(i), "10", "CORE", "", true);
            trx = transfer(master.name, prefix + fc::to_string(i), "1", "CORE", "", true);
         }
         end = fc::time_point::now();
         ilog("Transferred to ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts*2)("time", (end - start).count() / 1000));

         start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            signed_transaction trx = issue_asset(prefix + fc::to_string(i), "1000", "SHILL", "", true);
         }
         end = fc::time_point::now();
         ilog("Issued to ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts)("time", (end - start).count() / 1000));
      }
      catch (...)
      {
         throw;
      }

   }

   operation get_prototype_operation( string operation_name )
   {
      auto it = _prototype_ops.find( operation_name );
      if( it == _prototype_ops.end() )
         FC_THROW("Unsupported operation: \"${operation_name}\"", ("operation_name", operation_name));
      return it->second;
   }

   string                  _wallet_filename;
   wallet_data             _wallet;

   map<public_key_type,string> _keys;
   fc::sha512                  _checksum;

   chain_id_type           _chain_id;
   fc::api<login_api>      _remote_api;
   fc::api<database_api>   _remote_db;
   fc::api<network_broadcast_api>   _remote_net_broadcast;
   fc::api<history_api>    _remote_hist;
   fc::api<custom_operations_api>    _custom_operations;
   optional< fc::api<network_node_api> > _remote_net_node;
   optional< fc::api<graphene::debug_witness::debug_api> > _remote_debug;

   flat_map<string, operation> _prototype_ops;

   static_variant_map _operation_which_map = create_static_variant_map< operation >();

#ifdef __unix__
   mode_t                  _old_umask;
#endif
   const string _wallet_filename_extension = ".wallet";
};

}}} // namespace graphene::wallet::detail