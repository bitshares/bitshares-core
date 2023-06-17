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

#include <fc/io/json.hpp>
#include <fc/exception/exception.hpp>

static size_t curl_write_function(void *contents, size_t size, size_t nmemb, void *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

namespace graphene { namespace utilities {

static bool handle_bulk_response( uint16_t http_code, const std::string& curl_read_buffer )
{
   if( curl_wrapper::http_response_code::HTTP_200 == http_code )
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

   if( curl_wrapper::http_response_code::HTTP_413 == http_code )
   {
      elog( "413 error: Request too large. Can be low disk space. ${e}", ("e", curl_read_buffer) );
   }
   else if( curl_wrapper::http_response_code::HTTP_401 == http_code )
   {
      elog( "401 error: Unauthorized. ${e}", ("e", curl_read_buffer) );
   }
   else
   {
      elog( "${code} error: ${e}", ("code", std::to_string(http_code)) ("e", curl_read_buffer) );
   }
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

bool curl_wrapper::http_response::is_200() const
{
   return ( http_response_code::HTTP_200 == code );
}

CURL* curl_wrapper::init_curl()
{
   CURL* curl = curl_easy_init();
   if( curl )
   {
      curl_easy_setopt( curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 );
      return curl;
   }
   FC_THROW( "Unable to init cURL" );
}

curl_slist* curl_wrapper::init_request_headers()
{
   curl_slist* request_headers = curl_slist_append( NULL, "Content-Type: application/json" );
   FC_ASSERT( request_headers, "Unable to init cURL request headers" );
   return request_headers;
}

curl_wrapper::curl_wrapper()
{
   curl_easy_setopt( curl.get(), CURLOPT_HTTPHEADER, request_headers.get() );
   curl_easy_setopt( curl.get(), CURLOPT_USERAGENT, "bitshares-core/6.1" );
}

void curl_wrapper::curl_deleter::operator()( CURL* p_curl ) const
{
   if( p_curl )
      curl_easy_cleanup( p_curl );
}

void curl_wrapper::curl_slist_deleter::operator()( curl_slist* slist ) const
{
   if( slist )
      curl_slist_free_all( slist );
}

curl_wrapper::http_response curl_wrapper::request( curl_wrapper::http_request_method method,
                                                   const std::string& url,
                                                   const std::string& auth,
                                                   const std::string& query ) const
{
   curl_wrapper::http_response resp;

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

   if( curl_wrapper::http_request_method::HTTP_POST == method
       || curl_wrapper::http_request_method::HTTP_PUT == method )
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

   long code;
   curl_easy_getinfo( curl.get(), CURLINFO_RESPONSE_CODE, &code );
   resp.code = static_cast<uint16_t>( code );

   return resp;
}

curl_wrapper::http_response curl_wrapper::get( const std::string& url, const std::string& auth ) const
{
   return request( http_request_method::HTTP_GET, url, auth, "" );
}

curl_wrapper::http_response curl_wrapper::del( const std::string& url, const std::string& auth ) const
{
   return request( http_request_method::HTTP_DELETE, url, auth, "" );
}

curl_wrapper::http_response curl_wrapper::post( const std::string& url, const std::string& auth,
                                                const std::string& query ) const
{
   return request( http_request_method::HTTP_POST, url, auth, query );
}

curl_wrapper::http_response curl_wrapper::put( const std::string& url, const std::string& auth,
                                               const std::string& query ) const
{
   return request( http_request_method::HTTP_PUT, url, auth, query );
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
   if( !response.is_200() )
      FC_THROW( "Error on es_client::get_version(): code = ${code}, message = ${message} ",
                ("code", response.code) ("message", response.content) );

   fc::variant content = fc::json::from_string( response.content );
   return content["version"]["number"].as_string();
} FC_CAPTURE_LOG_AND_RETHROW( (base_url) ) } // GCOVR_EXCL_LINE

void es_client::check_version_7_or_above( bool& result ) const noexcept
{
   static const int64_t version_7 = 7;
   try {
      const auto es_version = get_version();
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

fc::variant es_data_adaptor::adapt( const fc::variant_object& op, uint16_t max_depth )
{
   if( 0 == max_depth )
   {
      fc::variant v;
      fc::to_variant(fc::json::to_string(op), v, FC_PACK_MAX_DEPTH);
      return v;
   }

   fc::mutable_variant_object o(op);

   // Note:
   // These fields are maps, they are stored redundantly in ES,
   //   one instance is a nested string array using the original field names (for backward compatibility, although
   //     ES queries return results in JSON format a little differently than node APIs),
   //   and a new instance is an object array with "_object" suffix added to the field name.
   static const std::unordered_set<std::string> to_string_array_fields = { "account_auths", "address_auths",
                                                                           "key_auths" };

   // Note:
   // These fields are stored redundantly in ES,
   //   one instance is a string using the original field names (originally for backward compatibility,
   //     but new fields are added here as well),
   //   and a new instance is a nested object or nested object array with "_object" suffix added to the field name.
   //
   // Why do we add new fields here?
   // Because we want to keep the JSON format made by node (stored in ES as a string), and store the object format
   //   at the same time for more flexible query.
   //
   // Object arrays not listed in this map (if any) are stored as nested objects only.
   static const std::unordered_map<std::string, data_type> to_string_fields = {
      { "parameters",               data_type::array_type }, // in committee proposals, current_fees.parameters
      { "op",                       data_type::static_variant_type }, // proposal_create_op.proposed_ops[*].op
      { "proposed_ops",             data_type::array_type }, // proposal_create_op.proposed_ops
      { "operations",               data_type::array_type }, // proposal_object.operations
      { "initializer",              data_type::static_variant_type }, // for workers
      { "policy",                   data_type::static_variant_type }, // for vesting balances
      { "predicates",               data_type::array_type }, // for assert_operation
      { "active_special_authority", data_type::static_variant_type }, // for accounts
      { "owner_special_authority",  data_type::static_variant_type }, // for accounts
      { "htlc_preimage_hash",       data_type::static_variant_type }, // for HTLCs
      { "argument",                 data_type::static_variant_type }, // for custom authority, restriction.argument
      { "feeds",                    data_type::map_type }, // asset_bitasset_data_object.feeds
      { "acceptable_collateral",    data_type::map_type }, // for credit offers
      { "acceptable_borrowers",     data_type::map_type }, // for credit offers
      { "on_fill",                  data_type::array_type } // for limit orders
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
         element = adapt( vo, max_depth - 1 );
         continue;
      }

      if( !element.is_array() )
         continue;

      auto& array = element.get_array();
      if( to_string_fields.find(name) != to_string_fields.end() )
      {
         // make a backup (only if depth is sufficient) and convert to string
         if( max_depth > 1 )
            original_arrays.emplace_back( name, array );
         element = fc::json::to_string(element);
      }
      else if( to_string_array_fields.find(name) != to_string_array_fields.end() )
      {
         // make a backup (only if depth is sufficient) and adapt the original
         if( max_depth > 1 )
         {
            auto backup = array;
            original_arrays.emplace_back( name, std::move( backup ) );
         }
         in_situ_adapt( array, max_depth - 1 );
      }
      else
         in_situ_adapt( array, max_depth - 1 );
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
         type = to_string_fields.at(name);
      o[name + "_object"] = adapt( value, type, max_depth - 1 );
   }

   fc::variant v;
   fc::to_variant(o, v, FC_PACK_MAX_DEPTH);
   return v;
}

fc::variant es_data_adaptor::adapt( const fc::variants& v, data_type type, uint16_t max_depth )
{
   if( data_type::static_variant_type == type )
      return adapt_static_variant( v, max_depth );

   // map_type or array_type
   fc::variants vs;
   vs.reserve( v.size() );
   for( const auto& item : v )
   {
      if( item.is_array() )
      {
         if( data_type::map_type == type )
            vs.push_back( adapt_map_item( item.get_array(), max_depth ) );
         else // assume it is a static_variant array
            vs.push_back( adapt_static_variant( item.get_array(), max_depth ) );
      }
      else if( item.is_object() ) // object array
         vs.push_back( adapt( item.get_object(), max_depth ) );
      else
         wlog( "Type of item is unexpected: ${item}", ("item", item) );
   }

   fc::variant nv;
   fc::to_variant(vs, nv, FC_PACK_MAX_DEPTH);
   return nv;
}

void es_data_adaptor::extract_data_from_variant(
      const fc::variant& v, fc::mutable_variant_object& mv, const std::string& prefix, uint16_t max_depth )
{
   FC_ASSERT( max_depth > 0, "Internal error" );
   if( v.is_object() )
      mv[prefix + "_object"] = adapt( v.get_object(), max_depth - 1 );
   else if( v.is_int64() || v.is_uint64() )
      mv[prefix + "_int"] = v;
   else if( v.is_bool() )
      mv[prefix + "_bool"] = v;
   else if( v.is_string() )
      mv[prefix + "_string"] = v.get_string();
   else
      mv[prefix + "_string"] = fc::json::to_string( v );
   // Note: we don't use double here, and we convert nulls and blobs to strings,
   //       arrays and pairs (i.e. in custom authorities) are converted to strings,
   //       static_variants and maps (if any) are converted to strings too.
}

fc::variant es_data_adaptor::adapt_map_item( const fc::variants& v, uint16_t max_depth )
{
   if( 0 == max_depth )
   {
      fc::variant nv;
      fc::to_variant(fc::json::to_string(v), nv, FC_PACK_MAX_DEPTH);
      return nv;
   }

   FC_ASSERT( v.size() == 2, "Internal error" );
   fc::mutable_variant_object mv;

   extract_data_from_variant( v[0], mv, "key", max_depth );
   extract_data_from_variant( v[1], mv, "data", max_depth );

   fc::variant nv;
   fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
   return nv;
}

fc::variant es_data_adaptor::adapt_static_variant( const fc::variants& v, uint16_t max_depth )
{
   if( 0 == max_depth )
   {
      fc::variant nv;
      fc::to_variant(fc::json::to_string(v), nv, FC_PACK_MAX_DEPTH);
      return nv;
   }

   FC_ASSERT( v.size() == 2, "Internal error" );
   fc::mutable_variant_object mv;

   mv["which"] = v[0];
   extract_data_from_variant( v[1], mv, "data", max_depth );

   fc::variant nv;
   fc::to_variant( mv, nv, FC_PACK_MAX_DEPTH );
   return nv;
}

void es_data_adaptor::in_situ_adapt( fc::variants& v, uint16_t max_depth )
{
   for( auto& array_element : v )
   {
      if( array_element.is_object() )
         array_element = adapt( array_element.get_object(), max_depth );
      else if( array_element.is_array() )
         in_situ_adapt( array_element.get_array(), max_depth );
      else
         array_element = array_element.as_string();
   }
}

} } // end namespace graphene::utilities
