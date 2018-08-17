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
#include <graphene/utilities/elasticsearch.hpp>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/json.hpp>

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
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
const std::string simpleQuery(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.endpoint;
   curl_request.auth = es.auth;
   curl_request.type = "POST";
   curl_request.query = es.query;

   return doCurl(curl_request);
}

bool SendBulk(ES&& es)
{
   std::string bulking = joinBulkLines(es.bulk_lines);

   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + "_bulk";
   curl_request.auth = es.auth;
   curl_request.type = "POST";
   curl_request.query = std::move(bulking);

   auto curlResponse = doCurl(curl_request);

   if(handleBulkResponse(getResponseCode(curl_request.handler), curlResponse))
      return true;
   return false;
}

const std::string joinBulkLines(const std::vector<std::string>& bulk)
{
   auto bulking = boost::algorithm::join(bulk, "\n");
   bulking = bulking + "\n";

   return bulking;
}
long getResponseCode(CURL *handler)
{
   long http_code = 0;
   curl_easy_getinfo (handler, CURLINFO_RESPONSE_CODE, &http_code);
   return http_code;
}

bool handleBulkResponse(long http_code, const std::string& CurlReadBuffer)
{
   if(http_code == 200) {
      // all good, but check errors in response
      fc::variant j = fc::json::from_string(CurlReadBuffer);
      bool errors = j["errors"].as_bool();
      if(errors == true) {
         return false;
      }
   }
   else {
      if(http_code == 413) {
         elog( "413 error: Can be low disk space" );
      }
      else if(http_code == 401) {
         elog( "401 error: Unauthorized" );
      }
      else {
         elog( std::to_string(http_code) + " error: Unknown error" );
      }
      return false;
   }
   return true;
}

const std::vector<std::string> createBulk(const fc::mutable_variant_object& bulk_header, std::string&& data)
{
   std::vector<std::string> bulk;
   fc::mutable_variant_object final_bulk_header;
   final_bulk_header["index"] = bulk_header;
   bulk.push_back(fc::json::to_string(final_bulk_header));
   bulk.push_back(data);

   return bulk;
}

bool deleteAll(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.index_prefix + "*";
   curl_request.auth = es.auth;
   curl_request.type = "DELETE";

   auto curl_response = doCurl(curl_request);
   if(curl_response.empty())
      return false;
   else
      return true;
}
const std::string getEndPoint(ES& es)
{
   graphene::utilities::CurlRequest curl_request;
   curl_request.handler = es.curl;
   curl_request.url = es.elasticsearch_url + es.endpoint;
   curl_request.auth = es.auth;
   curl_request.type = "GET";

   return doCurl(curl_request);
}

const std::string generateIndexName(const fc::time_point_sec& block_date, const std::string& _elasticsearch_index_prefix)
{
   auto block_date_string = block_date.to_iso_string();
   std::vector<std::string> parts;
   boost::split(parts, block_date_string, boost::is_any_of("-"));
   std::string index_name = _elasticsearch_index_prefix + parts[0] + "-" + parts[1];
   return index_name;
}

const std::string doCurl(CurlRequest& curl)
{
   std::string CurlReadBuffer;
   struct curl_slist *headers = NULL;
   headers = curl_slist_append(headers, "Content-Type: application/json");

   curl_easy_setopt(curl.handler, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(curl.handler, CURLOPT_URL, curl.url.c_str());
   curl_easy_setopt(curl.handler, CURLOPT_CUSTOMREQUEST, curl.type.c_str());
   if(curl.type == "POST")
   {
      curl_easy_setopt(curl.handler, CURLOPT_POST, true);
      curl_easy_setopt(curl.handler, CURLOPT_POSTFIELDS, curl.query.c_str());
   }
   curl_easy_setopt(curl.handler, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl.handler, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl.handler, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!curl.auth.empty())
      curl_easy_setopt(curl.handler, CURLOPT_USERPWD, curl.auth.c_str());
   curl_easy_perform(curl.handler);

   return CurlReadBuffer;
}

} } // end namespace graphene::utilities
