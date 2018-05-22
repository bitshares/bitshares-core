#include <cybex/block_callback.hpp>

#include <cybex/crowdfund.hpp>
#include <cybex/crowdfund_contract.hpp>


namespace graphene {
namespace chain {


void block_callback::process_crowdfund(database &db) const
{
      fc::time_point_sec now = db.head_block_time();
   
      auto& crowdfund_idx = db.get_index_type<crowdfund_index>();
      auto& by_id_idx = crowdfund_idx.indices().get<by_id>();

      auto itr = by_id_idx.lower_bound( crowdfund_id_type(0));

      while( itr != by_id_idx.end())
      {

 
          if(itr->state==0 )
          {
            uint64_t s = ( now-itr->begin).to_seconds();
           
            if(s >= itr->u)
            {
                  auto_withdraw(db,*itr);
                  db.modify( *itr,[&](crowdfund_object &c ){
                       c.state=1;
                  });
            }
           }
          itr++;                        
      }

}


void block_callback::auto_withdraw(database &db,const crowdfund_object & crowdfund) const
{
      const auto id = crowdfund.id;
      const auto& crowdfund_contract_idx = db.get_index_type<crowdfund_contract_index>();
      const auto& by_crowdfund_idx = crowdfund_contract_idx.indices().get<by_crowdfund>();

      auto itr = by_crowdfund_idx.lower_bound( id);
      
      while( itr != by_crowdfund_idx.end() && itr->crowdfund==id)
      {
         //
         // list out B1,B2,...,Bk
         //
         //  skip "used"
         while( itr != by_crowdfund_idx.end() && itr->crowdfund==id)
         {
              if(itr->state != CROWDFUND_STATE_USED) break;
              itr++; 
         }

         if (itr==by_crowdfund_idx.end()||itr->crowdfund!=id){
               return;
         }
         auto B1 = itr->cap;

         auto itr0 = itr;
         share_type S;
         int k;
         k=0;
         S=0;
         do{
             S.value += itr->valuation.value;
             k++;
             itr++;
         }
         while( itr != by_crowdfund_idx.end() 
                && itr->crowdfund==id
                && itr->cap ==B1);

         //printf("V=%ld,k=%d S=%ld,B1=%ld\n",crowdfund.V.value,k,S.value,B1.value);
         //
         //  V-S<B1? 
         //  Yes,  kick away and refund all k pariticipators.
         if((crowdfund.V -S).value > B1.value)
         {
              // printf("refund and kick\n");
              while(itr0!=itr) {
                  db.modify( *itr0,[&](crowdfund_contract_object &c ){
                       c.state=CROWDFUND_STATE_USED;
                  });
                  auto account_id=itr0->owner;
                  asset delta;
                  delta.asset_id=asset_id_type(0);
                  delta.amount.value=itr0->valuation.value;
                  db.adjust_balance(account_id,delta);
                  asset a;
                  a.asset_id= crowdfund.asset_id;
                  a.amount.value=0;
                  db.set_balance(account_id,a);
                  itr0++;
              }
              db.modify( crowdfund ,[&](crowdfund_object &c ){
                       c.state=CROWDFUND_STATE_USED;
                       c.V -= S;
              });
         } 
         //
         // refund q *V(Bi)  
         //
         else if (crowdfund.V.value>=B1.value)
         {
              float q= (float)(crowdfund.V-B1).value/S.value;
              share_type total_refund=0;
              //printf("refund q:%f\n",q);
              while(itr0!=itr) {
                  auto account_id=itr0->owner;
                  asset delta;
                  delta.asset_id=asset_id_type(0);
                  delta.amount.value=itr0->valuation.value*q;
                  db.adjust_balance(account_id,-delta);
                  total_refund += delta.amount.value;

                  share_type b_i=db.get_balance(account_id,crowdfund.asset_id).amount;
                  delta.asset_id=crowdfund.asset_id;
                  delta.amount.value=q*b_i.value;
                  db.adjust_balance(account_id,-delta);
                

                  db.modify( *itr0,[&](crowdfund_contract_object &c ){
                       c.state=CROWDFUND_STATE_ENDED;
                       c.valuation.value *= (1-q);
                  });
                  itr0++;
               }
              db.modify( crowdfund ,[&](crowdfund_object &c ){
                       c.V -= total_refund;
              });
           }
           else
           {
              //printf("no refund\n");
              while(itr0!=itr) {
                

                  db.modify( *itr0,[&](crowdfund_contract_object &c ){
                       c.state=CROWDFUND_STATE_ENDED;
                  });
                  itr0++;
               }
           }
        

           

      }
}


}}
