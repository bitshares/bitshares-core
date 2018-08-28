/*
 * Copyright (c) 2018 oxarbitrage, and contributors.
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
#include <cstddef>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <fc/time.hpp>
#include <fc/variant_object.hpp>

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

namespace graphene { namespace utilities {

   class ES {
      public:
         CURL *curl;
         std::vector <std::string> bulk_lines;
         std::string elasticsearch_url;
         std::string index_prefix;
         std::string auth;
         std::string endpoint;
         std::string query;
   };
   class CurlRequest {
      public:
         CURL *handler;
         std::string url;
         std::string type;
         std::string auth;
         std::string query;
   };

   bool SendBulk(ES&& es);
   const std::vector<std::string> createBulk(const fc::mutable_variant_object& bulk_header, std::string&& data);
   bool checkES(ES& es);
   const std::string simpleQuery(ES& es);
   bool deleteAll(ES& es);
   bool handleBulkResponse(long http_code, const std::string& CurlReadBuffer);
   const std::string getEndPoint(ES& es);
   const std::string generateIndexName(const fc::time_point_sec& block_date, const std::string& _elasticsearch_index_prefix);
   const std::string doCurl(CurlRequest& curl);
   const std::string joinBulkLines(const std::vector<std::string>& bulk);
   long getResponseCode(CURL *handler);

} } // end namespace graphene::utilities
