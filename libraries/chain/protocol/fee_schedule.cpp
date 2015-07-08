#include <graphene/chain/fee_schedule.hpp>

namespace graphene { namespace chain {

   fee_schedule::fee_schedule()
   {
   }

   fee_schedule fee_schedule::get_default()
   {
      fee_schedule result;
      for( uint32_t i = 0; i < fee_parameters.count(); ++i )
      {
         fee_parameters x; x.set_which(i);
         result.parameters.insert(x);
      }
      return result;
   }

   struct fee_schedule_validate_visitor
   {
      typedef result_type void;

      template<typename T>
      void operator()( const T& p )const
      {
         p.validate();
      }
   };

   void fee_schedule::validate()const
   {
      for( const auto& f : parameters )
         f.visit( fee_schedule_validate_visitor() );
   }

   struct calc_fee_visitor
   {
      typedef result_type asset;

      const fee_parameters& param;
      calc_fee_visitor( const fee_parameters& p ):param(p){}

      template<typename OpType>
      asset operator()(  const OpType& op )const
      {
         return op.calculate_fee( param.get<typename OpType::fee_parameters_type>() );
      }
   };

   asset fee_schedule::calculate_fee( const operation& op, const price& core_exchange_rate )const
   {
      fee_parameters params; params.set_which(op.which());
      auto itr = parameters.find(params);
      if( itr != parameters.end() ) params = *itr;
      share_type base_value op.visit( calc_fee_visitor( params ) );
      auto scaled = fc::uint128(base_value.value) * scale_factor;
      scaled /= GRAPHENE_100_PERCENT;
      FC_ASSERT( scaled <= GRAPHENE_MAX_SHARE_SUPPLY );
      auto result = asset( scaled.to_uint64(), 0 ) * core_exchange_rate;
      FC_ASSERT( result.amount <= GRAPHENE_MAX_SHARE_SUPPLY );
      return result;
   }

} } // graphene::chain
