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

#include <fc/io/json.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <graphene/chain/wild_object.hpp>

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace graphene::chain;

fc::mutable_variant_object g_vo_object_types;
std::vector< fc::mutable_variant_object > g_vo_fields;

struct serialize_object_type_member_visitor
{
   public:
      template<typename Member, class Class, Member (Class::*member)>
      void operator()( const char* name )const
      {
         fc::mutable_variant_object vo;
         vo["name"] = name;
         vo["type"] = fc::get_typename<Member>::name();
         vo["id"] = g_vo_fields.size();
         g_vo_fields.push_back( vo );
      }
};

struct serialize_object_type_visitor
{
   typedef void result_type;

   int t = 0;
   serialize_object_type_visitor(int _t ):t(_t){}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      fc::mutable_variant_object vo;
      vo["space_id"] = Type::space_id;
      vo["type_id"] = Type::type_id;
      g_vo_fields.clear();
      // visit all members
      fc::reflector<Type>::visit( serialize_object_type_member_visitor() );

      vo["fields"] = g_vo_fields;
      g_vo_object_types[ fc::get_typename<Type>::name() ] = vo;
   }
};

struct getattr_switch_table_entry
{
   uint32_t _switch_val;    // (space << 24) | (type << 16) | fieldnum
   string _object_typename;
   string _field_typename;
   string _field_name;
};

vector< getattr_switch_table_entry > build_switch_table()
{
   vector< getattr_switch_table_entry > result;
   for( const auto& item : g_vo_object_types )
   {
      const variant_object& vo = item.value().get_object();
      uint32_t top = (vo["space_id"].as_uint64() << 24) | (vo["type_id"].as_uint64() << 16);
      for( const auto& field : vo["fields"].get_array() )
      {
         getattr_switch_table_entry e;
         e._switch_val = top | field["id"].as_uint64();
         e._object_typename = item.key();
         e._field_typename = field["type"].get_string();
         e._field_name = field["name"].get_string();
         result.push_back( e );
      }
   }

   std::sort( result.begin(), result.end(),
      []( const getattr_switch_table_entry& a,
          const getattr_switch_table_entry& b )
      {
         return a._switch_val < b._switch_val;
      } );

   return result;
}

std::string generate_cmp_attr_impl( const vector< getattr_switch_table_entry >& switch_table )
{
   std::ostringstream out;

   // switch( space )
   // switch( type )
   // switch( fieldnum )
   // switch( opc )

   std::map< uint8_t,
    std::map< uint8_t,
     std::map< uint16_t,
     const getattr_switch_table_entry* > > > index;

   for( const getattr_switch_table_entry& e : switch_table )
   {
      uint8_t  sp = (e._switch_val >> 24) & 0xFF;
      uint8_t  ty = (e._switch_val >> 16) & 0xFF;
      uint16_t fn = (e._switch_val      ) & 0xFFFF;
      auto& i0 = index;
      if( i0.find( sp ) == i0.end() )
         i0[sp] = std::map< uint8_t, std::map< uint16_t, const getattr_switch_table_entry* > >();
      auto& i1 = i0[sp];
      if( i1.find( ty ) == i1.end() )
         i1[ty] = std::map< uint16_t, const getattr_switch_table_entry* >();
      auto& i2 = i1[ty];
      i2[fn] = &e;
   }
   out << "   switch( obj.id.space() )\n"
          "   {\n";
   for( const auto& e0 : index )
   {
      out << "    case " << int( e0.first ) << ":\n"
             "     switch( obj.id.type() )\n"
             "     {\n";
      for( const auto& e1 : e0.second )
      {
         out << "      case " << int( e1.first ) << ":\n"
                "       switch( field_num )\n"
                "       {\n";
         for( const auto& e2 : e1.second )
         {
            const std::string& ft = e2.second->_field_typename;
            const std::string& ot = e2.second->_object_typename;
            const std::string& fn = e2.second->_field_name;
            out << "        case " << int( e2.first ) << ":\n"
                   "        {\n"
                   "         // " << ft
                             << " " << ot
                             << "." << fn
                             << "\n"
                   "         const " << ft << "& dbval = object_database::cast< " << ot << " >( obj )." << fn << ";\n"
                   "         return _cmp< " << ft << " >( dbval, lit, opc );\n"
                   "        }\n";
         }
         out << "        default:\n"
                "         FC_ASSERT( false, \"unrecognized field_num\" );\n"
                "       }\n";
      }
      out << "      default:\n"
             "       FC_ASSERT( false, \"unrecognized object type\" );\n"
             "     }\n";
   }
   out << "    default:\n"
          "     FC_ASSERT( false, \"unrecognized object space\" );\n"
          "   }\n";

   return out.str();
}

static const char generated_file_banner[] =
"//                                   _           _    __ _ _        //\n"
"//                                  | |         | |  / _(_) |       //\n"
"//    __ _  ___ _ __   ___ _ __ __ _| |_ ___  __| | | |_ _| | ___   //\n"
"//   / _` |/ _ \\ '_ \\ / _ \\ '__/ _` | __/ _ \\/ _` | |  _| | |/ _ \\  //\n"
"//  | (_| |  __/ | | |  __/ | | (_| | ||  __/ (_| | | | | | |  __/  //\n"
"//   \\__, |\\___|_| |_|\\___|_|  \\__,_|\\__\\___|\\__,_| |_| |_|_|\\___|  //\n"
"//    __/ |                                                         //\n"
"//   |___/                                                          //\n"
"//                                                                  //\n"
"// Generated by:  programs/field_reflector/main.cpp                 //\n"
"//                                                                  //\n"
"// Warning: This is a generated file, any changes made here will be //\n"
"// overwritten by the build process.  If you need to change what    //\n"
"// is generated here, you should either modify the reflected        //\n"
"// types, or modify the code generator itself.                      //\n"
"//                                                                  //\n"
;

int main( int argc, char** argv )
{
   try
   {
      if( argc != 3 )
      {
         std::cout << "syntax:  " << argv[0] << " <template_filename> <output_filename>\n";
         return 1;
      }

      graphene::chain::impl::wild_object wo;

      for( int32_t i = 0; i < wo.count(); ++i )
      {
         wo.set_which(i);
         wo.visit( serialize_object_type_visitor(i) );
      }

      vector< getattr_switch_table_entry > switch_table = build_switch_table();

      fc::mutable_variant_object tmpl_params;

      tmpl_params["generated_file_banner"] = generated_file_banner;
      tmpl_params["object_descriptor"] = fc::json::to_string( g_vo_object_types );
      tmpl_params["cmp_attr_impl_body"] = generate_cmp_attr_impl( switch_table );

      std::ifstream template_file( argv[1] );
      std::stringstream ss;
      ss << template_file.rdbuf();
      std::string result = fc::format_string( ss.str(), tmpl_params );
      std::ofstream result_file( argv[2] );
      result_file << result;
   }
   catch ( const fc::exception& e ){ edump((e.to_detail_string())); }
   return 0;
}
