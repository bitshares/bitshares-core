#include <cybex/block_callback.hpp>



namespace graphene {
namespace chain {


void block_callback::handler(database &db)
{
      process_crowdfund(db);
      snapshot(db);
}

}}
