#include <fc/reflect/variant.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/sha224.hpp>
#include <sstream>
#include <set>

namespace graphene { namespace db {
using std::set;
using std::map;
using fc::time_point_sec;
using fc::time_point;
using std::string;

std::set<std::string>& processed_types();
std::stringstream& current_stream( std::unique_ptr<std::stringstream>&& s = std::unique_ptr<std::stringstream>() );
string remove_tail_if( const string& str, char c, const string& match );
string remove_namespace_if( const string& str, const string& match );
string remove_namespace( string str );

template<typename T>
void generate_serializer();
template<typename T>
void register_serializer();

extern map<string, size_t >                st;
extern vector<std::function<void()>>       serializers;

bool register_serializer( const string& name, std::function<void()> sr );

template<typename T> struct js_name { static std::string name(){ return  remove_namespace(fc::get_typename<T>::name()); }; };

template<typename T, size_t N>
struct js_name<fc::array<T,N>>
{
   static std::string name(){ return  "fixed_array "+ fc::to_string(N) + ", "  + remove_namespace(fc::get_typename<T>::name()); };
};
template<size_t N>   struct js_name<fc::array<char,N>>    { static std::string name(){ return  "bytes "+ fc::to_string(N); }; };
template<size_t N>   struct js_name<fc::array<uint8_t,N>> { static std::string name(){ return  "bytes "+ fc::to_string(N); }; };
template<typename T> struct js_name< fc::optional<T> >    { static std::string name(){ return "optional " + js_name<T>::name(); } };
template<>           struct js_name< object_id_type >     { static std::string name(){ return "object_id_type"; } };
template<typename T> struct js_name< fc::flat_set<T> >    { static std::string name(){ return "set " + js_name<T>::name(); } };
template<typename T> struct js_name< std::vector<T> >     { static std::string name(){ return "array " + js_name<T>::name(); } };
template<typename T> struct js_name< fc::safe<T> > { static std::string name(){ return js_name<T>::name(); } };


template<> struct js_name< std::vector<char> > { static std::string name(){ return "bytes()";     } };
//template<> struct js_name< op_wrapper >        { static std::string name(){ return "operation "; } };
template<> struct js_name<fc::uint160>         { static std::string name(){ return "bytes 20";   } };
template<> struct js_name<fc::sha224>          { static std::string name(){ return "bytes 28";   } };
template<> struct js_name<fc::unsigned_int>    { static std::string name(){ return "varuint32";  } };
template<> struct js_name<fc::signed_int>      { static std::string name(){ return "varint32";   } };
template<> struct js_name< time_point_sec >    { static std::string name(){ return "time_point_sec"; } };

template<uint8_t S, uint8_t T, typename O>
struct js_name<graphene::db::object_id<S,T,O> >
{
   static std::string name(){
      return "protocol_id_type \"" + remove_namespace(fc::get_typename<O>::name()) + "\"";
   };
};


template<typename T> struct js_name< std::set<T> > { static std::string name(){ return "set " + js_name<T>::name(); } };

template<typename K, typename V>
struct js_name< std::map<K,V> > { static std::string name(){ return "map (" + js_name<K>::name() + "), (" + js_name<V>::name() +")"; } };

template<typename K, typename V>
struct js_name< fc::flat_map<K,V> > { static std::string name(){ return "map (" + js_name<K>::name() + "), (" + js_name<V>::name() +")"; } };


template<typename... T> struct js_sv_name;

template<typename A> struct js_sv_name<A>
{ static std::string name(){ return  "\n    " + js_name<A>::name(); } };

template<typename A, typename... T>
struct js_sv_name<A,T...> { static std::string name(){ return  "\n    " + js_name<A>::name() +"    " + js_sv_name<T...>::name(); } };


template<typename... T>
struct js_name< fc::static_variant<T...> >
{
   static std::string name( std::string n = ""){
      static const std::string name = n;
      if( name == "" )
         return "static_variant [" + js_sv_name<T...>::name() + "\n]";
      else return name;
   }
};


template<typename T, bool reflected = fc::reflector<T>::is_defined::value>
struct serializer;


struct register_type_visitor
{
   typedef void result_type;

   template<typename Type>
   result_type operator()( const Type& op )const { serializer<Type>::init(); }
};

class register_member_visitor;

struct serialize_type_visitor
{
   typedef void result_type;

   fc::mutable_variant_object& obj;
   int t = 0;
   serialize_type_visitor( fc::mutable_variant_object& o, int _t ):obj(o),t(_t){}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      obj(remove_namespace( fc::get_typename<Type>::name()),t);
   }
};


class serialize_member_visitor
{
   public:
      template<typename Member, class Class, Member (Class::*member)>
      void operator()( const char* name )const
      {
         current_stream() << "    " << name << " : " <<  fc::get_typename<Member>::name() <<"\n";
      }
};

template<typename T>
struct serializer<T,false>
{
   static_assert( fc::reflector<T>::is_defined::value == false, "invalid template arguments" );
   static void init()
   {}

   static void generate()
   {}
};

template<typename T, size_t N>
struct serializer<fc::array<T,N>,false>
{
   static void init() { serializer<T>::init(); }
   static void generate() {}
};
template<typename T>
struct serializer<std::vector<T>,false>
{
   static void init() { serializer<T>::init(); }
   static void generate() {}
};
/*
template<>
struct serializer<std::vector<operation>,false>
{
   static void init() { }
   static void generate() {}
};
*/

template<>
struct serializer<object_id_type,true>
{
   static void init() {}

   static void generate() {}
};
template<>
struct serializer<uint64_t,false>
{
   static void init() {}
   static void generate() {}
};
#ifdef __APPLE__
// on mac, size_t is a distinct type from uint64_t or uint32_t and needs a separate specialization
template<> struct serializer<size_t,false> { static void init() {} static void generate() {} };
#endif
template<> struct serializer<int64_t,false> { static void init() {} static void generate() {} };
template<> struct serializer<int64_t,true> { static void init() {} static void generate() {} };

template<typename T>
struct serializer<fc::optional<T>,false>
{
   static void init() { serializer<T>::init(); }
   static void generate(){}
};

template<uint8_t SpaceID, uint8_t TypeID, typename T>
struct serializer< graphene::db::object_id<SpaceID,TypeID,T> ,true>
{
   static void init() {}
   static void generate() {}
};

template<typename... T>
struct serializer< fc::static_variant<T...>, false >
{
   static void init()
   {
      auto name = js_name<fc::static_variant<T...>>::name();
      if( processed_types().find( name ) == processed_types().end()  )
      {
         processed_types().insert( name );
         fc::static_variant<T...> var;
         for( int i = 0; i < var.count(); ++i )
         {
            var.set_which(i);
            var.visit( register_type_visitor() );
         }
         register_serializer( js_name<fc::static_variant<T...>>::name(), [=](){ generate(); } );
      }
   }

   static void generate()
   {
      current_stream() <<  js_name<fc::static_variant<T...>>::name() << " = static_variant [" + js_sv_name<T...>::name() + "\n]\n\n";
   }
};

class register_member_visitor
{
   public:
      template<typename Member, class Class, Member (Class::*member)>
      void operator()( const char* name )const
      {
         serializer<Member>::init();
      }
};

template<typename T, bool reflected>
struct serializer
{
   static_assert( fc::reflector<T>::is_defined::value == reflected, "invalid template arguments" );
   static void init()
   {
      auto name = js_name<T>::name();
      if( st.find(name) == st.end() )
      {
         fc::reflector<T>::visit( register_member_visitor() );
         register_serializer( name, [=](){ generate(); } );
      }
   }

   static void generate()
   {
      auto name = remove_namespace( js_name<T>::name() );
      if( name == "int64" ) return;
      current_stream() << "" << name
                << " = new Serializer( \n"
                << "    \"" + name + "\"\n";

      fc::reflector<T>::visit( serialize_member_visitor() );

      current_stream() <<")\n\n";
   }
};


template<typename T>
std::string get_type_description()
{
   current_stream( std::unique_ptr<std::stringstream>(new std::stringstream()) );
   processed_types().clear();
   serializer<T>::init();
   for( const auto& gen : serializers )
      gen();
   return current_stream().str();
}


} } // graphene::db








