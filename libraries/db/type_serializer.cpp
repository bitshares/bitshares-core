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
#include <graphene/db/index.hpp>

namespace graphene { namespace db {
map<string, size_t >                st;
vector<std::function<void()>>       serializers;

std::set<std::string>& processed_types()
{
   static std::set<std::string> p;
   return p;
}

std::stringstream& current_stream( std::unique_ptr<std::stringstream>&& s  )
{
   static std::unique_ptr<std::stringstream> stream;
   if( s ) stream = std::move(s);
   FC_ASSERT( stream );
   return *stream;
}

string remove_tail_if( const string& str, char c, const string& match )
{
   auto last = str.find_last_of( c );
   if( last != std::string::npos )
      if( str.substr( last + 1 ) == match )
         return str.substr( 0, last );
   return str;
}
string remove_namespace_if( const string& str, const string& match )
{
   auto last = str.find( match );
   if( last != std::string::npos )
      return str.substr( match.size()+2 );
   return str;
}

string remove_namespace( string str )
{
   str = remove_tail_if( str, '_', "operation" );
   str = remove_tail_if( str, '_', "t" );
   str = remove_tail_if( str, '_', "object" );
   str = remove_tail_if( str, '_', "type" );
   str = remove_namespace_if( str, "graphene::chain" );
   str = remove_namespace_if( str, "graphene::db" );
   str = remove_namespace_if( str, "std" );
   str = remove_namespace_if( str, "fc" );
   auto pos = str.find( ":" );
   if( pos != str.npos )
      str.replace( pos, 2, "_" );
   return str;
}
bool register_serializer( const string& name, std::function<void()> sr )
{
   if( st.find(name) == st.end() )
   {
      serializers.push_back( sr );
      st[name] = serializers.size() - 1;
      return true;
   }
   return false;
}

} }// graphene 
