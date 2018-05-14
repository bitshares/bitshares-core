      ////////////////////////////
      //   crowdfund            //
      ////////////////////////////
      /**
       * This call will list all crowdfunds owned
       * by the given account.
       */
      vector< crowdfund_object > get_crowdfunds( string account_name_or_id);
      /**
       * This call will list all crowdfund  contracts owned
       * by the given account.
       */
      vector< crowdfund_contract_object > get_crowdfund_contracts( string account_name_or_id);
      /**
       * This call will list a number of crowdfunds 
       * from a given crowdfund id.
       */
      vector< crowdfund_object > list_crowdfunds( string id,uint32_t limit);

      signed_transaction initiate_crowdfund(string name_or_id, string id, uint64_t u,uint64_t t , bool broadcast);
      signed_transaction participate_crowdfund(string name_or_id, string id, uint64_t valuation,uint64_t cap , bool broadcast);
      signed_transaction withdraw_crowdfund(string name_or_id, string id, bool broadcast);
    
      
      ////////////////////////////
      //   misc                 //
      ////////////////////////////
      void snapshot(const string & type, int64_t param ) const;



      ////////////////////////////
      //   vestin               //
      ////////////////////////////
      /**
       * This call will list all balances owned
       * by the given account.
       */
      vector< balance_object > list_balances( string account_name_or_id);
      signed_transaction cancel_vesting(string name_or_id, string id, bool broadcast);
#ifndef CYBEX_WALLET_API

#define CYBEX_WALLET_API (cancel_vesting)(get_crowdfunds)(get_crowdfund_contracts)(list_crowdfunds)(initiate_crowdfund)(participate_crowdfund)(withdraw_crowdfund)(snapshot)

#endif

