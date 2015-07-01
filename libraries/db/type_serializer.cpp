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
