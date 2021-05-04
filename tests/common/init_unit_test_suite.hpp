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
#pragma once

#include <cstdlib>
#include <iostream>
#include <boost/test/included/unit_test.hpp>
#include <chrono>
#include <string>

uint32_t    GRAPHENE_TESTING_GENESIS_TIMESTAMP = 1431700000;
std::string GRAPHENE_TESTING_ES_URL            = "http://127.0.0.1:9200/";

boost::unit_test::test_suite* init_unit_test_suite(int argc, char* argv[]) {
   const auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
   std::srand( seed );
   std::cout << "Random number generator seeded to " << seed << std::endl;
   const char* genesis_timestamp_str = getenv("GRAPHENE_TESTING_GENESIS_TIMESTAMP");
   if( genesis_timestamp_str != nullptr )
   {
      GRAPHENE_TESTING_GENESIS_TIMESTAMP = std::stoul( genesis_timestamp_str );
   }
   std::cout << "GRAPHENE_TESTING_GENESIS_TIMESTAMP is " << GRAPHENE_TESTING_GENESIS_TIMESTAMP << std::endl;
   const char* env_es_url = getenv("GRAPHENE_TESTING_ES_URL");
   if( env_es_url != nullptr )
   {
      std::string tmp_es_url( env_es_url );
      if( tmp_es_url.substr(0, 7) == "http://" || tmp_es_url.substr(0, 8) == "https://" )
         GRAPHENE_TESTING_ES_URL = tmp_es_url;
   }
   std::cout << "GRAPHENE_TESTING_ES_URL is " << GRAPHENE_TESTING_ES_URL << std::endl;
   return nullptr;
}
