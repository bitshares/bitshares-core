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

#include <graphene/persistent_proposals/persistent_proposals_plugin.hpp>

#include <graphene/persistent_proposals/persistent_proposals_objects.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <graphene/db/object.hpp>

using graphene::chain::primary_index;
using graphene::chain::proposal_index;
using graphene::chain::proposal_object;
using graphene::chain::index_observer;
using graphene::db::object;

namespace graphene { namespace persistent_proposals {

namespace detail {

class proposal_index_observer: public index_observer
{
public:
    proposal_index_observer( graphene::chain::database* database )
    : _database(database)
    {}

    virtual void on_add( const object& obj ) override
    {
        const auto& proposal = dynamic_cast<const proposal_object&>(obj);

        auto& proposal_update_base_by_original_id = _database->get_index_type<proposal_update_base_index>().indices().get<by_original_id>();

        auto old_proposal_update_it = proposal_update_base_by_original_id.find(proposal.id);
        if (old_proposal_update_it != proposal_update_base_by_original_id.end())
        {
            _database->remove(*old_proposal_update_it);
        }

        create_update_base(proposal);
    }

    virtual void on_modify( const object& obj ) override
    {
        const auto& proposal = dynamic_cast<const proposal_object&>(obj);

        auto& proposal_update_base_by_original_id = _database->get_index_type<proposal_update_base_index>().indices().get<by_original_id>();
        auto proposal_update_base_it = proposal_update_base_by_original_id.find(proposal.id);
        if (proposal_update_base_it == proposal_update_base_by_original_id.end())
        {
            create_update_base(proposal);
        }

        _database->create<proposal_update_object>([&](proposal_update_object& update) {
            update.proposal = proposal.id;

            update.added_owner_approvals = get_difference(proposal.available_owner_approvals, proposal_update_base_it->available_owner_approvals);
            update.removed_owner_approvals = get_difference(proposal_update_base_it->available_owner_approvals, proposal.available_owner_approvals);
            update.added_active_approvals = get_difference(proposal.available_active_approvals, proposal_update_base_it->available_active_approvals);
            update.removed_active_approvals = get_difference(proposal_update_base_it->available_active_approvals, proposal.available_active_approvals);
            update.added_key_approvals = get_difference(proposal.available_key_approvals, proposal_update_base_it->available_key_approvals);
            update.removed_key_approvals = get_difference(proposal_update_base_it->available_key_approvals, proposal.available_key_approvals);

            update.update_time = fc::time_point::now();
        });

        _database->modify(*proposal_update_base_it, [&](proposal_update_base_object& update_base) {
            update_base.original_id = proposal.id;

            duplicate_proposal(proposal, &update_base);
        });
    }

    virtual void on_remove( const object& obj ) override
    {
        const auto& proposal = dynamic_cast<const proposal_object&>(obj);
        if (!is_expired(proposal) && !proposal.is_authorized_to_execute(*_database))
        {
            return;
        }

        auto& persistent_proposal_by_original_id = _database->get_index_type<persistent_proposal_index>().indices().get<by_original_id>();

        auto old_persistent_proposal_it = persistent_proposal_by_original_id.find(proposal.id);
        if (old_persistent_proposal_it != persistent_proposal_by_original_id.end())
        {
            _database->remove(*old_persistent_proposal_it);
        }

        _database->create<persistent_proposal_object>([&](persistent_proposal_object& persistent_proposal) {
            persistent_proposal.original_id = proposal.id;

            duplicate_proposal(proposal, &persistent_proposal);
        });
    }

private:
    bool is_expired(const proposal_object& proposal)
    {
        return proposal.expiration_time <= _database->head_block_time();
    }

    template <class T>
    flat_set<T> get_difference(const flat_set<T>& left, const flat_set<T>& right)
    {
        flat_set<T> difference;

        for (const auto& item: left)
        {
            if (right.count(item) == 0)
            {
                difference.insert(item);
            }
        }

        return difference;
    }

    void duplicate_proposal(const proposal_object& original, proposal_object* duplicate)
    {
        duplicate->expiration_time = original.expiration_time;
        duplicate->review_period_time = original.review_period_time;
        duplicate->proposed_transaction = original.proposed_transaction;
        duplicate->required_active_approvals = original.required_active_approvals;
        duplicate->available_active_approvals = original.available_active_approvals;
        duplicate->required_owner_approvals = original.required_owner_approvals;
        duplicate->available_owner_approvals = original.available_owner_approvals;
        duplicate->available_key_approvals = original.available_key_approvals;
    }

    void create_update_base(const proposal_object& proposal)
    {
        _database->create<proposal_update_base_object>([&](proposal_update_base_object& update_base) {
            update_base.original_id = proposal.id;

            duplicate_proposal(proposal, &update_base);
        });
    }

private:
    graphene::chain::database* _database;
};

}

persistent_proposals_plugin::persistent_proposals_plugin()
{}

persistent_proposals_plugin::~persistent_proposals_plugin()
{}

void persistent_proposals_plugin::plugin_set_program_options(
   boost::program_options::options_description& command_line_options,
   boost::program_options::options_description& config_file_options)
{}

std::string persistent_proposals_plugin::plugin_name()const
{
   return "persistent_proposals";
}

void persistent_proposals_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{}

void persistent_proposals_plugin::plugin_startup()
{
    ilog("persistent_proposals_plugin::plugin_startup()");

    database().add_index<primary_index<persistent_proposal_index>>();
    database().add_index<primary_index<proposal_update_index>>();
    database().add_index<primary_index<proposal_update_base_index>>();

    std::shared_ptr<index_observer> observer(new detail::proposal_index_observer(&database()));
    auto& mutable_proposal_index = const_cast<primary_index<proposal_index>&>(database().get_index_type<primary_index<proposal_index>>());
    mutable_proposal_index.add_observer(observer);
}

void persistent_proposals_plugin::plugin_shutdown()
{
    ilog("persistent_proposals_plugin::plugin_shutdown()");
}

} }
