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
#include <fc/io/json.hpp>
#include <fc/exception/exception.hpp>

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

static bool handle_bulk_response( long http_code, const std::string& curl_read_buffer )
{
   if( 200 == http_code )
   {
      // all good, but check errors in response
      fc::variant j = fc::json::from_string(curl_read_buffer);
      bool errors = j["errors"].as_bool();
      if( errors )
      {
         elog( "ES returned 200 but with errors: ${e}", ("e", curl_read_buffer) );
         return false;
      }
      return true;
   }

   if( 413 == http_code )
   {
      elog( "413 error: Request too large. Can be low disk space. ${e}", ("e", curl_read_buffer) );
   }
   else if( 401 == http_code )
   {
      elog( "401 error: Unauthorized. ${e}", ("e", curl_read_buffer) );
   }
   else
   {
      elog( "${code} error: ${e}", ("code", std::to_string(http_code)) ("e", curl_read_buffer) );
   }
   return false;
}

static std::string joinBulkLines(const std::vector<std::string>& bulk)
{
   auto bulking = boost::algorithm::join(bulk, "\n");
   bulking = bulking + "\n";

   return bulking;
}

static long getResponseCode(CURL *handler)
{
   long http_code = 0;
   curl_easy_getinfo (handler, CURLINFO_RESPONSE_CODE, &http_code);
   return http_code;
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

   if(handle_bulk_response(getResponseCode(curl_request.handler), curlResponse))
      return true;
   return false;
}

std::vector<std::string> createBulk(const fc::mutable_variant_object& bulk_header, std::string&& data)
{
   std::vector<std::string> bulk;
   fc::mutable_variant_object final_bulk_header;
   final_bulk_header["index"] = bulk_header;
   bulk.push_back(fc::json::to_string(final_bulk_header));
   bulk.emplace_back(std::move(data));

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

   return CurlReadBuffer;
}

static CURL* init_curl()
{
   CURL* curl = curl_easy_init();
   if( curl )
   {
      curl_easy_setopt( curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 );
      return curl;
   }
   FC_THROW( "Unable to init cURL" );
}

static curl_slist* init_request_headers()
{
   curl_slist* request_headers = curl_slist_append( NULL, "Content-Type: application/json" );
   FC_ASSERT( request_headers, "Unable to init cURL request headers" );
   return request_headers;
}

curl_wrapper::curl_wrapper()
: curl( init_curl() ), request_headers( init_request_headers() )
{
   curl_easy_setopt( curl.get(), CURLOPT_HTTPHEADER, request_headers.get() );
   curl_easy_setopt( curl.get(), CURLOPT_USERAGENT, "bitshares-core/6.1" );
}

curl_wrapper::response curl_wrapper::request( curl_wrapper::http_request_method method,
                                              const std::string& url,
                                              const std::string& auth,
                                              const std::string& query ) const
{
   curl_wrapper::response resp;

   // Note: the variable curl has a long lifetime, it only gets initialized once, then be used many times,
   //       thus we need to clear old data

   // Note: host and auth are always the same in the program, ideally we don't need to set them every time
   curl_easy_setopt( curl.get(), CURLOPT_URL, url.c_str() );
   if( !auth.empty() )
      curl_easy_setopt( curl.get(), CURLOPT_USERPWD, auth.c_str() );

   // Empty for GET, POST or HEAD, non-empty for DELETE or PUT
   static const std::vector<std::string> http_request_method_custom_str = {
      "", // GET
      "", // POST
      "", // HEAD
      "PUT",
      "DELETE",
      "PATCH",
      "OPTIONS"
   };
   const auto& custom_request = http_request_method_custom_str[static_cast<size_t>(method)];
   const auto* p_custom_request = custom_request.empty() ? NULL : custom_request.c_str();
   curl_easy_setopt( curl.get(), CURLOPT_CUSTOMREQUEST, p_custom_request );

   if( curl_wrapper::http_request_method::_POST == method
       || curl_wrapper::http_request_method::_PUT == method )
   {
      curl_easy_setopt( curl.get(), CURLOPT_HTTPGET, false );
      curl_easy_setopt( curl.get(), CURLOPT_POST, true );
      curl_easy_setopt( curl.get(), CURLOPT_POSTFIELDS, query.c_str() );
   }
   else // GET or DELETE (only these are used in this file)
   {
      curl_easy_setopt( curl.get(), CURLOPT_POSTFIELDS, NULL );
      curl_easy_setopt( curl.get(), CURLOPT_POST, false );
      curl_easy_setopt( curl.get(), CURLOPT_HTTPGET, true );
   }

   curl_easy_setopt( curl.get(), CURLOPT_WRITEFUNCTION, curl_write_function );
   curl_easy_setopt( curl.get(), CURLOPT_WRITEDATA, (void *)(&resp.content) );
   curl_easy_perform( curl.get() );

   curl_easy_getinfo( curl.get(), CURLINFO_RESPONSE_CODE, &resp.code );

   return resp;
}

bool es_client::check_status() const
{
   const auto response = curl.get( base_url + "_nodes", auth );

   // Note: response.code is ignored here
   return !response.content.empty();
}

std::string es_client::get_version() const
{ try {
   const auto response = curl.get( base_url, auth );
   if( response.code != 200 )
      FC_THROW( "Error on es_client::get_version(): code = ${code}, message = ${message} ",
                ("code", int64_t(response.code)) ("message", response.content) );

   fc::variant content = fc::json::from_string( response.content );
   return content["version"]["number"].as_string();
} FC_CAPTURE_AND_RETHROW() }

void es_client::check_version_7_or_above( bool& result ) const noexcept
{
   static const int64_t version_7 = 7;
   try {
      const auto es_version = get_version();
      auto dot_pos = es_version.find('.');
      result = ( std::stoi(es_version.substr(0,dot_pos)) >= version_7 );
   }
   catch( ... )
   {
      wlog( "Unable to get ES version, assuming it is 7 or above" );
      result = true;
   }
}

bool es_client::send_bulk( const std::vector<std::string>& bulk_lines ) const
{
   auto bulk_str = boost::algorithm::join( bulk_lines, "\n" ) + "\n";
   const auto response = curl.post( base_url + "_bulk", auth, bulk_str );

   return handle_bulk_response( response.code, response.content );
}

bool es_client::del( const std::string& path ) const
{
   const auto response = curl.del( base_url + path, auth );

   // Note: response.code is ignored here
   return !response.content.empty();
}

std::string es_client::get( const std::string& path ) const
{
   const auto response = curl.get( base_url + path, auth );

   // Note: response.code is ignored here
   return response.content;
}

std::string es_client::query( const std::string& path, const std::string& query ) const
{
   const auto response = curl.post( base_url + path, auth, query );

   // Note: response.code is ignored here
   return response.content;
}

fc::variant es_data_adaptor::adapt(const fc::variant_object& op)
{
   fc::mutable_variant_object o(op);

   // Note: these fields are maps, but were stored in ES as flattened arrays
   static const std::unordered_set<std::string> flattened_fields = { "account_auths", "address_auths", "key_auths" };

   // Note:
   // object arrays listed in this map are stored redundantly in ES, with one instance as a nested object and
   //      the other as a string for backward compatibility,
   // object arrays not listed in this map are stored as nested objects only.
   static const std::unordered_map<std::string, data_type> to_string_fields = {
      { "parameters",               data_type::array_type }, // in committee proposals, current_fees.parameters
      { "op",                       data_type::static_variant_type }, // proposal_create_op.proposed_ops[*].op
      { "proposed_ops",             data_type::array_type },
      { "operations",               data_type::array_type }, // proposal_object.operations
      { "initializer",              data_type::static_variant_type },
      { "policy",                   data_type::static_variant_type },
      { "predicates",               data_type::array_type },
      { "active_special_authority", data_type::static_variant_type },
      { "owner_special_authority",  data_type::static_variant_type },
      { "htlc_preimage_hash",       data_type::static_variant_type },
      { "feeds",                    data_type::map_type }, // asset_bitasset_data_object.feeds
      { "acceptable_collateral",    data_type::map_type },
      { "acceptable_borrowers",     data_type::map_type }
   };
   std::vector<std::pair<std::string, fc::variants>> original_arrays;
   std::vector<std::string> keys_to_rename;
   for( auto& i : o )
   {
      const std::string& name = i.key();
      auto& element = i.value();
      if( element.is_object() )
      {
         const auto& vo = element.get_object();
         if( vo.contains(name.c_str()) ) // transfer_operation.amount.amount
            keys_to_rename.emplace_back(name);
         element = adapt(vo);
      }
      else if( element.is_array() )
      {
         auto& array = element.get_array();
         if( to_string_fields.find(name) != to_string_fields.end() )
         {
            // make a backup and convert to string
            original_arrays.emplace_back( name, array );
            element = fc::json::to_string(element);
         }
         else if( flattened_fields.find(name) != flattened_fields.end() )
         {
            // make a backup and adapt the original
            auto backup = array;
            original_arrays.emplace_back( name, backup );
            adapt(array);
         }
         else
            adapt(array);
      }
   }

   for( const auto& i : keys_to_rename ) // transfer_operation.amount
   {
      std::string new_name = i + "_";
      o[new_name] = fc::variant(o[i]);
      o.erase(i);
   }

   if( o.find("nonce") != o.end() )
   {
      o["nonce"] = o["nonce"].as_string();
   }

   if( o.find("owner") != o.end() && o["owner"].is_string() ) // vesting_balance_*_operation.owner
   {
      o["owner_"] = o["owner"].as_string();
      o.erase("owner");
   }

   for( const auto& pair : original_arrays )
   {
      const auto& name = pair.first;
      auto& value = pair.second;
      auto type = data_type::map_type;
      if( to_string_fields.find(name) != to_string_fields.end() )
         type =  to_string_fields.at(name);
      o[name + "_object"] = adapt( value, type );
   }

   fc::variant v;
   fc::to_variant(o, v, FC_PACK_MAX_DEPTH);
   return v;
}

fc::variant es_data_adaptor::adapt( const fc::variants& v, data_type type )
{
   if( data_type::static_variant_type == type )
      return adapt_static_variant(v);

   // map_type or array_type
   fc::variants vs;
   vs.reserve( v.size() );
   for( const auto& item : v )
   {
      if( item.is_array() )
      {
         if( data_type::map_type == type )
            vs.push_back( adapt_map_item( item.get_array() ) );
         else // assume it is a static_variant array
            vs.push_back( adapt_static_variant( item.get_array() ) );
      }
      else if( item.is_object() ) // object array
         vs.push_back( adapt( item.get_object() ) );
      else
         wlog( "Type of item is unexpected: ${item}", ("item", item) );
   }

   fc::variant nv;
   fc::to_variant(vs, nv, FC_PACK_MAX_DEPTH);
   return nv;
}

void es_data_adaptor::extract_data_from_variant(
      const fc::variant& v, fc::mutable_variant_object& mv, const std::string& prefix )
{
   if( v.is_object() )
      mv[prefix + "_object"] = adapt( v.get_object() );
   else if( v.is_int64() || v.is_uint64() )
      mv[prefix + "_int"] = v;
   else if( v.is_bool() )
      mv[prefix + "_bool"] = v;
   else if( v.is_string() )
      mv[prefix + "_string"] = v.get_string();
   else
      mv[prefix + "_string"] = fc::json::to_string( v );
   // Note: we don't use double or array here, and we convert null and blob to string,
   //       and static_variants (i.e. in custom authorities) and maps (if any) are converted to strings too.
}

fc::variant es_data_adaptor::adapt_map_item( const fc::variants& v )
{
   FC_ASSERT( v.size() == 2, "Internal error" );
   fc::mutable_variant_object mv;

   extract_data_from_variant( v[0], mv, "key" );
   extract_data_from_variant( v[1], mv, "data" );

   fc::variant nv;
   fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
   return nv;
}

fc::variant es_data_adaptor::adapt_static_variant( const fc::variants& v )
{
   FC_ASSERT( v.size() == 2, "Internal error" );
   fc::mutable_variant_object mv;

   mv["which"] = v[0];
   extract_data_from_variant( v[1], mv, "data" );

   fc::variant nv;
   fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
   return nv;
}

void es_data_adaptor::adapt(fc::variants& v)
{
   for (auto& array_element : v)
   {
      if (array_element.is_object())
         array_element = adapt(array_element.get_object());
      else if (array_element.is_array())
         adapt(array_element.get_array());
      else
         array_element = array_element.as_string();
   }
}

} } // end namespace graphene::utilities
