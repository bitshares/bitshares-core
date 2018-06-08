#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <graphene/chain/balance_object.hpp>
#include <cybex/block_callback.hpp>


#include <fc/io/fstream.hpp>

namespace graphene {
namespace chain {
uint64_t block_callback::snapshot_at_block_num;
uint8_t block_callback::snapshot_in_day;
uint8_t block_callback::snapshot_in_hour;
uint8_t block_callback::snapshot_in_minute;

vector<address>   get_account_address(const account_object & account)
{
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

   return addrs;
}

void block_callback::snapshot(database &db)
{
   fc::time_point_sec now = db.head_block_time();
   uint64_t block_num =db.head_block_num();

   time_t t = (time_t) now.sec_since_epoch();

   struct tm tm = *localtime(&t);
   bool new_snapshot_done;
   bool do_snapshot = false;
  
    
   if ( tm.tm_mday==snapshot_in_day || (tm.tm_hour == snapshot_in_hour && tm.tm_min == snapshot_in_minute ))
   {
       if( !snapshot_done)
       {
           new_snapshot_done=true;  
           do_snapshot = true;       
       }
   }
   else 
   {
       snapshot_done=false;
   }

   if(block_num==snapshot_at_block_num)
   {
        new_snapshot_done = snapshot_done;
        do_snapshot = true;       
   }
  

   if(do_snapshot )
   {
        char buffer[32];
     
        snprintf(buffer,sizeof(buffer),"%d-%02d-%02d_%lu.json", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,block_num);
        fc::ofstream                   out;
        auto path = db.get_data_dir().parent_path()/string(buffer);

        out.open( path, std::ios_base::out | std::ios_base::app );

        if(!fc::exists(path))
        {
          elog("failed to create account balance snapshot.\n");
          return;        
        }


        const auto& account_idx = db.get_index_type<account_index>().indices().get<by_id>();
        auto& bal_index = db.get_index_type<account_balance_index>().indices().get<by_account_asset>();
        auto& bal_obj_index = db.get_index_type<balance_index>().indices().get<by_owner>();
        auto& vb_obj_index = db.get_index_type<vesting_balance_index>().indices().get<by_account>();
        auto asset_id = asset_id_type(0);

         bool first = true;
         strftime(buffer, sizeof(buffer)-1, "%Y %m %d %H:%M:%S", &tm);
         
         out << "{\"timestamp\":\"" << std::string(buffer)<<"\",\n";
         out << "\"block\":" << block_num <<",\n";
         out << "\"data\":[";
        for( const account_object& acct : account_idx )
        {
          if(acct.get_id().instance<6) continue;

          if(!first){ out <<","; } else first=false;
          out << "\n{ \"account\": \"" << acct.name << "\",\n \"account-balance-objects\":["; 
          auto itr = bal_index.lower_bound(boost::make_tuple(acct.get_id(), asset_id));
          bool first_line=true;
          while(itr !=bal_index.end()&&itr->owner==acct.get_id())
          {
            if(!first_line){ out <<","; } else first_line=false;
            out  << "\n\"" << db.to_pretty_string(asset(itr->balance.value,itr->asset_type)) << "\"";
            //out  << "   " << db.get(itr->asset_type).symbol  << " " << itr->balance.value << "\n";
      
            itr++;
          }
          out << "],\n\"vested-balance-objects\":[";
          first_line=true;
          auto vb_itr = vb_obj_index.find(acct.get_id());
          while(vb_itr !=vb_obj_index.end()&&vb_itr->owner==acct.get_id())
          {
            if(!first_line){ out <<","; } else first_line=false;
            out  << "\n\"" << db.to_pretty_string(vb_itr->balance);
            
            if(vb_itr->policy.which()==1)
            {
                cdd_vesting_policy policy=vb_itr->policy.get<cdd_vesting_policy>();
                out << " 1" 
                    << " " << policy.start_claim.to_iso_string() 
                    << " " << policy.vesting_seconds; 
            }  
            else if (vb_itr->policy.which()==0 ) {
                linear_vesting_policy policy=vb_itr->policy.get<linear_vesting_policy>();
                out << " 0" 
                    << " " << policy.begin_timestamp.to_iso_string() 
                    << " " << policy.vesting_cliff_seconds
                    << " " << policy.vesting_duration_seconds; 
            }  
            else
            {
                out << " " << vb_itr->policy.which(); 
            }
            out << "\"";
            vb_itr++;
          }
          out << "],\n\"balance-objects\":[";
          first_line=true;
          vector<address> addrs=get_account_address(acct);

          for( address addr : addrs)
          {
            
            auto itr = bal_obj_index.lower_bound(boost::make_tuple(addr, asset_id));

            if(itr ==bal_obj_index.end()||itr->owner!=addr)
            {
                   continue;
            }
            if(!first_line){ out <<","; } else first_line=false;
            out  << "\n{\"address\":\"" << (string)addr << "\",\n\"balance-objects\":[";
            bool line_first=true;
            while(itr !=bal_obj_index.end()&&itr->owner==addr)
            {
               if(!line_first){ out <<","; } else line_first=false;
               out  << "\n\"" << db.to_pretty_string(itr->balance);
               if(itr->vesting_policy.valid()) 
               { 
                  out  << " "    << itr->vesting_policy->begin_timestamp.to_iso_string()  
                       << " "    << itr->vesting_policy->vesting_cliff_seconds;
               }
               out     << "\""; 
              itr++;
            }
            out <<"]\n}";
          
          }
          out <<"]\n}";

        }         
        out <<"],\n";

        const auto& order_idx = db.get_index_type<limit_order_index>().indices().get<by_id>();
     
        out << "\"orders\":[";
        bool line_first=true;
        for( auto itr = order_idx.begin();itr!=order_idx.end();itr++)
        { 
              if(!line_first){ out <<"\"],"; } else line_first=false;
              out << "\n[ \"" << itr->seller(db).name.c_str();
              out << "\",\""  << db.to_pretty_string(itr->amount_for_sale()).c_str();
              out << "\",\""  << db.to_pretty_string(itr->amount_to_receive()).c_str();
              out << "\",\""  << itr->expiration.to_iso_string();
        }
        if(!line_first){ out <<"\"]"; } 
        
        out << "\n]\n}";
        out.flush();
        out.close();
        snapshot_done=new_snapshot_done;
      
    
   } 
}

}}
