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

#include <boost/test/unit_test.hpp>

#include <graphene/persistent_proposals/persistent_proposals_api.hpp>
#include <graphene/persistent_proposals/persistent_proposals_plugin.hpp>
#include <graphene/persistent_proposals/persistent_proposals_objects.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;

namespace
{
    struct persistent_proposals_fixture: public database_fixture
    {
        persistent_proposals_fixture()
        : persistent_proposals_api(app)
        {
            auto persistent_proposals_plugin = app.register_plugin<graphene::persistent_proposals::persistent_proposals_plugin>();

            persistent_proposals_plugin->plugin_set_app(&app);

            boost::program_options::variables_map options;
            persistent_proposals_plugin->plugin_initialize(options);
            persistent_proposals_plugin->plugin_startup();

            graphene::persistent_proposals::persistent_proposals_api persistent_proposals_api(app);

            nathan_key = generate_private_key("nathan");
            nathan = &create_account("nathan", nathan_key.get_public_key() );

            dan_key = generate_private_key("dan");
            dan = &create_account("dan", dan_key.get_public_key() );

            sam_key = generate_private_key("sam");
            sam = &create_account("sam", sam_key.get_public_key() );

            const int asset_count = 100000;
            transfer(account_id_type()(db), *nathan, asset(asset_count));
            transfer(account_id_type()(db), *dan, asset(asset_count));
            transfer(account_id_type()(db), *sam, asset(asset_count));
        }

        void propose_transfer( const account_object& from, const account_object& to, const private_key_type& to_key, const fc::microseconds& expiration_delay )
        {
            transfer_operation top;
            top.from = from.get_id();
            top.to = to.get_id();
            top.amount = asset(500);

            proposal_create_operation pop;
            pop.proposed_ops.emplace_back(top);
            std::swap(top.from, top.to);
            pop.proposed_ops.emplace_back(top);

            pop.fee_paying_account = to.get_id();
            pop.expiration_time = db.head_block_time() + expiration_delay;
            trx.operations.push_back(pop);
            sign( trx, to_key );
            PUSH_TX( db, trx );
            trx.clear();
        }

        void add_active_approvals_to_proposal(const object_id_type& proposal_id,
                                              const account_id_type& approval,
                                              const account_id_type& fee_paying_account,
                                              const vector<private_key_type>& sign_keys)
        {
            proposal_update_operation uop;
            uop.proposal = proposal_id;
            uop.active_approvals_to_add.insert(approval);
            uop.fee_paying_account = fee_paying_account;
            trx.operations.push_back(uop);

            for (auto& sign_key : sign_keys)
            {
                sign( trx, sign_key );
            }

            PUSH_TX( db, trx );
            trx.clear();
        }

        fc::ecc::private_key nathan_key;
        const account_object* nathan;

        fc::ecc::private_key sam_key;
        const account_object* sam;

        fc::ecc::private_key dan_key;
        const account_object* dan;

        graphene::persistent_proposals::persistent_proposals_api persistent_proposals_api;
    };
}

BOOST_FIXTURE_TEST_SUITE( persistent_proposals_api_tests, persistent_proposals_fixture )

BOOST_AUTO_TEST_CASE(get_proposed_transactions_no_transactions) {
    try {
        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK(proposals.empty());

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_normal_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal = proposals.front();
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);

        BOOST_CHECK(!proposal.is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_expired_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));

        //waiting for expiration of proposals
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal = proposals.front();
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.size(), 2);

        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);

        BOOST_CHECK(!proposal.is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_no_transactions) {
    try {
        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account( sam->get_id() );
        BOOST_CHECK(proposals.empty());

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_normal_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal = proposals.front();
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.size(), 2);

        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);

        BOOST_CHECK(!proposal.is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_expired_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));

        //waiting for expiration of proposals
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        auto sam_proposals = persistent_proposals_api.get_proposed_transactions_for_account(sam->get_id());
        BOOST_CHECK_EQUAL(sam_proposals.size(), 1);

        auto sam_proposal = sam_proposals.front();
        BOOST_CHECK_EQUAL(sam_proposal.required_active_approvals.size(), 2);

        BOOST_CHECK_EQUAL(sam_proposal.required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(sam_proposal.required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(sam_proposal.required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposal.available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposal.available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposal.available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposal.required_owner_approvals.size(), 0);

        BOOST_CHECK(!sam_proposal.is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_several_expired_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*sam, *dan, dan_key, fc::seconds(1));

        //waiting for expiration of proposals
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 3);

        auto sam_proposals = persistent_proposals_api.get_proposed_transactions_for_account(sam->get_id());
        BOOST_CHECK_EQUAL(sam_proposals.size(), 2);

        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(sam_proposals[0].required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_owner_approvals.size(), 0);

        BOOST_CHECK(!sam_proposals[0].is_authorized_to_execute(db));


        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(sam_proposals[1].required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_owner_approvals.size(), 0);

        BOOST_CHECK(!sam_proposals[1].is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_several_not_expired_transactions) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*dan, *sam, sam_key, fc::days(1));
        propose_transfer(*sam, *dan, dan_key, fc::seconds(1));

        //waiting for expiration of proposals
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 3);

        auto sam_proposals = persistent_proposals_api.get_proposed_transactions_for_account(sam->get_id());
        BOOST_CHECK_EQUAL(sam_proposals.size(), 2);

        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(sam_proposals[0].required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[0].required_owner_approvals.size(), 0);

        BOOST_CHECK(!sam_proposals[0].is_authorized_to_execute(db));


        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(sam_proposals[1].required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(sam_proposals[1].required_owner_approvals.size(), 0);

        BOOST_CHECK(!sam_proposals[1].is_authorized_to_execute(db));

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_for_empty_account) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(sam->get_id());
        BOOST_CHECK(proposals.empty());

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_updates_for_not_modified_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK(proposal_updates.empty());

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_updates_for_not_modified_expired_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));

        //waiting for expiration of proposals
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK(proposal_updates.empty());

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_for_modified_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        proposal_update_operation uop;
        uop.fee_paying_account = nathan->get_id();
        uop.proposal = proposals.front().id;
        uop.owner_approvals_to_add.insert(nathan->get_id());
        trx.operations.push_back(uop);
        sign( trx, nathan_key );
        PUSH_TX( db, trx );

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 1);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_for_several_modifications) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        add_active_approvals_to_proposal(proposals.front().id, nathan->get_id(), nathan->get_id(), {nathan_key});
        add_active_approvals_to_proposal(proposals.front().id, dan->get_id(), nathan->get_id(), {nathan_key, dan_key});

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 2);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);

        BOOST_CHECK(proposal_updates[1].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_approved_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        add_active_approvals_to_proposal(proposals.front().id, nathan->get_id(), nathan->get_id(), {nathan_key});
        add_active_approvals_to_proposal(proposals.front().id, dan->get_id(), nathan->get_id(), {nathan_key, dan_key});

        //trigger proposal approving
        generate_block();

        proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal = proposals.front();
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal.required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal.available_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal.available_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal.available_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.available_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal.available_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal.required_owner_approvals.size(), 0);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_of_expired_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        add_active_approvals_to_proposal(proposals.front().id, nathan->get_id(), nathan->get_id(), {nathan_key});

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        //fetch expired proposal
        proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 1);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_of_expired_proposal_of_account) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        add_active_approvals_to_proposal(proposals.front().id, nathan->get_id(), nathan->get_id(), {nathan_key});

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        //fetch expired proposal
        proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 1);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_of_mulitple_updates) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        const auto& proposal = db.get<proposal_object>(proposals.front().id);
        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(nathan->get_id());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(nathan->get_id());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_owner_approvals.insert(dan->get_id());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_owner_approvals.erase(dan->get_id());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_key_approvals.insert(dan_key.get_public_key());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_key_approvals.erase(dan_key.get_public_key());
        });

        //fetch expired proposal
        proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 6);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[1].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[2].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[2].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[2].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[2].added_owner_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[2].added_owner_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[2].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[2].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[2].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[3].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[3].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[3].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[3].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[3].removed_owner_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[3].removed_owner_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[3].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[3].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[4].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[4].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[4].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[4].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[4].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[4].added_key_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[4].added_key_approvals.count(dan_key.get_public_key()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[4].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[5].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[5].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[5].removed_active_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[5].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[5].removed_owner_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[5].added_key_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[5].removed_key_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[5].removed_key_approvals.count(dan_key.get_public_key()), 1);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_for_complex_modification) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        const auto& proposal = db.get<proposal_object>(proposals.front().id);
        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(nathan->get_id());
            proposal.available_active_approvals.insert(dan->get_id());
            proposal.available_owner_approvals.insert(dan->get_id());

            proposal.available_key_approvals.insert(dan_key.get_public_key());
            proposal.available_key_approvals.insert(nathan_key.get_public_key());
        });

        db.modify(proposal, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(nathan->get_id());
            proposal.available_active_approvals.erase(dan->get_id());
            proposal.available_owner_approvals.erase(dan->get_id());

            proposal.available_key_approvals.erase(dan_key.get_public_key());
            proposal.available_key_approvals.erase(nathan_key.get_public_key());
        });

        //fetch expired proposal
        proposals = persistent_proposals_api.get_proposed_transactions_for_account(dan->get_id());
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposals.front().id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 2);

        BOOST_CHECK(proposal_updates[0].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.count(dan_key.get_public_key()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.count(nathan_key.get_public_key()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[1].proposal == proposals.front().id);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.count(dan_key.get_public_key()), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.count(nathan_key.get_public_key()), 1);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_for_concrete_proposal) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        const auto& proposal1 = db.get<proposal_object>(proposals[0].id);
        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(nathan->get_id());
        });

        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(nathan->get_id());
        });

        const auto& proposal2 = db.get<proposal_object>(proposals[1].id);
        db.modify(proposal2, [&]( proposal_object& proposal ){
            proposal.available_owner_approvals.insert(dan->get_id());
        });

        db.modify(proposal2, [&]( proposal_object& proposal ){
            proposal.available_owner_approvals.erase(dan->get_id());
        });

        //
        // proposal1 updates check
        //

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposal1.id);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 2);

        BOOST_CHECK(proposal_updates[0].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[1].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 3);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));
        propose_transfer(*dan, *sam, sam_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions(proposals.back().id, 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(dan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_expired_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 3);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_expired_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions(proposals.back().id, 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_expired_and_normal_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*nathan, *sam, sam_key, fc::seconds(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 10);
        BOOST_CHECK_EQUAL(proposals.size(), 4);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.count(sam->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_expired_and_normal_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*nathan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions(object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions(proposals.back().id, 3);
        BOOST_CHECK_EQUAL(proposals.size(), 3);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(sam->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));
        propose_transfer(*dan, *sam, sam_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 3);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::days(1));
        propose_transfer(*nathan, *sam, sam_key, fc::days(1));
        propose_transfer(*dan, *sam, sam_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), proposals.back().id, 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_expired_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 3);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_expired_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*nathan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), proposals.back().id, 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_expired_n_normal_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *dan, dan_key, fc::days(1));
        propose_transfer(*nathan, *sam, sam_key, fc::days(1));
        propose_transfer(*nathan, *dan, dan_key, fc::days(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposals.size(), 2);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), proposals.back().id, 3);
        BOOST_CHECK_EQUAL(proposals.size(), 3);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(sam->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(dan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposed_transactions_for_account_expired_n_normal_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*dan, *sam, sam_key, fc::seconds(1));
        propose_transfer(*sam, *nathan, nathan_key, fc::seconds(1));
        propose_transfer(*sam, *dan, dan_key, fc::days(1));
        propose_transfer(*nathan, *sam, sam_key, fc::days(1));
        propose_transfer(*nathan, *dan, dan_key, fc::days(1));

        //wait for proposal expiration
        fc::usleep(fc::seconds(2));

        //trigger transactions clearing
        generate_block();

        auto proposals = persistent_proposals_api.get_proposed_transactions_for_account(nathan->get_id(), object_id_type(), 10);
        BOOST_CHECK_EQUAL(proposals.size(), 4);

        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(dan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[0].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[1].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(sam->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[2].required_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.size(), 2);
        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.count(nathan->get_id()), 1);
        BOOST_CHECK_EQUAL(proposals[3].required_active_approvals.count(dan->get_id()), 1);

    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_paged_request_more_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        const auto& proposal1 = db.get<proposal_object>(proposals[0].id);
        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(nathan->get_id());
        });

        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(nathan->get_id());
        });

        //
        // proposal1 updates check
        //

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposal1.id, object_id_type(), 20);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 2);

        BOOST_CHECK(proposal_updates[0].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[1].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_CASE(get_proposal_updates_paged_request_less_than_exist) {
    try {
        propose_transfer(*dan, *nathan, nathan_key, fc::days(1));

        auto proposals = persistent_proposals_api.get_proposed_transactions();
        BOOST_CHECK_EQUAL(proposals.size(), 1);

        const auto& proposal1 = db.get<proposal_object>(proposals[0].id);
        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(nathan->get_id());
        });

        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(nathan->get_id());
        });

        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.insert(dan->get_id());
        });

        db.modify(proposal1, [&]( proposal_object& proposal ){
            proposal.available_active_approvals.erase(dan->get_id());
        });

        auto proposal_updates = persistent_proposals_api.get_proposal_updates(proposal1.id, object_id_type(), 2);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 2);

        BOOST_CHECK(proposal_updates[0].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[1].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);

        proposal_updates = persistent_proposals_api.get_proposal_updates(proposal1.id, proposal_updates[1].id, 3);
        BOOST_CHECK_EQUAL(proposal_updates.size(), 3);

        BOOST_CHECK(proposal_updates[0].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_active_approvals.count(nathan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[0].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[0].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[0].removed_key_approvals.size(), 0);

        BOOST_CHECK(proposal_updates[1].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_active_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[1].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[1].removed_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[1].removed_key_approvals.size(), 0);


        BOOST_CHECK(proposal_updates[2].proposal == proposal1.id);

        BOOST_CHECK_EQUAL(proposal_updates[2].removed_active_approvals.size(), 1);
        BOOST_CHECK_EQUAL(proposal_updates[2].removed_active_approvals.count(dan->get_id()), 1);

        BOOST_CHECK_EQUAL(proposal_updates[2].added_active_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[2].added_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[2].added_key_approvals.size(), 0);

        BOOST_CHECK_EQUAL(proposal_updates[2].removed_owner_approvals.size(), 0);
        BOOST_CHECK_EQUAL(proposal_updates[2].removed_key_approvals.size(), 0);
    } catch (fc::exception &e) {
        edump((e.to_detail_string()));
        throw;
    }
}

BOOST_AUTO_TEST_SUITE_END()
