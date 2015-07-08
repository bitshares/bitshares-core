
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

struct predicate_validator
{
   typedef void result_type;

   template<typename T>
   void operator()( const T& p )const
   {
      p.validate();
   }
};

void assert_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   for( const auto& item : predicates )
      item.visit( predicate_validator() );
}

/**
 * The fee for assert operations is proportional to their size,
 * but cheaper than a data fee because they require no storage
 */
share_type  assert_operation::calculate_fee(const fee_parameters_type& k)const
{
   return k.fee * predicates.size();
}


} } }
