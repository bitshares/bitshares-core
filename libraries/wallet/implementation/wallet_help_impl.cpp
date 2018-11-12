/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

namespace graphene { namespace wallet {

namespace detail {

variant wallet_api_impl::info() const
{
   auto chain_props = get_chain_properties();
   auto global_props = get_global_properties();
   auto dynamic_props = get_dynamic_global_properties();
   fc::mutable_variant_object result;
   result["head_block_num"] = dynamic_props.head_block_number;
   result["head_block_id"] = fc::variant(dynamic_props.head_block_id, 1);
   result["head_block_age"] = fc::get_approximate_relative_time_string(dynamic_props.time,
                                                                        time_point_sec(time_point::now()),
                                                                        " old");
   result["next_maintenance_time"] = fc::get_approximate_relative_time_string(dynamic_props.next_maintenance_time);
   result["chain_id"] = chain_props.chain_id;
   result["participation"] = (100*dynamic_props.recent_slots_filled.popcount()) / 128.0;
   result["active_witnesses"] = fc::variant(global_props.active_witnesses, GRAPHENE_MAX_NESTED_OBJECTS);
   result["active_committee_members"] = fc::variant(global_props.active_committee_members, GRAPHENE_MAX_NESTED_OBJECTS);
   return result;
}

variant_object wallet_api_impl::about() const
{
   string client_version( graphene::utilities::git_revision_description );
   const size_t pos = client_version.find( '/' );
   if( pos != string::npos && client_version.size() > pos )
      client_version = client_version.substr( pos + 1 );

   fc::mutable_variant_object result;
   //result["blockchain_name"]        = BLOCKCHAIN_NAME;
   //result["blockchain_description"] = BTS_BLOCKCHAIN_DESCRIPTION;
   result["client_version"]           = client_version;
   result["graphene_revision"]        = graphene::utilities::git_revision_sha;
   result["graphene_revision_age"]    = fc::get_approximate_relative_time_string( fc::time_point_sec( graphene::utilities::git_revision_unix_timestamp ) );
   result["fc_revision"]              = fc::git_revision_sha;
   result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec( fc::git_revision_unix_timestamp ) );
   result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
   result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
   result["openssl_version"]          = OPENSSL_VERSION_TEXT;

   std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
   std::string os = "osx";
#elif defined(__linux__)
   std::string os = "linux";
#elif defined(_MSC_VER)
   std::string os = "win32";
#else
   std::string os = "other";
#endif
   result["build"] = os + " " + bitness;

   return result;
}

}}} // graphene::wallet::detail
