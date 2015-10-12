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

#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/protocol/vote.hpp>
#include <fc/variant.hpp>

namespace graphene { namespace chain {

vote_id_type get_next_vote_id( global_property_object& gpo, vote_id_type::vote_type type )
{
   return vote_id_type( type, gpo.next_available_vote_id++ );
}

} } // graphene::chain

namespace fc
{

void to_variant(const graphene::chain::vote_id_type& var, variant& vo)
{
   vo = string(var);
}

void from_variant(const variant& var, graphene::chain::vote_id_type& vo)
{
   vo = graphene::chain::vote_id_type(var.as_string());
}

} // fc
