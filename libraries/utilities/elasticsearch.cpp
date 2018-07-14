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
   CURL *curl = es.curl;
   std::string elasticsearch_url = es.elasticsearch_url;
   std::string auth = es.auth;

   std::string CurlReadBuffer;
   std::string url = elasticsearch_url + "_nodes";
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!auth.empty())
      curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
   curl_easy_perform(curl);
   if(CurlReadBuffer.empty())
      return false;
   else
      return true;
}
std::string simpleQuery(ES& es)
{
   CURL *curl = es.curl;
   std::string elasticsearch_url = es.elasticsearch_url;
   std::string endpoint = es.endpoint;
   std::string query = es.query;
   std::string auth = es.auth;

   std::string CurlReadBuffer;
   struct curl_slist *headers = NULL;
   headers = curl_slist_append(headers, "Content-Type: application/json");
   std::string url = elasticsearch_url + endpoint;
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_POST, true);
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!auth.empty())
      curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
   curl_easy_perform(curl);

   if(!CurlReadBuffer.empty())
      return CurlReadBuffer;
   return "";
}

bool SendBulk(ES& es)
{
   CURL *curl = es.curl;
   std::vector<std::string>& bulk = es.bulk_lines;
   std::string elasticsearch_url = es.elasticsearch_url;
   std::string auth = es.auth;

   std::string CurlReadBuffer;

   std::string bulking = "";
   bulking = boost::algorithm::join(bulk, "\n");
   bulking = bulking + "\n";
   bulk.clear();

   struct curl_slist *headers = NULL;
   headers = curl_slist_append(headers, "Content-Type: application/json");
   std::string url = elasticsearch_url + "_bulk";
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_POST, true);
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bulking.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!auth.empty()) {
      curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
   }
   curl_easy_perform(curl);

   long http_code = 0;
   curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

   if(handleBulkResponse(http_code, CurlReadBuffer))
      return true;
   return false;
}

bool handleBulkResponse(long http_code, std::string CurlReadBuffer)
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

std::vector<std::string> createBulk(std::string index_name, std::string data, std::string id, bool onlycreate)
{
   std::vector<std::string> bulk;
   std::string create_string = "";
   if(!onlycreate)
      create_string = ",\"_id\" : "+id;

   bulk.push_back("{ \"index\" : { \"_index\" : \""+index_name+"\", \"_type\" : \"data\" "+create_string+" } }");
   bulk.push_back(data);
   return bulk;
}

bool deleteAll(ES& es)
{
   CURL *curl = es.curl;
   std::string elasticsearch_url = es.elasticsearch_url;
   std::string auth = es.auth;
   std::string index_prefix = es.index_prefix;

   std::string CurlReadBuffer;

   std::string url = elasticsearch_url + index_prefix + "*";
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!auth.empty())
      curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
   curl_easy_perform(curl);
   if(CurlReadBuffer.empty())
      return false;
   else
      return true;
}
std::string getEndPoint(ES& es)
{
   CURL *curl = es.curl;
   std::string elasticsearch_url = es.elasticsearch_url;
   std::string endpoint = es.endpoint;
   std::string auth = es.auth;

   std::string CurlReadBuffer;
   std::string url = elasticsearch_url + endpoint;
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlReadBuffer);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
   if(!auth.empty())
      curl_easy_setopt(curl, CURLOPT_USERPWD, auth.c_str());
   curl_easy_perform(curl);
   if(CurlReadBuffer.empty())
      return "";
   else
      return CurlReadBuffer;
}

std::string generateIndexName(fc::time_point_sec block_date, std::string _elasticsearch_index_prefix)
{
   auto block_date_string = block_date.to_iso_string();
   std::vector<std::string> parts;
   boost::split(parts, block_date_string, boost::is_any_of("-"));
   std::string index_name = _elasticsearch_index_prefix + parts[0] + "-" + parts[1];
   return index_name;
}

} } // end namespace graphene::utilities
