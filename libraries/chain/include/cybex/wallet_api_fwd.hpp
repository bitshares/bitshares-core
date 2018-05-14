vector< crowdfund_object > wallet_api::get_crowdfunds( string name_or_id)
{
   return my->get_crowdfunds( name_or_id);
}
vector< crowdfund_contract_object > wallet_api::get_crowdfund_contracts( string name_or_id)
{
   return my->get_crowdfund_contracts( name_or_id);
}
vector< crowdfund_object > wallet_api::list_crowdfunds( string name_or_id,uint32_t limit )
{
   return my->list_crowdfunds( name_or_id,limit);
}
signed_transaction  wallet_api::initiate_crowdfund(string name_or_id, string id, uint64_t u,uint64_t t , bool broadcast)
{
   return my->initiate_crowdfund( name_or_id, id,u,t, broadcast );
}
signed_transaction  wallet_api::participate_crowdfund(string name_or_id, string id, uint64_t valuation,uint64_t cap , bool broadcast)
{
   return my->participate_crowdfund( name_or_id, id,valuation,cap, broadcast );
}
signed_transaction  wallet_api::withdraw_crowdfund(string name_or_id, string id, bool broadcast)
{
   return my->withdraw_crowdfund( name_or_id, id, broadcast );
}
signed_transaction  wallet_api::cancel_vesting(string name_or_id, string id, bool broadcast)
{
   return my->cancel_vesting( name_or_id, id, broadcast );
}

void wallet_api::snapshot(const std::string&type, int64_t param) const
{
     my->snapshot(type,param);
}
vector< balance_object > wallet_api::list_balances( string name_or_id)
{
   return my->list_balances( name_or_id);
}
