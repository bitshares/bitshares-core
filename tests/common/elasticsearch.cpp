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

#include "elasticsearch.hpp"

#include <fc/exception/exception.hpp>
#include <fc/io/json.hpp>
#include <fc/time.hpp>
#include <fc/variant_object.hpp>

static size_t curl_write_function(void *contents, size_t size, size_t nmemb, void *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

namespace graphene { namespace utilities {

bool checkES(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + "_nodes";
   curl_request.auth = es.auth;
   curl_request.type = "GET";

   if(doCurl(curl_request).empty())
      return false;
   return true;
}

std::string getESVersion(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url;
   curl_request.auth = es.auth;
   curl_request.type = "GET";

   fc::variant response = fc::json::from_string(doCurl(curl_request));

   return response["version"]["number"].as_string();
}

void checkESVersion7OrAbove(ES& es, bool& result) noexcept
{
   static const int64_t version_7 = 7;
   try {
      const auto es_version = graphene::utilities::getESVersion(es);
      ilog( "ES version detected: ${v}", ("v", es_version) );
      auto dot_pos = es_version.find('.');
      result = ( std::stoi(es_version.substr(0,dot_pos)) >= version_7 );
   }
   catch( ... )
   {
      wlog( "Unable to get ES version, assuming it is 7 or above" );
      result = true;
   }
}

std::string simpleQuery(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.endpoint;
   curl_request.auth = es.auth;
   curl_request.type = "POST";
   curl_request.query = es.query;

   return doCurl(curl_request);
}

bool deleteAll(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.index_prefix + "*";
   curl_request.auth = es.auth;
   curl_request.type = "DELETE";

   auto curl_response = doCurl(curl_request);
   if( curl_response.empty() )
   {
      wlog( "Empty ES response" );
      return false;
   }

   // Check errors in response
   try
   {
      fc::variant j = fc::json::from_string(curl_response);
      if( j.is_object() && j.get_object().contains("error") )
      {
         wlog( "ES returned an error: ${r}", ("r", curl_response) );
         return false;
      }
   }
   catch( const fc::exception& e )
   {
      wlog( "Error while checking ES response ${r}", ("r", curl_response) );
      wdump( (e.to_detail_string()) );
      return false;
   }
   return true;
}

std::string getEndPoint(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.endpoint;
   curl_request.auth = es.auth;
   curl_request.type = "GET";

   return doCurl(curl_request);
}

std::string doCurl(CurlRequest& curl)
{
   std::string CurlReadBuffer;
   struct curl_slist *headers = NULL;
   headers = curl_slist_append(headers, "Content-Type: application/json");

   // Note: the variable curl.handler has a long lifetime, it only gets initialized once, then be used many times,
   //       thus we need to clear old data
   curl_easy_setopt(curl.handler, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(curl.handler, CURLOPT_URL, curl.url.c_str());
   curl_easy_setopt(curl.handler, CURLOPT_CUSTOMREQUEST, curl.type.c_str()); // this is OK
   if(curl.type == "POST")
   {
      curl_easy_setopt(curl.handler, CURLOPT_HTTPGET, false);
      curl_easy_setopt(curl.handler, CURLOPT_POST, true);
      curl_easy_setopt(curl.handler, CURLOPT_POSTFIELDS, curl.query.c_str());
   }
   else // GET or DELETE (only these are used in this file)
   {
      curl_easy_setopt(curl.handler, CURLOPT_POSTFIELDS, NULL);
      curl_easy_setopt(curl.handler, CURLOPT_POST, false);
      curl_easy_setopt(curl.handler, CURLOPT_HTTPGET, true);
   }
   curl_easy_setopt(curl.handler, CURLOPT_WRITEFUNCTION, curl_write_function);
   curl_easy_setopt(curl.handler, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl.handler, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!curl.auth.empty())
      curl_easy_setopt(curl.handler, CURLOPT_USERPWD, curl.auth.c_str());
   curl_easy_perform(curl.handler);

   long code;
   curl_easy_getinfo( curl.handler, CURLINFO_RESPONSE_CODE, &code );

   if( 200 != code )
      wlog( "doCurl response [${code}] ${msg}", ("code", ((int64_t)code))("msg", CurlReadBuffer) );

   return CurlReadBuffer;
}

} } // graphene::utilities
