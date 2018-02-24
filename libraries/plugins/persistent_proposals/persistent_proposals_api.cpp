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

#include <graphene/persistent_proposals/persistent_proposals_api.hpp>

#include <graphene/persistent_proposals/persistent_proposals_plugin.hpp>
#include <graphene/persistent_proposals/persistent_proposals_objects.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/app/application.hpp>

namespace graphene { namespace persistent_proposals {

namespace detail {

class persistent_proposals_api_impl
{
public:
    persistent_proposals_api_impl( graphene::app::application& app )
    : _database(app.chain_database().get())
    {}

    vector<proposal_object> get_proposed_transactions( object_id_type start, unsigned int limit )const
    {
        return get_proposed_transactions_impl(start, limit, [](const proposal_object&){return true;});
    }

    vector<proposal_object> get_proposed_transactions_for_account( account_id_type account_id, object_id_type start, unsigned int limit )const
    {
        return get_proposed_transactions_impl(start, limit,
                                              [&](const proposal_object& proposal) {
                                                  return does_proposal_belong_to_account(proposal, account_id);
                                              });
    }

    vector<proposal_update_object> get_proposal_updates( object_id_type proposal_id, object_id_type start, unsigned limit )const
    {
        FC_ASSERT(limit <= 100, "You may request at most 100 proposal updates at a time.");

        vector<proposal_update_object> proposal_updates;

        auto& proposal_updates_by_proposal_id = _database->get_index_type<proposal_update_index>().indices().get<by_proposal_id>();
        proposal_update_multi_index_container::index<by_proposal_id>::type::iterator iter;
        if (start == object_id_type())
        {
            iter = proposal_updates_by_proposal_id.lower_bound(std::make_tuple(proposal_id));
        }
        else
        {
            iter = proposal_updates_by_proposal_id.lower_bound(std::make_tuple(proposal_id, start));
        }


        while (iter != proposal_updates_by_proposal_id.end() &&
               iter->proposal == proposal_id &&
               proposal_updates.size() < limit)
        {
            proposal_updates.push_back(*iter);

            ++iter;
        }

        return proposal_updates;
    }

private:
    template <class Predicate>
    vector<proposal_object> get_proposed_transactions_impl( object_id_type start, unsigned int limit, const Predicate& can_proposal_be_saved )const
    {
        FC_ASSERT(limit <= 100, "You may request at most 100 proposals at a time.");

        const auto& proposals_by_id = _database->get_index_type<proposal_index>().indices().get<by_id>();
        vector<proposal_object> proposals = get_proposals<proposal_object>(proposals_by_id, start, limit, can_proposal_be_saved);

        const auto& persistent_proposals_by_original_id = _database->get_index_type<persistent_proposal_index>().indices().get<by_original_id>();
        vector<persistent_proposal_object> expired_proposals = get_proposals<persistent_proposal_object>(persistent_proposals_by_original_id, start, limit, can_proposal_be_saved);
        map_original_ids_to_proposal_id(&expired_proposals);

        proposals.insert(proposals.end(), expired_proposals.begin(), expired_proposals.end());

        std::sort(proposals.begin(), proposals.end(), [](const proposal_object& left, const proposal_object& right) {
            return left.id < right.id;
        });

        if (proposals.size() > limit)
        {
            proposals.resize(limit);
        }

        return proposals;
    }

    static bool does_proposal_belong_to_account( const proposal_object& proposal, account_id_type account_id )
    {
        if( proposal.required_active_approvals.find( account_id ) != proposal.required_active_approvals.end() ||
            proposal.required_owner_approvals.find( account_id ) != proposal.required_owner_approvals.end() ||
            proposal.available_active_approvals.find( account_id ) != proposal.available_active_approvals.end() )
        {
            return true;
        }

        return false;
    }

    template <class T, class Container, class Predicate>
    static vector<T> get_proposals( const Container& proposals_by_id, object_id_type start, unsigned int limit, const Predicate& can_proposal_be_saved )
    {
        vector<T> result;

        auto proposal_it = proposals_by_id.begin();
        if (start != object_id_type())
        {
            proposal_it = proposals_by_id.lower_bound(start);
        }

        while (result.size() < limit)
        {
            if (proposal_it == proposals_by_id.end())
            {
                break;
            }

            if (can_proposal_be_saved(*proposal_it))
            {
                result.insert(result.end(), *proposal_it);
            }

            proposal_it++;
        }

        return result;
    }

    //this is needed to make expired proposals have ids like ones they have before expiration
    static void map_original_ids_to_proposal_id( vector<persistent_proposal_object>* proposals )
    {
        for ( auto& proposal : *proposals )
        {
            proposal.id = proposal.original_id;
        }
    }

private:
    graphene::chain::database* _database;
};

} //details

persistent_proposals_api::persistent_proposals_api( graphene::app::application& app )
: _my(new detail::persistent_proposals_api_impl(app))
{}

vector<proposal_object> persistent_proposals_api::get_proposed_transactions( object_id_type start, unsigned int limit )const
{
    return _my->get_proposed_transactions(start, limit);
}

vector<proposal_object> persistent_proposals_api::get_proposed_transactions_for_account( account_id_type account_id, object_id_type start, unsigned int limit )const
{
    return _my->get_proposed_transactions_for_account(account_id, start, limit);
}

vector<proposal_update_object> persistent_proposals_api::get_proposal_updates( object_id_type proposal_id, object_id_type start, unsigned int limit )const
{
    return _my->get_proposal_updates(proposal_id, start, limit);
}

} } //graphene::persistent_proposals
