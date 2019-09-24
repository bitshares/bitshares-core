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
