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

void wallet_api_impl::on_block_applied( const variant& block_id )
{
   fc::async([this]{resync();}, "Resync after block");
}

void wallet_api_impl::resync()
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
      std::vector<string> pending_account_names = boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_account_registrations));

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
      std::vector<string> pending_witness_names = boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_witness_registrations));

      // look up the owners on the blockchain
      std::vector<fc::optional<graphene::chain::account_object>> owner_account_objects = _remote_db->lookup_account_names(pending_witness_names);

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

chain_property_object wallet_api_impl::get_chain_properties() const
{
   return _remote_db->get_chain_properties();
}
global_property_object wallet_api_impl::get_global_properties() const
{
   return _remote_db->get_global_properties();
}
dynamic_global_property_object wallet_api_impl::get_dynamic_global_properties() const
{
   return _remote_db->get_dynamic_global_properties();
}

template<typename T>
T wallet_api_impl::get_object(object_id<T::space_id, T::type_id, T> id)const
{
   auto ob = _remote_db->get_objects({id}).front();
   return ob.template as<T>( GRAPHENE_MAX_NESTED_OBJECTS );
}

}}} // graphene::wallet::detail
