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

#include <fc/reflect/reflect.hpp>
#include <fc/static_variant.hpp>

#include <map>
#include <string>
#include <vector>

namespace graphene { namespace app {

struct api_access_info
{
   std::string password_hash_b64;
   std::string password_salt_b64;
   std::vector< std::string > allowed_apis;
};

struct api_access_info_signed
{
   bool required_lifetime_member;
   std::string required_registrar;
   std::string required_referrer;
   std::vector< std::string > allowed_apis;
};

struct api_access
{
   std::map< std::string, api_access_info > permission_map;
   std::vector< api_access_info_signed > permission_map_signed_default;
   std::map< std::string, api_access_info_signed > permission_map_signed_user;
};

typedef fc::static_variant<
   api_access_info_signed,
   std::vector< api_access_info_signed >
> api_access_info_signed_variant;

} } // graphene::app

FC_REFLECT( graphene::app::api_access_info,
    (password_hash_b64)
    (password_salt_b64)
    (allowed_apis)
   )

FC_REFLECT( graphene::app::api_access_info_signed,
    (required_lifetime_member)
    (required_registrar)
    (required_referrer)
    (allowed_apis)
   )

FC_REFLECT( graphene::app::api_access,
    (permission_map)
    (permission_map_signed_default)
    (permission_map_signed_user)
   )

FC_REFLECT_TYPENAME( graphene::app::api_access_info_signed_variant )
