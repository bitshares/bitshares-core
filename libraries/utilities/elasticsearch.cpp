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
#include <fc/log/logger.hpp>

namespace graphene { namespace utilities {

bool SendBulk(CURL *curl, std::vector<std::string>& bulk, std::string elasticsearch_url, bool do_logs, std::string logs_index)
{
  // curl buffers to read
  std::string readBuffer;
  std::string readBuffer_logs;

  std::string bulking = "";

  bulking = boost::algorithm::join(bulk, "\n");
  bulking = bulking + "\n";
  bulk.clear();

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  std::string url = elasticsearch_url + "_bulk";
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, true);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bulking.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&readBuffer);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
  //curl_easy_setopt(curl, CURLOPT_VERBOSE, true);
  curl_easy_perform(curl);

  long http_code = 0;
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
   if(http_code == 200) {
      // all good, do nothing
   }
   else if(http_code == 413) {
      elog("413 error: Can be low space disk");
      return 0;
   }
   else {
      elog(std::to_string(http_code) + " error: Unknown error");
      return 0;
   }

  if(do_logs) {
    auto logs = readBuffer;
    // do logs
    std::string url_logs = elasticsearch_url + logs_index + "/data/";
    curl_easy_setopt(curl, CURLOPT_URL, url_logs.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, true);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, logs.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &readBuffer_logs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcrp/0.1");
    curl_easy_perform(curl);

    http_code = 0;
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    if(http_code == 200) {
      // all good, do nothing
       return 1;
    }
    else if(http_code == 201) {
       // 201 is ok
       return 1;
    }
    else if(http_code == 409) {
       // 409 for record already exist is ok
       return 1;
    }
    else if(http_code == 413) {
       elog("413 error: Can be low space disk");
       return 0;
    }
    else {
       elog(std::to_string(http_code) + " error: Unknown error");
       return 0;
    }
  }
  return 0;
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


} } // end namespace graphene::utilities
