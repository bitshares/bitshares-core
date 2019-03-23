/*
 * Copyright (c) 2018 Bitshares Foundation, and contributors.
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

#include <chrono>
#include <graphene/chain/hardfork.hpp>
#include "../common/database_fixture.hpp"

using namespace graphene::chain;

BOOST_FIXTURE_TEST_CASE(mfs_performance_test, database_fixture)
{
   try
   {
      ACTORS((issuer));

      const unsigned int accounts = 3000;
      const unsigned int iterations = 20;

      std::vector<account_object> registrators;
      for (unsigned int i = 0; i < accounts; ++i)
      {
         auto account = create_account("registrar" + std::to_string(i));
         transfer(committee_account, account.get_id(), asset(1000000));
         upgrade_to_lifetime_member(account);

         registrators.push_back(std::move(account));
      }

      generate_blocks(HARDFORK_1268_TIME);
      generate_block();

      additional_asset_options_t options;
      options.value.reward_percent = 2 * GRAPHENE_1_PERCENT;

      const auto usd = create_user_issued_asset(
                  "USD",
                  issuer,
                  charge_market_fee,
                  price(asset(1, asset_id_type(1)), asset(1)),
                  1,
                  20 * GRAPHENE_1_PERCENT,
                  options);

      issue_uia(issuer, usd.amount(iterations * accounts * 2000));

      std::vector<account_object> traders;
      for (unsigned int i = 0; i < accounts; ++i)
      {
         std::string name = "account" + std::to_string(i);
         auto account = create_account(name, registrators[i], registrators[i], GRAPHENE_1_PERCENT);
         transfer(committee_account, account.get_id(), asset(1000000));
         transfer(issuer, account, usd.amount(iterations * 2000));

         traders.push_back(std::move(account));
      }

      using namespace std::chrono;

      const auto start = high_resolution_clock::now();

      for (unsigned int i = 0; i < iterations; ++i)
      {
         for (unsigned int j = 0; j < accounts; ++j)
         {
            create_sell_order(traders[j], usd.amount(2000), asset(1));
            create_sell_order(traders[accounts - j - 1], asset(1), usd.amount(1000));
         }
      }

      const auto end = high_resolution_clock::now();

      const auto elapsed = duration_cast<milliseconds>(end - start);
      wlog("Elapsed: ${c} ms", ("c", elapsed.count()));

      for (unsigned int i = 0; i < accounts; ++i)
      {
         const auto reward = get_market_fee_reward(registrators[i], usd);
         BOOST_CHECK_GT(reward, 0);
      }
   }
   FC_LOG_AND_RETHROW()
}
