/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/chain/operations.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/block.hpp>
#include <iostream>

using namespace graphene::chain;

namespace detail_ns {

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




template<typename T>
void generate_serializer();
template<typename T>
void register_serializer();


map<string, size_t >                st;
vector<std::function<void()>>       serializers;

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
template<> struct js_name< op_wrapper >        { static std::string name(){ return "operation "; } };
template<> struct js_name<fc::uint160>         { static std::string name(){ return "bytes 20";   } };
template<> struct js_name<fc::sha224>          { static std::string name(){ return "bytes 28";   } };
template<> struct js_name<fc::unsigned_int>    { static std::string name(){ return "varuint32";  } };
template<> struct js_name<fc::signed_int>      { static std::string name(){ return "varint32";   } };
template<> struct js_name< vote_id_type >      { static std::string name(){ return "vote_id";    } };
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

   int t = 0;
   serialize_type_visitor(int _t ):t(_t){}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      std::cout << "    " <<remove_namespace( fc::get_typename<Type>::name() )  <<": "<<t<<"\n";
   }
};


class serialize_member_visitor
{
   public:
      template<typename Member, class Class, Member (Class::*member)>
      void operator()( const char* name )const
      {
         std::cout << "    " << name <<": " << js_name<Member>::name() <<"\n";
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
template<>
struct serializer<std::vector<operation>,false>
{
   static void init() { }
   static void generate() {}
};

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
template<> struct serializer<vote_id_type,false> { static void init() {} static void generate() {} };
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
      static bool init = false;
      if( !init )
      {
         init = true;
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
      std::cout <<  js_name<fc::static_variant<T...>>::name() << " = static_variant [" + js_sv_name<T...>::name() + "\n]\n\n";
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
      std::cout << "" << name
                << " = new Serializer( \n"
                << "    \"" + name + "\"\n";

      fc::reflector<T>::visit( serialize_member_visitor() );

      std::cout <<")\n\n";
   }
};

} // namespace detail_ns

int main( int argc, char** argv )
{
   try {
    operation op;

    std::cout << "ChainTypes.operations=\n";
    for( uint32_t i = 0; i < op.count(); ++i )
    {
       op.set_which(i);
       op.visit( detail_ns::serialize_type_visitor(i) );
    }
    std::cout << "\n";

    detail_ns::js_name<operation>::name("operation");
    detail_ns::js_name<static_variant<address,public_key_type>>::name("key_data");
    detail_ns::js_name<operation_result>::name("operation_result");
    detail_ns::js_name<header_extension>::name("header_extension");
    detail_ns::js_name<static_variant<refund_worker_type::initializer, vesting_balance_worker_type::initializer>>::name("initializer_type");
    detail_ns::serializer<signed_block>::init();
    detail_ns::serializer<block_header>::init();
    detail_ns::serializer<signed_block_header>::init();
    detail_ns::serializer<operation>::init();
    detail_ns::serializer<transaction>::init();
    detail_ns::serializer<signed_transaction>::init();
    for( const auto& gen : detail_ns::serializers )
       gen();

  } catch ( const fc::exception& e ){ edump((e.to_detail_string())); }
   return 0;
}
