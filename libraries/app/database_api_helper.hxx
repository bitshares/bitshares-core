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
#pragma once

namespace graphene { namespace app {

class database_api_helper
{
public:
   database_api_helper( graphene::chain::database& db, const application_options* app_options );
   explicit database_api_helper( const graphene::app::application& app );

   // Member variables
   graphene::chain::database& _db;
   const application_options* _app_options = nullptr;

   // Accounts
   const account_object* get_account_from_string( const std::string& name_or_id,
                                                  bool throw_if_not_found = true ) const;

   // Assets
   const asset_object* get_asset_from_string( const std::string& symbol_or_id,
                                              bool throw_if_not_found = true ) const;

   /// Template functions for simple list_X and get_X_by_T APIs, to reduce duplicate code
   /// @{
   template <typename X>
   auto make_tuple_if_multiple(X x) const
   { return x; }

   template <typename... X>
   auto make_tuple_if_multiple(X... x) const
   { return std::make_tuple( x... ); }

   template <typename T>
   auto call_end_or_upper_bound( const T& t ) const
   { return std::end( t ); }

   template <typename T, typename... X>
   auto call_end_or_upper_bound( const T& t, X... x ) const
   { return t.upper_bound( make_tuple_if_multiple( x... ) ); }

   template <typename OBJ_TYPE, typename OBJ_ID_TYPE, typename INDEX_TYPE, typename T, typename... X >
   vector<OBJ_TYPE> get_objects_by_x(
               T application_options::* app_opt_member_ptr,
               const INDEX_TYPE& idx,
               const optional<uint32_t>& olimit,
               const optional<OBJ_ID_TYPE>& ostart_id,
               X... x ) const
   {
      FC_ASSERT( _app_options, "Internal error" );
      const auto configured_limit = _app_options->*app_opt_member_ptr;
      uint64_t limit = olimit.valid() ? *olimit : configured_limit;
      FC_ASSERT( limit <= configured_limit,
                 "limit can not be greater than ${configured_limit}",
                 ("configured_limit", configured_limit) );

      vector<OBJ_TYPE> results;

      OBJ_ID_TYPE start_obj_id = ostart_id.valid() ? *ostart_id : OBJ_ID_TYPE();
      object_id_type start_id { start_obj_id };

      auto lower_itr = idx.lower_bound( make_tuple_if_multiple( x..., start_id ) );
      auto upper_itr = call_end_or_upper_bound( idx, x... );

      results.reserve( limit );
      while( lower_itr != upper_itr && results.size() < limit )
      {
         results.emplace_back( *lower_itr );
         ++lower_itr;
      }

      return results;
   }
   /// @}

};

} } // graphene::app
