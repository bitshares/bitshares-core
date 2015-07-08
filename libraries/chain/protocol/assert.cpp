#include <graphene/chain/protocol/protocol.hpp>

namespace graphene { namespace chain {

bool account_name_eq_lit_predicate::validate()const
{
   return is_valid_name( name );
}

bool asset_symbol_eq_lit_predicate::validate()const
{
   return is_valid_symbol( symbol );
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


} }  // namespace graphene::chain
