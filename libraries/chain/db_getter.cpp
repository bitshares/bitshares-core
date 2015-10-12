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

#include <graphene/chain/database.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/global_property_object.hpp>

namespace graphene { namespace chain {

const asset_object& database::get_core_asset() const
{
   return get(asset_id_type());
}

const global_property_object& database::get_global_properties()const
{
   return get( global_property_id_type() );
}

const chain_property_object& database::get_chain_properties()const
{
   return get( chain_property_id_type() );
}

const dynamic_global_property_object&database::get_dynamic_global_properties() const
{
   return get( dynamic_global_property_id_type() );
}

const fee_schedule&  database::current_fee_schedule()const
{
   return get_global_properties().parameters.current_fees;
}

time_point_sec database::head_block_time()const
{
   return get( dynamic_global_property_id_type() ).time;
}

uint32_t database::head_block_num()const
{
   return get( dynamic_global_property_id_type() ).head_block_number;
}

block_id_type database::head_block_id()const
{
   return get( dynamic_global_property_id_type() ).head_block_id;
}

decltype( chain_parameters::block_interval ) database::block_interval( )const
{
   return get_global_properties().parameters.block_interval;
}

const chain_id_type& database::get_chain_id( )const
{
   return get_chain_properties().chain_id;
}

const node_property_object& database::get_node_properties()const
{
   return _node_property_object;
}

node_property_object& database::node_properties()
{
   return _node_property_object;
}

uint32_t database::last_non_undoable_block_num() const
{
   return head_block_num() - _undo_db.size();
}


} }
