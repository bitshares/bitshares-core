/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

namespace graphene { namespace wallet {

namespace detail {

// BLOCK  TRX  OP  VOP
struct operation_printer
{
private:
   ostream& out;
   const wallet_api_impl& wallet;
   operation_result result;

   std::string fee(const asset& a) const;

public:
   operation_printer( ostream& out, const wallet_api_impl& wallet, const operation_result& r = operation_result() )
      : out(out),
        wallet(wallet),
        result(r)
   {}
   typedef std::string result_type;

   template<typename T>
   std::string operator()(const T& op)const;

   std::string operator()(const transfer_operation& op)const;
   std::string operator()(const transfer_from_blind_operation& op)const;
   std::string operator()(const transfer_to_blind_operation& op)const;
   std::string operator()(const account_create_operation& op)const;
   std::string operator()(const account_update_operation& op)const;
   std::string operator()(const asset_create_operation& op)const;
};

struct operation_result_printer
{
public:
   explicit operation_result_printer( const wallet_api_impl& w )
      : _wallet(w) {}
   const wallet_api_impl& _wallet;
   typedef std::string result_type;

   std::string operator()(const void_result& x) const;
   std::string operator()(const object_id_type& oid);
   std::string operator()(const asset& a);
};

std::string operation_printer::fee(const asset& a)const {
   out << "   (Fee: " << wallet.get_asset(a.asset_id).amount_to_pretty_string(a) << ")";
   return "";
}

template<typename T>
std::string operation_printer::operator()(const T& op)const
{
   //balance_accumulator acc;
   //op.get_balance_delta( acc, result );
   auto a = wallet.get_asset( op.fee.asset_id );
   auto payer = wallet.get_account( op.fee_payer() );

   string op_name = fc::get_typename<T>::name();
   if( op_name.find_last_of(':') != string::npos )
      op_name.erase(0, op_name.find_last_of(':')+1);
   out << op_name <<" ";
  // out << "balance delta: " << fc::json::to_string(acc.balance) <<"   ";
   out << payer.name << " fee: " << a.amount_to_pretty_string( op.fee );
   operation_result_printer rprinter(wallet);
   std::string str_result = result.visit(rprinter);
   if( str_result != "" )
   {
      out << "   result: " << str_result;
   }
   return "";
}
std::string operation_printer::operator()(const transfer_from_blind_operation& op)const
{
   auto a = wallet.get_asset( op.fee.asset_id );
   auto receiver = wallet.get_account( op.to );

   out <<  receiver.name
   << " received " << a.amount_to_pretty_string( op.amount ) << " from blinded balance";
   return "";
}
std::string operation_printer::operator()(const transfer_to_blind_operation& op)const
{
   auto fa = wallet.get_asset( op.fee.asset_id );
   auto a = wallet.get_asset( op.amount.asset_id );
   auto sender = wallet.get_account( op.from );

   out <<  sender.name
   << " sent " << a.amount_to_pretty_string( op.amount ) << " to " << op.outputs.size() << " blinded balance" << (op.outputs.size()>1?"s":"")
   << " fee: " << fa.amount_to_pretty_string( op.fee );
   return "";
}
string operation_printer::operator()(const transfer_operation& op) const
{
   out << "Transfer " << wallet.get_asset(op.amount.asset_id).amount_to_pretty_string(op.amount)
       << " from " << wallet.get_account(op.from).name << " to " << wallet.get_account(op.to).name;
   std::string memo;
   if( op.memo )
   {
      if( wallet.is_locked() )
      {
         out << " -- Unlock wallet to see memo.";
      } else {
         try {
            FC_ASSERT(wallet._keys.count(op.memo->to) || wallet._keys.count(op.memo->from), "Memo is encrypted to a key ${to} or ${from} not in this wallet.", ("to", op.memo->to)("from",op.memo->from));
            if( wallet._keys.count(op.memo->to) ) {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->to));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->from);
               out << " -- Memo: " << memo;
            } else {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->from));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->to);
               out << " -- Memo: " << memo;
            }
         } catch (const fc::exception& e) {
            out << " -- could not decrypt memo";
         }
      }
   }
   fee(op.fee);
   return memo;
}

std::string operation_printer::operator()(const account_create_operation& op) const
{
   out << "Create Account '" << op.name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const account_update_operation& op) const
{
   out << "Update Account '" << wallet.get_account(op.account).name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const asset_create_operation& op) const
{
   out << "Create ";
   if( op.bitasset_opts.valid() )
      out << "BitAsset ";
   else
      out << "User-Issue Asset ";
   out << "'" << op.symbol << "' with issuer " << wallet.get_account(op.issuer).name;
   return fee(op.fee);
}

std::string operation_result_printer::operator()(const void_result& x) const
{
   return "";
}

std::string operation_result_printer::operator()(const object_id_type& oid)
{
   return std::string(oid);
}

std::string operation_result_printer::operator()(const asset& a)
{
   return _wallet.get_asset(a.asset_id).amount_to_pretty_string(a);
}

void wallet_api_impl::encrypt_keys()
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

memo_data wallet_api_impl::sign_memo(string from, string to, string memo)
{
   FC_ASSERT( !self.is_locked() );

   memo_data md = memo_data();

   // get account memo key, if that fails, try a pubkey
   try {
      account_object from_account = get_account(from);
      md.from = from_account.options.memo_key;
   } catch (const fc::exception& e) {
      md.from =  self.get_public_key( from );
   }
   // same as above, for destination key
   try {
      account_object to_account = get_account(to);
      md.to = to_account.options.memo_key;
   } catch (const fc::exception& e) {
      md.to = self.get_public_key( to );
   }

   md.set_message(get_private_key(md.from), md.to, memo);
   return md;
}

string wallet_api_impl::read_memo(const memo_data& md)
{
   FC_ASSERT(!is_locked());
   std::string clear_text;

   const memo_data *memo = &md;

   try {
      FC_ASSERT(_keys.count(memo->to) || _keys.count(memo->from), "Memo is encrypted to a key ${to} or ${from} not in this wallet.", ("to", memo->to)("from",memo->from));
      if( _keys.count(memo->to) ) {
         auto my_key = wif_to_key(_keys.at(memo->to));
         FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
         clear_text = memo->get_message(*my_key, memo->from);
      } else {
         auto my_key = wif_to_key(_keys.at(memo->from));
         FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
         clear_text = memo->get_message(*my_key, memo->to);
      }
   } catch (const fc::exception& e) {
      elog("Error when decrypting memo: ${e}", ("e", e.to_detail_string()));
   }

   return clear_text;
}

signed_transaction wallet_api_impl::whitelist_account(string authorizing_account,
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
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (authorizing_account)(account_to_list)(new_listing_status)(broadcast) ) }

// This function generates derived keys starting with index 0 and keeps incrementing
// the index until it finds a key that isn't registered in the block chain.  To be
// safer, it continues checking for a few more keys to make sure there wasn't a short gap
// caused by a failed registration or the like.
int wallet_api_impl::find_first_unused_derived_key_index(const fc::ecc::private_key& parent_key)
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

template<class T>
optional<T> maybe_id( const string& name_or_id )
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
   return optional<T>();
}

vector< vesting_balance_object_with_info > wallet_api_impl::get_vesting_balances( string account_name )
{ try {
   fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>( account_name );
   std::vector<vesting_balance_object_with_info> result;
   fc::time_point_sec now = _remote_db->get_dynamic_global_properties().time;

   if( vbid )
   {
      result.emplace_back( get_object<vesting_balance_object>(*vbid), now );
      return result;
   }
   /*
      * Compatibility issue
      * Current Date: 2018-09-28 More info: https://github.com/bitshares/bitshares-core/issues/1307
      * Todo: remove the next 2 lines and change always_id to name in remote call after next hardfork
   */
   auto account = get_account(account_name);
   auto always_id = account_id_to_string(account.id);

   vector< vesting_balance_object > vbos = _remote_db->get_vesting_balances( always_id );
   if( vbos.size() == 0 )
      return result;

   for( const vesting_balance_object& vbo : vbos )
      result.emplace_back( vbo, now );

   return result;
} FC_CAPTURE_AND_RETHROW( (account_name) )
}

signed_transaction wallet_api_impl::withdraw_vesting(
   string witness_name,
   string amount,
   string asset_symbol,
   bool broadcast )
{ try {
   asset_object asset_obj = get_asset( asset_symbol );
   fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>(witness_name);
   if( !vbid )
   {
      witness_object wit = get_witness( witness_name );
      FC_ASSERT( wit.pay_vb );
      vbid = wit.pay_vb;
   }

   vesting_balance_object vbo = get_object< vesting_balance_object >( *vbid );
   vesting_balance_withdraw_operation vesting_balance_withdraw_op;

   vesting_balance_withdraw_op.vesting_balance = *vbid;
   vesting_balance_withdraw_op.owner = vbo.owner;
   vesting_balance_withdraw_op.amount = asset_obj.amount_from_string(amount);

   signed_transaction tx;
   tx.operations.push_back( vesting_balance_withdraw_op );
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (witness_name)(amount) )
}

signed_transaction wallet_api_impl::transfer(string from, string to, string amount,
                              string asset_symbol, string memo, bool broadcast)
{ try {
   FC_ASSERT( !self.is_locked() );
   fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
   FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

   account_object from_account = get_account(from);
   account_object to_account = get_account(to);
   account_id_type from_id = from_account.id;
   account_id_type to_id = to_account.id;

   transfer_operation xfer_op;

   xfer_op.from = from_id;
   xfer_op.to = to_id;
   xfer_op.amount = asset_obj->amount_from_string(amount);

   if( memo.size() )
      {
         xfer_op.memo = memo_data();
         xfer_op.memo->from = from_account.options.memo_key;
         xfer_op.memo->to = to_account.options.memo_key;
         xfer_op.memo->set_message(get_private_key(from_account.options.memo_key),
                                    to_account.options.memo_key, memo);
      }

   signed_transaction tx;
   tx.operations.push_back(xfer_op);
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
   tx.validate();

   return sign_transaction(tx, broadcast);
} FC_CAPTURE_AND_RETHROW( (from)(to)(amount)(asset_symbol)(memo)(broadcast) ) }

// imports the private key into the wallet, and associate it in some way (?) with the
// given account name.
// @returns true if the key matches a current active/owner/memo key for the named
//          account, false otherwise (but it is stored either way)
bool wallet_api_impl::import_key(string account_name_or_id, string wif_key)
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
   std::copy(active_keys.begin(), active_keys.end(), std::inserter(all_keys_for_account, all_keys_for_account.end()));
   std::copy(owner_keys.begin(), owner_keys.end(), std::inserter(all_keys_for_account, all_keys_for_account.end()));
   all_keys_for_account.insert(account.options.memo_key);

   _keys[wif_pub_key] = wif_key;

   _wallet.update_account(account);

   _wallet.extra_keys[account.id].insert(wif_pub_key);

   return all_keys_for_account.find(wif_pub_key) != all_keys_for_account.end();
}

std::string wallet_api_impl::account_id_to_string(account_id_type id) const
{
   std::string account_id = fc::to_string(id.space_id)
                              + "." + fc::to_string(id.type_id)
                              + "." + fc::to_string(id.instance.value);
   return account_id;
}
account_object wallet_api_impl::get_account(account_id_type id) const
{
   std::string account_id = account_id_to_string(id);

   auto rec = _remote_db->get_accounts({account_id}).front();
   FC_ASSERT(rec);
   return *rec;
}

fc::ecc::private_key wallet_api_impl::get_private_key_for_account(const account_object& account)const
{
   vector<public_key_type> active_keys = account.active.get_keys();
   if (active_keys.size() != 1)
      FC_THROW("Expecting a simple authority with one active key");
   return get_private_key(active_keys.front());
}

signed_transaction wallet_api_impl::approve_proposal(
   const string& fee_paying_account,
   const string& proposal_id,
   const approval_delta& delta,
   bool broadcast)
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
   set_operation_fees(tx, get_global_properties().parameters.current_fees);
   tx.validate();
   return sign_transaction(tx, broadcast);
}

signed_transaction wallet_api_impl::create_account_with_private_key(fc::ecc::private_key owner_privkey,
                                                      string account_name,
                                                      string registrar_account,
                                                      string referrer_account,
                                                      bool broadcast,
                                                      bool save_wallet)
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

      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);

      vector<public_key_type> paying_keys = registrar_account_object.active.get_keys();

      auto dyn_props = get_dynamic_global_properties();
      tx.set_reference_block( dyn_props.head_block_id );
      tx.set_expiration( dyn_props.time + fc::seconds(30) );
      tx.validate();

      for( public_key_type& key : paying_keys )
      {
         auto it = _keys.find(key);
         if( it != _keys.end() )
         {
            fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
            FC_ASSERT( privkey.valid(), "Malformed private key in _keys" );
            tx.sign( *privkey, _chain_id );
         }
      }

      // we do not insert owner_privkey here because
      //    it is intended to only be used for key recovery
      _wallet.pending_account_registrations[account_name].push_back(key_to_wif( active_privkey ));
      _wallet.pending_account_registrations[account_name].push_back(key_to_wif( memo_privkey ));
      if( save_wallet )
         save_wallet_file();
      if( broadcast )
         _remote_net_broadcast->broadcast_transaction( tx );
      return tx;
} FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account)(broadcast) ) }

string normalize_brain_key( string s )
{
   size_t i = 0, n = s.length();
   std::string result;
   char c;
   result.reserve( n );

   bool preceded_by_whitespace = false;
   bool non_empty = false;
   while( i < n )
   {
      c = s[i++];
      switch( c )
      {
      case ' ':  case '\t': case '\r': case '\n': case '\v': case '\f':
         preceded_by_whitespace = true;
         continue;

      case 'a': c = 'A'; break;
      case 'b': c = 'B'; break;
      case 'c': c = 'C'; break;
      case 'd': c = 'D'; break;
      case 'e': c = 'E'; break;
      case 'f': c = 'F'; break;
      case 'g': c = 'G'; break;
      case 'h': c = 'H'; break;
      case 'i': c = 'I'; break;
      case 'j': c = 'J'; break;
      case 'k': c = 'K'; break;
      case 'l': c = 'L'; break;
      case 'm': c = 'M'; break;
      case 'n': c = 'N'; break;
      case 'o': c = 'O'; break;
      case 'p': c = 'P'; break;
      case 'q': c = 'Q'; break;
      case 'r': c = 'R'; break;
      case 's': c = 'S'; break;
      case 't': c = 'T'; break;
      case 'u': c = 'U'; break;
      case 'v': c = 'V'; break;
      case 'w': c = 'W'; break;
      case 'x': c = 'X'; break;
      case 'y': c = 'Y'; break;
      case 'z': c = 'Z'; break;

      default:
         break;
      }
      if( preceded_by_whitespace && non_empty )
         result.push_back(' ');
      result.push_back(c);
      preceded_by_whitespace = false;
      non_empty = true;
   }
   return result;
}

signed_transaction wallet_api_impl::create_account_with_brain_key(string brain_key,
                                                   string account_name,
                                                   string registrar_account,
                                                   string referrer_account,
                                                   bool broadcast,
                                                   bool save_wallet)
{ try {
   FC_ASSERT( !self.is_locked() );
   string normalized_brain_key = normalize_brain_key( brain_key );
   // TODO:  scan blockchain for accounts that exist with same brain key
   fc::ecc::private_key owner_privkey = derive_private_key( normalized_brain_key, 0 );
   return create_account_with_private_key(owner_privkey, account_name, registrar_account, referrer_account, broadcast, save_wallet);
} FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account) ) }

void wallet_api_impl::claim_registered_account(const account_object& account)
{
   auto it = _wallet.pending_account_registrations.find( account.name );
   FC_ASSERT( it != _wallet.pending_account_registrations.end() );
   for (const std::string& wif_key : it->second)
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
      }
   _wallet.pending_account_registrations.erase( it );
}

signed_transaction wallet_api_impl::register_account(
                                    string name,
                                    public_key_type owner,
                                    public_key_type active,
                                    string  registrar_account,
                                    string  referrer_account,
                                    uint32_t referrer_percent,
                                    bool broadcast)
{ try {
   FC_ASSERT( !self.is_locked() );
   FC_ASSERT( is_valid_name(name) );
   account_create_operation account_create_op;

   // #449 referrer_percent is on 0-100 scale, if user has larger
   // number it means their script is using GRAPHENE_100_PERCENT scale
   // instead of 0-100 scale.
   FC_ASSERT( referrer_percent <= 100 );
   // TODO:  process when pay_from_account is ID

   account_object registrar_account_object =
         this->get_account( registrar_account );
   FC_ASSERT( registrar_account_object.is_lifetime_member() );

   account_id_type registrar_account_id = registrar_account_object.id;

   account_object referrer_account_object =
         this->get_account( referrer_account );
   account_create_op.referrer = referrer_account_object.id;
   account_create_op.referrer_percent = uint16_t( referrer_percent * GRAPHENE_1_PERCENT );

   account_create_op.registrar = registrar_account_id;
   account_create_op.name = name;
   account_create_op.owner = authority(1, owner, 1);
   account_create_op.active = authority(1, active, 1);
   account_create_op.options.memo_key = active;

   signed_transaction tx;

   tx.operations.push_back( account_create_op );

   auto current_fees = _remote_db->get_global_properties().parameters.current_fees;
   set_operation_fees( tx, current_fees );

   vector<public_key_type> paying_keys = registrar_account_object.active.get_keys();

   auto dyn_props = get_dynamic_global_properties();
   tx.set_reference_block( dyn_props.head_block_id );
   tx.set_expiration( dyn_props.time + fc::seconds(30) );
   tx.validate();

   for( public_key_type& key : paying_keys )
   {
      auto it = _keys.find(key);
      if( it != _keys.end() )
      {
         fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
         if( !privkey.valid() )
         {
            FC_ASSERT( false, "Malformed private key in _keys" );
         }
         tx.sign( *privkey, _chain_id );
      }
   }

   if( broadcast )
      _remote_net_broadcast->broadcast_transaction( tx );
   return tx;
} FC_CAPTURE_AND_RETHROW( (name)(owner)(active)(registrar_account)(referrer_account)(referrer_percent)(broadcast) ) }


signed_transaction wallet_api_impl::upgrade_account(string name, bool broadcast)
{ try {
   FC_ASSERT( !self.is_locked() );
   account_object account_obj = get_account(name);
   FC_ASSERT( !account_obj.is_lifetime_member() );

   signed_transaction tx;
   account_upgrade_operation op;
   op.account_to_upgrade = account_obj.get_id();
   op.upgrade_to_lifetime_member = true;
   tx.operations = {op};
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (name) ) }

account_object wallet_api_impl::get_account(string account_name_or_id) const
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
account_id_type wallet_api_impl::get_account_id(string account_name_or_id) const
{
   return get_account(account_name_or_id).get_id();
}

}}} // graphene::wallet::detail
