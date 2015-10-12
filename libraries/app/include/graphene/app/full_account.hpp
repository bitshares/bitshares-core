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

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/market_evaluator.hpp>

namespace graphene { namespace app {
   using namespace graphene::chain;

   struct full_account
   {
      account_object                   account;
      account_statistics_object        statistics;
      string                           registrar_name;
      string                           referrer_name;
      string                           lifetime_referrer_name;
      vector<variant>                  votes;
      optional<vesting_balance_object> cashback_balance;
      vector<account_balance_object>   balances;
      vector<vesting_balance_object>   vesting_balances;
      vector<limit_order_object>       limit_orders;
      vector<call_order_object>        call_orders;
      vector<proposal_object>          proposals;
   };

} }

FC_REFLECT( graphene::app::full_account, 
            (account)
            (statistics)
            (registrar_name)
            (referrer_name)
            (lifetime_referrer_name)
            (votes)
            (cashback_balance)
            (balances)
            (vesting_balances)
            (limit_orders)
            (call_orders)
            (proposals) 
          )
