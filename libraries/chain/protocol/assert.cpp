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
