      ///////////////////
      //  crowdfund    //
      ///////////////////
      /** @return all crowdfund  objects for an account  */
      vector<crowdfund_object> get_crowdfund_objects( const account_id_type account_id)const;

      vector<crowdfund_object> list_crowdfund_objects( const crowdfund_id_type id,uint32_t limit)const;
      /** @return all crowdfund contract  objects for a set of addresses */
      vector<crowdfund_contract_object> get_crowdfund_contract_objects( const account_id_type account_id )const;
      ///////////////////
      //  misc    //
      ///////////////////
      void snapshot(const string & type,int64_t param) const;

#ifndef CYBEX_DATABASE_API

#define  CYBEX_DATABASE_API \
(snapshot)(get_crowdfund_contract_objects)(list_crowdfund_objects)(get_crowdfund_objects)

#endif
