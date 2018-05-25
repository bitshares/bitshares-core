void database_api_impl::snapshot(const string & type, int64_t param )const
{

    if(type=="block")
    {
        graphene::chain::block_callback::snapshot_at_block_num=param;
    }
    else if(type=="day")
    {
        graphene::chain::block_callback::snapshot_in_day=param;
    }
    else if(type=="time")
    {
         graphene::chain::block_callback::snapshot_in_hour = ((uint16_t)param)/100;
         graphene::chain::block_callback::snapshot_in_minute = ((uint16_t)param)%100;
    } 
}
vector<crowdfund_contract_object> database_api_impl::get_crowdfund_contract_objects( const account_id_type id )const
{
   try
   {
      const auto& crowdfund_contract_idx = _db.get_index_type<crowdfund_contract_index>();
      const auto& by_owner_idx = crowdfund_contract_idx.indices().get<by_owner>();

      vector<crowdfund_contract_object> result;


         auto itr = by_owner_idx.lower_bound( boost::make_tuple( id, crowdfund_id_type(0) ) );
         while( itr != by_owner_idx.end() && itr->owner == id )
         {
            result.push_back( *itr );
            ++itr;
         }
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (id) )
}
vector<crowdfund_object> database_api_impl::get_crowdfund_objects( const account_id_type id )const
{
   try
   {
      const auto& crowdfund_idx = _db.get_index_type<crowdfund_index>();
      const auto& by_owner_idx = crowdfund_idx.indices().get<by_owner>();

      vector<crowdfund_object> result;


         auto itr = by_owner_idx.lower_bound( boost::make_tuple( id, asset_id_type(0) ) );
         while( itr != by_owner_idx.end() && itr->owner == id )
         {
            result.push_back( *itr );
            ++itr;
         }
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (id) )
}
vector<crowdfund_object> database_api_impl::list_crowdfund_objects( const crowdfund_id_type  id,uint32_t limit )const
{
   try
   {
      const auto& crowdfund_idx = _db.get_index_type<crowdfund_index>();
      const auto& by_id_idx = crowdfund_idx.indices().get<by_id>();

      vector<crowdfund_object> result;



      {
         auto itr = by_id_idx.lower_bound(id);
         while( itr != by_id_idx.end() && limit )
         {
            result.push_back( *itr );
            ++itr;
            --limit;
         }
      }
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (id) )
}
void  database_api::snapshot( const string & type, int64_t param )const
{
   my->snapshot(type,param);
}
vector<crowdfund_contract_object> database_api::get_crowdfund_contract_objects( const account_id_type id )const
{
   return my->get_crowdfund_contract_objects( id );
}
vector<crowdfund_object> database_api::get_crowdfund_objects( const  account_id_type id)const
{
   return my->get_crowdfund_objects( id );
}
vector<crowdfund_object> database_api::list_crowdfund_objects( const crowdfund_id_type id,uint32_t limits)const
{
   return my->list_crowdfund_objects( id,limits );
}

