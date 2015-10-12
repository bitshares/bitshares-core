/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/db/object.hpp>

namespace graphene { namespace chain {
   using namespace graphene::db;

   /**
    *  @brief tracks minimal information about past blocks to implement TaPOS
    *  @ingroup object
    *
    *  When attempting to calculate the validity of a transaction we need to
    *  lookup a past block and check its block hash and the time it occurred
    *  so we can calculate whether the current transaction is valid and at
    *  what time it should expire.
    */
   class block_summary_object : public abstract_object<block_summary_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_block_summary_object_type;

         block_id_type      block_id;
   };

} }

FC_REFLECT_DERIVED( graphene::chain::block_summary_object, (graphene::db::object), (block_id) )
