#pragma once
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain { namespace cybex {

extern graphene::chain::database* _db;

static inline graphene::chain::database & database(){  return  *_db; }

static inline void init(graphene::chain::database * db){_db=db;}

} } }
