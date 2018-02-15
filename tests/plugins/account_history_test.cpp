/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/account_history/account_history_plugin.hpp>

#define BOOST_TEST_MODULE Test Application
#include <boost/test/included/unit_test.hpp>

/**
 * Create an account_history_plugin with some options
 */
BOOST_AUTO_TEST_CASE( options_account_history )
{
      BOOST_TEST_MESSAGE( "Creating account_history plugin" );

      graphene::account_history::account_history_plugin plugin;
      graphene::app::application app;
      plugin.plugin_set_app(&app);

      BOOST_TEST_MESSAGE("Add some options");
      boost::program_options::variables_map options;
      options.emplace("track-account", boost::program_options::variable_value(std::vector<std::string>{"\"1.2.1\"", "\"1.2.2\""}, false));
      options.emplace("partial-operations", boost::program_options::variable_value(std::to_string(true), false));
      options.emplace("max-ops-per-account", boost::program_options::variable_value(std::to_string(12), false));
      BOOST_TEST_MESSAGE("Initialize the plugin");
      plugin.plugin_initialize(options);
      BOOST_TEST_MESSAGE("Retrieve the results");
      boost::program_options::variables_map results = plugin.plugin_get_options();

      BOOST_TEST_MESSAGE("Check the results");

      std::cout << "Here are the results:" << std::endl;
      BOOST_CHECK_EQUAL(results["tracked-accounts"].as<std::string>(), "1.2.1, 1.2.2");
      BOOST_CHECK_EQUAL(results["partial-operations"].as<std::string>(), "1");
      BOOST_CHECK_EQUAL(results["max-ops-per-account"].as<std::string>(), "12");

      BOOST_TEST_MESSAGE("Test Complete");
}
