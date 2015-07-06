#pragma once

namespace graphene { namespace chain {

   struct line_of_credit
   {
      enum flag
      {
         /** 
          * Debt may be increased via credit_passthrough operation, controlled by borrower
          **/
         bool              allow_lender_passthrough  = 0x01,
         bool              allow_borrower_passthrough  = 0x02,
         /** borrower may pull BitUSD from lender */
         bool              allow_cash_advance      = 0x04,
         /** the lender may change the interest rate */
         bool              allow_variable_interest = 0x08
      };

      account_id_type   borrower;
      account_id_type   lender;
      share_type        debt;
      share_type        credit_limit;
      share_type        passthrough_fee;
      asset_id_type     asset_type;
      /** accumulated daily, compounded every update */
      uint16_t          interest_apr = 0;
      uint8_t           flags = 0;
      /** requires borrower and lender to approve update */
      digest_type       loan_contract_digest;
   };

   /**
    *  Index on borrower/asset_type and lender/asset_type
    */
   class line_of_credit_object : public db::abstract_object<line_of_credit_object>
   {
      static const uint8_t space_id = protocol_ids;
      static const uint8_t type_id  = line_of_credit_object_type;

      account_id_type borrower()const { return terms.borrower;                                }
      account_id_type lender()const   { return terms.lender;                                  }
      asset debt()const               { return asset( terms.debt, terms.asset_type );         }
      asset credit_limit()const       { return asset( terms.credit_limit, terms.asset_type ); }
      asset_id_type asset_type()const { return term.asset_type; }

      line_of_credit terms;
      time_point_sec last_update;

      /**
       *  Both borrower and lender must approve of the line of credit before any
       *  debts may accrue. 
       */
      bool   borrower_approved = false;
      bool   borrower_lender_approved = false;
   };

   struct by_borrower;
   struct by_lender;

   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      line_of_credit_object,
      indexed_by<
         ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         ordered_non_unique< tag<by_borrower>, composite_key<
            line_of_credit_object,
            const_member_fun<line_of_credit_object, account_id_type, &line_of_credit_object::borrower>,
            const_member_fun<line_of_credit_object, asset_id_type, &line_of_credit_object::asset_type> >
         >,
         ordered_non_unique< tag<by_lender>, composite_key<
            line_of_credit_object,
            const_member_fun<line_of_credit_object, account_id_type, &line_of_credit_object::lender>,
            const_member_fun<line_of_credit_object, asset_id_type, &line_of_credit_object::asset_type> >
         >
      >
   > line_of_credit_object_multi_index_type;
   typedef generic_index<line_of_credit_object, line_of_credit_object_multi_index_type> line_of_credit_index;

   struct line_of_credit_create_operation
   {
      asset            fee;
      account_id_type  creator; ///< pays the transaction fee and approves terms by default
      line_of_credit   terms;
   };

   struct line_of_credit_accept_terms_operation
   {
      asset                    fee;
      line_of_credit_id_type   id;
      account_id_type          acceptor;
   };

   struct line_of_credit_update_operation
   {
      asset                    fee;  ///< paid for by terms.lender
      account_id_type          updater; ///< account performing the update
      line_of_credit_id_type   id;
      /**
       * Lender may increase or lower, borrower may increase
       */
      uint16_t                 new_interest_rate = 0;
      uint8_t                  flags;
      /**
       * Lender may increase or lower, borrower may lower
       */
      asset                    new_credit_limit;
   };

   /**
    *  This operation is used to transfer the debt to a debt
    *  collector.
    */
   struct line_of_credit_transfer_debt_operation
   {
      asset                    fee; ///< paid for by current_lender
      line_of_credit_id_type   loc;
      account_id_type          current_lender;
      account_id_type          new_lender;
   };


   /**
    * @brief enables payment of 3rd parties by rebalancing debts among users
    */
   struct line_of_credit_passthrough_operation
   {
      struct transfer_node
      {
         line_of_credit_id_type loc;
         account_id_type        borrower;
         account_id_type        lender;
         share_type             delta_debt;
         share_type             fee_paid; ///< always paid to the lender
      };

      asset                  fee;
      account_id_type        fee_payer; ///< accout with actual asset to pay fee
      account_id_type        from; ///< account funding the init_amount
      asset                  init_amount;
      vector<transfer_node>  passthrough; ///< pass through nodes
   };

} } // graphene / chain 
