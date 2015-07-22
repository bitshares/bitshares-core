#pragma once
#include <graphene/chain/evaluator.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

/**
 * @class blinded_balance_object
 * @brief tracks a blinded balance commitment
 * @ingroup object
 * @ingroup protocol
 */
class blinded_balance_object : public graphene::db::abstract_object<blinded_balance_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id  = impl_blinded_balance_object_type;

      fc::ecc::commitment_type                commitment;
      asset_id_type                           asset_id;
      authority                               owner;
};

struct by_asset;
struct by_owner;
struct by_commitment;

/**
 * @ingroup object_index
 */
typedef multi_index_container<
   blinded_balance_object,
   indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_unique< tag<by_commitment>, member<blinded_balance_object, commitment_type, &blinded_balance_object::commitment> >
   >
> blinded_balance_object_multi_index_type;
typedef generic_index<blinded_balance_object, blinded_balance_object_multi_index_type> blinded_balance_index;


class transfer_to_blind_evaluator : public evaluator<transfer_to_blind_evaluator>
{
   public:
      typedef transfer_to_blind_operation operation_type;

      void_result do_evaluate( const transfer_to_blind_operation& o );
      void_result do_apply( const transfer_to_blind_operation& o ) ;
};

class transfer_from_blind_evaluator : public evaluator<transfer_from_blind_evaluator>
{
   public:
      typedef transfer_from_blind_operation operation_type;

      void_result do_evaluate( const transfer_from_blind_operation& o );
      void_result do_apply( const transfer_from_blind_operation& o ) ;
};

class blind_transfer_evaluator : public evaluator<blind_transfer_evaluator>
{
   public:
      typedef blind_transfer_operation operation_type;

      void_result do_evaluate( const blind_transfer_operation& o );
      void_result do_apply( const blind_transfer_operation& o ) ;
};

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::blinded_balance_object, (commitment)(asset_id)(owner) )
