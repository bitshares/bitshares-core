void wallet_api_impl::snapshot(const std::string&type, int64_t param) const
{ try{

      _remote_db->snapshot(type,param);

} FC_CAPTURE_AND_RETHROW( (type) ) }
vector< crowdfund_object > wallet_api_impl::get_crowdfunds( string name_or_id)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   vector< crowdfund_object > crowdfunds = _remote_db->get_crowdfund_objects( account.get_id() );

   return crowdfunds;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }
vector< crowdfund_contract_object > wallet_api_impl::get_crowdfund_contracts( string name_or_id)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   vector< crowdfund_contract_object > crowdfund_contracts = _remote_db->get_crowdfund_contract_objects( account.get_id() );

   return crowdfund_contracts;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }
vector< crowdfund_object > wallet_api_impl::list_crowdfunds( string id,uint32_t limit)
{ try {
   FC_ASSERT(!is_locked());
   vector< crowdfund_object > crowdfunds;
   if( auto crowdfund_id = maybe_id<crowdfund_id_type>(id) )
    {
        crowdfunds = _remote_db->list_crowdfund_objects( *crowdfund_id,limit);
   }
   return crowdfunds;
} FC_CAPTURE_AND_RETHROW( (id) ) }
signed_transaction wallet_api_impl::initiate_crowdfund(string name_or_id, string id, uint64_t u,uint64_t t , bool broadcast)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   auto asset_id = maybe_id<asset_id_type>(id);

   signed_transaction tx;
   signed_transaction signed_tx;
   if(asset_id)
   {
      tx.operations.reserve( 1 );
      initiate_crowdfund_operation op;
      op.owner             = account.get_id();
      op.u                 = u;
      op.t                 = t;
      op.asset_id          = *asset_id;
      tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_tx = sign_transaction( tx, broadcast );


  }
  return signed_tx;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

signed_transaction wallet_api_impl::participate_crowdfund(string name_or_id, string id, uint64_t valuation,uint64_t cap , bool broadcast)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   auto crowdfund_id = maybe_id<crowdfund_id_type>(id);
   signed_transaction tx;
   signed_transaction signed_tx;
   if(crowdfund_id)
   {
      tx.operations.reserve( 1 );
      participate_crowdfund_operation op;
      op.buyer             = account.get_id();
      op.crowdfund         = *crowdfund_id;
      op.valuation         = valuation;
      op.cap               = cap;
      //op.A                 = pubkey;
      tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_tx = sign_transaction( tx, broadcast );


  }
  return signed_tx;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

signed_transaction wallet_api_impl::withdraw_crowdfund(string name_or_id, string id, bool broadcast)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   auto crowdfund_contract_id = maybe_id<crowdfund_contract_id_type>(id);
   signed_transaction tx;
   signed_transaction signed_tx;
   if(crowdfund_contract_id)
   {
      tx.operations.reserve( 1 );
      withdraw_crowdfund_operation op;
      op.buyer             = account.get_id();
      op.crowdfund_contract= *crowdfund_contract_id;
      tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_tx = sign_transaction( tx, broadcast );

   }
   return signed_tx;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

signed_transaction wallet_api_impl::cancel_vesting(string name_or_id, string id, bool broadcast)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );
   auto bal_id = maybe_id<balance_id_type>(id);
   signed_transaction tx;
   signed_transaction signed_tx;
   if(bal_id)
   {
      tx.operations.reserve( 1 );
      cancel_vesting_operation op;
      op.sender             = account.get_id();
      op.balance_object     = *bal_id;
      tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_tx = sign_transaction( tx, broadcast );

   }
   return signed_tx;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

vector< balance_object > wallet_api_impl::list_balances( string name_or_id)
{ try {
   FC_ASSERT(!is_locked());
   account_object account = get_account( name_or_id );


   vector< address > addrs;
   for( auto &pub_key : account.active.key_auths)
   {
   fc::ecc::public_key pk = pub_key.first.operator fc::ecc::public_key() ;
   addrs.push_back( pk );
   addrs.push_back( pts_address( pk, false, 56 ) );
   addrs.push_back( pts_address( pk, true, 56 ) );
   addrs.push_back( pts_address( pk, false, 0 ) );
   addrs.push_back( pts_address( pk, true, 0 ) );
   }
   for( auto &pub_key : account.owner.key_auths)
   {
   fc::ecc::public_key pk = pub_key.first.operator fc::ecc::public_key() ;
   addrs.push_back( pk );
   addrs.push_back( pts_address( pk, false, 56 ) );
   addrs.push_back( pts_address( pk, true, 56 ) );
   addrs.push_back( pts_address( pk, false, 0 ) );
   addrs.push_back( pts_address( pk, true, 0 ) );
   }

   vector< balance_object > balances = _remote_db->get_balance_objects( addrs );
   wdump((balances));
   addrs.clear();

   return balances;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

