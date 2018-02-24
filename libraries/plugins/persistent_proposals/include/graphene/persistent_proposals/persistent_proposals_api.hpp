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

#include <vector>
#include <memory>

#include <graphene/chain/proposal_object.hpp>
#include <graphene/persistent_proposals/persistent_proposals_objects.hpp>
#include <fc/api.hpp>

//This one is needed to omit strange compilation issue in "return vector<proposal_object>();" expression
#include <graphene/chain/protocol/fee_schedule.hpp>

namespace graphene { namespace app {
    class application;
} }

namespace graphene { namespace persistent_proposals {

    using namespace graphene::chain;
    using namespace std;

    namespace detail {
        class persistent_proposals_api_impl;
    }

    class persistent_proposals_api
    {
    public:
        persistent_proposals_api( graphene::app::application& app );

        /**
         * Get the proposed transactions for all accounts. 'limit' max allowed value is 100.
         */
        vector<proposal_object> get_proposed_transactions( object_id_type start = object_id_type(), unsigned int limit = 100 )const;

        /**
         * Get the proposed transactions for account id. 'limit' max allowed value is 100.
         */
        vector<proposal_object> get_proposed_transactions_for_account( account_id_type account_id, object_id_type start = object_id_type(), unsigned int limit = 100 )const;

        /**
         * Get the proposed transactions updates for proposal id. 'limit' max allowed value is 100.
         */
        vector<proposal_update_object> get_proposal_updates( object_id_type proposal_id, object_id_type start = object_id_type(), unsigned int limit = 100 )const;

    private:
        std::shared_ptr< detail::persistent_proposals_api_impl > _my;
    };

} }

FC_API(graphene::persistent_proposals::persistent_proposals_api,
       (get_proposed_transactions)
       (get_proposed_transactions_for_account)
       (get_proposal_updates)
       )

