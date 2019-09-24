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
#include <graphene/wallet/reflect_util.hpp>

namespace graphene { namespace wallet { namespace impl {

std::string clean_name( const std::string& name )
{
   const static std::string prefix = "graphene::protocol::";
   const static std::string suffix = "_operation";
   // graphene::protocol::.*_operation
   if(    (name.size() >= prefix.size() + suffix.size())
       && (name.substr( 0, prefix.size() ) == prefix)
       && (name.substr( name.size()-suffix.size(), suffix.size() ) == suffix )
     )
        return name.substr( prefix.size(), name.size() - prefix.size() - suffix.size() );

   wlog( "don't know how to clean name: ${name}", ("name", name) );
   return name;
}

}}} // namespace graphene::wallet::impl
