
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/operations.hpp>
#include <graphene/chain/predicate.hpp>

namespace graphene { namespace chain { namespace pred {

bool account_name_eq_lit::validate()const
{
   return is_valid_name( name );
}

bool account_name_eq_lit::evaluate( const database& db )const
{
   return account_id(db).name == name;
}

bool asset_symbol_eq_lit::validate()const
{
   return is_valid_symbol( symbol );
}

bool asset_symbol_eq_lit::evaluate( const database& db )const
{
   return asset_id(db).symbol == symbol;
}

} } }
