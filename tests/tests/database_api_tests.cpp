/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

#include <graphene/app/database_api.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE(database_api_tests, database_fixture)

  BOOST_AUTO_TEST_CASE(is_registered) {
      try {
          /***
           * Arrange
           */
          auto nathan_private_key = generate_private_key("nathan");
          public_key_type nathan_public = nathan_private_key.get_public_key();

          auto dan_private_key = generate_private_key("dan");
          public_key_type dan_public = dan_private_key.get_public_key();

          auto unregistered_private_key = generate_private_key("unregistered");
          public_key_type unregistered_public = unregistered_private_key.get_public_key();


          /***
           * Act
           */
          create_account("dan", dan_private_key.get_public_key()).id;
          create_account("nathan", nathan_private_key.get_public_key()).id;
          // Unregistered key will not be registered with any account


          /***
           * Assert
           */
          graphene::app::database_api db_api(db);

          BOOST_CHECK(db_api.is_public_key_registered((string) nathan_public));
          BOOST_CHECK(db_api.is_public_key_registered((string) dan_public));
          BOOST_CHECK(!db_api.is_public_key_registered((string) unregistered_public));

      } FC_LOG_AND_RETHROW()
  }

BOOST_AUTO_TEST_SUITE_END()
