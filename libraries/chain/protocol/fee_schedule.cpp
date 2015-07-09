#include <graphene/chain/protocol/fee_schedule.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace chain {

   typedef fc::smart_ref<fee_schedule> smart_fee_schedule;

   fee_schedule::fee_schedule()
   {
   }

   fee_schedule fee_schedule::get_default()
   {
      fee_schedule result;
      for( uint32_t i = 0; i < fee_parameters().count(); ++i )
      {
         fee_parameters x; x.set_which(i);
         result.parameters.insert(x);
      }
      return result;
   }

   struct fee_schedule_validate_visitor
   {
      typedef void result_type;

      template<typename T>
      void operator()( const T& p )const
      {
         //p.validate();
      }
   };

   void fee_schedule::validate()const
   {
      for( const auto& f : parameters )
         f.visit( fee_schedule_validate_visitor() );
   }

   struct calc_fee_visitor
   {
      typedef uint64_t result_type;

      const fee_parameters& param;
      calc_fee_visitor( const fee_parameters& p ):param(p){}

      template<typename OpType>
      result_type operator()(  const OpType& op )const
      {
         return op.calculate_fee( param.get<typename OpType::fee_parameters_type>() ).value;
      }
   };

   struct set_fee_visitor
   {
      typedef void result_type;
      asset _fee;

      set_fee_visitor( asset f ):_fee(f){}

      template<typename OpType>
      void operator()( OpType& op )const
      {
         op.fee = _fee;
      }
   };

   struct zero_fee_visitor
   {
      typedef void result_type;

      template<typename ParamType>
      result_type operator()(  ParamType& op )const
      {
         memset( (char*)&op, 0, sizeof(op) );
      }
   };

   void fee_schedule::zero_all_fees()
   {
      *this = get_default();
      for( fee_parameters& i : parameters )
         i.visit( zero_fee_visitor() );
   }

   asset fee_schedule::calculate_fee( const operation& op, const price& core_exchange_rate )const
   {
      //idump( (op)(core_exchange_rate) );
      fee_parameters params; params.set_which(op.which());
      auto itr = parameters.find(params);
      if( itr != parameters.end() ) params = *itr;
      //idump( (params) );
      auto base_value = op.visit( calc_fee_visitor( params ) );
      auto scaled = fc::uint128(base_value) * scale;
      scaled /= GRAPHENE_100_PERCENT;
      FC_ASSERT( scaled <= GRAPHENE_MAX_SHARE_SUPPLY );
      auto result = asset( scaled.to_uint64(), 0 ) * core_exchange_rate;
      FC_ASSERT( result.amount <= GRAPHENE_MAX_SHARE_SUPPLY );
      return result;
   }

   asset fee_schedule::set_fee( operation& op, const price& core_exchange_rate )const
   {
      auto f = calculate_fee( op, core_exchange_rate );
      op.visit( set_fee_visitor( f ) );
      return f;
   }

} } // graphene::chain
