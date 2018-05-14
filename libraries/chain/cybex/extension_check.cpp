
#include <graphene/chain/account_object.hpp>
#include "cybex/extensions.hpp"

namespace graphene { namespace chain {


void cybex_ext_vesting_check(const account_object & acc, const cybex_ext_vesting & ext1) 
{


   fc::ecc::public_key pk = fc::ecc::public_key((fc::ecc::public_key_data)ext1.public_key);

   // check whether the receiver has the given public key
   bool found=false;
   for( auto k:acc.owner.key_auths)
   {
       found = pk == k.first.operator fc::ecc::public_key();
       if ( found )break;
   }
   for( auto k:acc.active.key_auths)
   {
       if ( found )break;
       found = pk == k.first.operator fc::ecc::public_key();
   }

   FC_ASSERT( found,"${a} does not have the given public key",("a",acc.name));
}

} }
