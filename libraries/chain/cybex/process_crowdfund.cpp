#include <graphene/chain/asset_object.hpp>
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

 
            auto & crowdfund=*itr;
            auto s = ( now-crowdfund.begin).to_seconds();

            itr++;
            //ilog("s:${s} t:${t} u:${u}",("s",s)("t",crowdfund.t)("u",crowdfund.u));
            if(s>=crowdfund.u)
            {
                  crowdfund_ended(db,crowdfund);
                  db.remove(crowdfund);
            } 
            else if(s >= crowdfund.t)
            {
                  auto_withdraw(db,crowdfund);
            }
            
      }


}


void block_callback::crowdfund_ended(database &db,const crowdfund_object & crowdfund) const
{
      const auto & crowdfund_asset = db.get(crowdfund.asset_id);  
      auto const & dyn_data = crowdfund_asset.dynamic_asset_data_id(db);

      const auto id = crowdfund.id;
      const auto& crowdfund_contract_idx = db.get_index_type<crowdfund_contract_index>();
      const auto& by_crowdfund_idx = crowdfund_contract_idx.indices().get<by_crowdfund>();

      auto itr = by_crowdfund_idx.lower_bound( boost::make_tuple(id,0));
      auto end = by_crowdfund_idx.lower_bound( boost::make_tuple(id+1,0));

      share_type total_supply=0;      
      share_type total_V=0;      
      while( itr != end )
      {
              auto & crowdfund_contract = *itr;
              ilog("id:${i} state:${s}",("i",crowdfund_contract.id)("s",crowdfund_contract.state));
              if(crowdfund_contract.state != CROWDFUND_STATE_USED) 
              {
                  total_V += crowdfund_contract.valuation;
                  total_supply += crowdfund_contract.balance.amount.value;
                  db.adjust_balance(crowdfund_contract.owner,crowdfund_contract.balance);
              }

              itr++;     
              db.remove(crowdfund_contract);

      }
      ilog("sum:${s}, V:${v}",("s",total_V)("v",crowdfund.V));
      //
      //  crowdfund.V to owner
      //
      asset native_asset;
      native_asset.asset_id=asset_id_type(0);
      native_asset.amount  = crowdfund.V;
      db.adjust_balance(crowdfund.owner,native_asset);

      //update asset dynamic data current_supply
      db.modify( dyn_data,[&](asset_dynamic_data_object &dyn_data) {
                     dyn_data.current_supply = total_supply;
      });
    
}

void block_callback::auto_withdraw(database &db,const crowdfund_object & crowdfund) const
{
      const auto & crowdfund_asset = db.get(crowdfund.asset_id);  

      const auto id = crowdfund.id;
      const auto& crowdfund_contract_idx = db.get_index_type<crowdfund_contract_index>();
      const auto& by_crowdfund_idx = crowdfund_contract_idx.indices().get<by_crowdfund>();

      auto itr = by_crowdfund_idx.lower_bound( id );
      auto end = by_crowdfund_idx.lower_bound( id+1 );
      
      while( itr != end )
      {
         //
         // list out B1,B2,...,Bk
         //
         //  skip "used"
         while( itr != end )
         {
              if(itr->state != CROWDFUND_STATE_USED) break;
              itr++; 
         }

         if (itr==end ){
               break;
         }

         auto c_B1 = itr->cap;

         ilog("V:${v},c(B1):${b}",("v",crowdfund.V)("b",c_B1));
 
         //  V>c(B)
         if(crowdfund.V<=c_B1)
         {
              break;
         }

         // while there exists an actice B whose personal cap is exceded by present crowdsale valuation.

         auto itr0 = itr;
         share_type S;
         int k;
         k=0;
         S=0;
         do{
             S.value += itr->valuation.value;
             k++;
             itr++;
         } while( itr != end && itr->cap==c_B1);

         ilog("V:${v},k:${k} S:${S},c(B1):${b}",("v",crowdfund.V)("k",k)("S",S)("b",c_B1));
         //
         //  V-S<c(B1)? 
         //  Yes,  kick away and refund all k pariticipators.
         if(crowdfund.V -S >= c_B1)
         {
              ilog("refund and kick");
              while(itr0!=itr) {
                  
                  asset delta;
                  delta.asset_id=asset_id_type(0);
                  delta.amount.value=itr0->valuation.value;
                  db.adjust_balance(itr0->owner,delta);

                  ilog("refund owner:${o} b:${b} v:${v}",("o",itr0->owner)("b",delta)("v",itr0->valuation));
 
                  db.modify( *itr0,[&](crowdfund_contract_object &c ){
                       c.state=CROWDFUND_STATE_USED;
                       c.balance.amount.value=0;
                  });
                  itr0++;
              }
              db.modify( crowdfund ,[&](crowdfund_object &c ){
                       c.V -= S;
              });
         } 
         //
         // refund q *V(Bi)  
         //
         else // V-S < c(B1)
         {
              float q= (float)(crowdfund.V-c_B1 + k).value/S.value;
              share_type total_delta_V=0;

              ilog("refund q:${q}\n",("q",q));

              while(itr0!=itr) {
                  asset delta;
                  delta.asset_id=asset_id_type(0);
                  delta.amount.value=itr0->valuation.value*q;
 
                 if(delta.amount.value<1)
                  {
                       delta.amount.value=1;
                  }

                  db.adjust_balance(itr0->owner,delta);
                  total_delta_V += delta.amount.value;

                  share_type delta_b=itr0->balance.amount.value * q;
                  if(delta_b.value<1)
                  {
                        delta_b.value=1;
                  }

                  ilog("refund owner:${o},b:${b} v:${v}",("o",itr0->owner)("b",delta)("v",itr0->valuation));

                  // v(Bi)=(1-q)v(Bi),b(Bi)=(1-q)b(Bi)
                  db.modify( *itr0,[&](crowdfund_contract_object &c ){
                       c.valuation.value      -= delta.amount.value;
                       c.balance.amount.value -= delta_b.value;
                  });
                  itr0++;
               }
              db.modify( crowdfund ,[&](crowdfund_object &c ){
                       c.V -= total_delta_V;
              });
           
           }

      }
}


}}
