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
#include <boost/test/unit_test_monitor.hpp>
#include <chrono>
#include <string>

#include <fc/exception/exception.hpp>

/**
 * Report escaped fc exceptions instead of swallowing them.
 *
 * Boost.Test recognises exceptions derived from std::exception and reports their what().
 * fc::exception derives from nothing, so anything escaping a test case falls into Boost's
 * catch-all, which reports the bare placeholder:
 *
 *     unknown location(0): fatal error: in "<some test>": unknown type
 *
 * No code, no message, no location -- and the exception's own text is never written
 * anywhere, so it cannot be recovered from the log afterwards either. Two entirely
 * different causes produce that identical line, which makes the report misleading rather
 * than merely thin.
 *
 * The translator turns it into the detail string: code, message and throw site.
 */
inline void translate_fc_exception( const fc::exception& e )
{
   BOOST_FAIL( "fc::exception: " + e.to_detail_string() );
}

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
   boost::unit_test::unit_test_monitor.register_exception_translator<fc::exception>(
      &translate_fc_exception );
   return nullptr;
}
