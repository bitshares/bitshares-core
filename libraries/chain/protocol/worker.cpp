#include <graphene/chain/protocol/worker.hpp>

namespace graphene { namespace chain {

void worker_create_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(work_end_date > work_begin_date);
   FC_ASSERT(daily_pay > 0);
   FC_ASSERT(daily_pay < GRAPHENE_MAX_SHARE_SUPPLY);
   FC_ASSERT(name.size() < GRAPHENE_MAX_WORKER_NAME_LENGTH );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

} }
