/*
 * Copyright (c) 2015 Blockchain Projects BV, and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <graphene/chain/proposal_object.hpp>

namespace graphene { namespace persistent_proposals {

using namespace chain;

enum spaces
{
    persistent_proposal_objects = 7
};

enum persistent_proposal_object_type
{
    persistent_proposal_object_type,
    proposal_update_object_type,
    proposal_update_base_object_type,
    PERSISTENT_PROPOSAL_OBJECT_TYPE_COUNT ///< Sentry value which contains the number of different object types
};

class proposal_update_object : public abstract_object<proposal_object>
{
public:
    static const uint8_t space_id = persistent_proposal_objects;
    static const uint8_t type_id = proposal_update_object_type;

    proposal_id_type           proposal;
    flat_set<account_id_type>  added_active_approvals;
    flat_set<account_id_type>  removed_active_approvals;
    flat_set<account_id_type>  added_owner_approvals;
    flat_set<account_id_type>  removed_owner_approvals;
    flat_set<public_key_type>  added_key_approvals;
    flat_set<public_key_type>  removed_key_approvals;

    time_point_sec             update_time;
};

struct by_proposal_id{};

typedef boost::multi_index_container< proposal_update_object,
                                      indexed_by< ordered_unique< tag< by_id >, member< object, object_id_type, &object::id >>,
                                                  ordered_unique< tag< by_proposal_id >,
                                                                  composite_key< proposal_update_object,
                                                                                 member< proposal_update_object, proposal_id_type, &proposal_update_object::proposal >,
                                                                                 member< object, object_id_type, &object::id > >,
                                                                  composite_key_compare< std::less<proposal_id_type>,
                                                                                         std::less<object_id_type>
                                                                                       >>>>
                    proposal_update_multi_index_container;

typedef generic_index<proposal_update_object, proposal_update_multi_index_container> proposal_update_index;

class persistent_proposal_object: public proposal_object
{
public:
    static const uint8_t space_id = persistent_proposal_objects;
    static const uint8_t type_id = persistent_proposal_object_type::persistent_proposal_object_type;

    object_id_type original_id;
};

struct by_original_id{};

typedef boost::multi_index_container< persistent_proposal_object,
                                      indexed_by<ordered_unique< tag< by_id >, member< object, object_id_type, &object::id >>,
                                                 ordered_unique< tag< by_original_id >, member< persistent_proposal_object, object_id_type, &persistent_proposal_object::original_id >>>>
        persistent_proposal_multi_index_container;

typedef generic_index<persistent_proposal_object, persistent_proposal_multi_index_container> persistent_proposal_index;

class proposal_update_base_object: public proposal_object
{
public:
    static const uint8_t space_id = persistent_proposal_objects;
    static const uint8_t type_id = persistent_proposal_object_type::proposal_update_base_object_type;

    object_id_type original_id;
};

typedef boost::multi_index_container< proposal_update_base_object,
                                      indexed_by<ordered_unique< tag< by_id >, member< object, object_id_type, &object::id >>,
                                                 ordered_unique< tag< by_original_id >, member< proposal_update_base_object, object_id_type, &proposal_update_base_object::original_id >>>>
        proposal_update_base_multi_index_container;

typedef generic_index<proposal_update_base_object, proposal_update_base_multi_index_container> proposal_update_base_index;

} } //graphene::persistent_proposals

FC_REFLECT_DERIVED( graphene::persistent_proposals::persistent_proposal_object, (graphene::chain::proposal_object), (original_id))
FC_REFLECT_DERIVED( graphene::persistent_proposals::proposal_update_object, (graphene::chain::object),
                   (proposal)
                   (added_active_approvals)
                   (removed_active_approvals)
                   (added_owner_approvals)
                   (removed_owner_approvals)
                   (added_key_approvals)
                   (removed_key_approvals)
                   (update_time)
                   )
FC_REFLECT_DERIVED( graphene::persistent_proposals::proposal_update_base_object, (graphene::chain::proposal_object), (original_id))
