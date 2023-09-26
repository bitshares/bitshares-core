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
#include <fc/exception/exception.hpp>
#include <fc/io/varint.hpp>

namespace graphene { namespace db {

   struct object_id_type
   {
      static constexpr uint8_t instance_bits = 48;
      static constexpr uint8_t type_and_instance_bits = 56;
      static constexpr uint64_t one_byte_mask = 0x00ff;
      static constexpr uint64_t max_instance = 0x0000ffffffffffff;

      object_id_type() = default;
      object_id_type( uint8_t s, uint8_t t, uint64_t i ){ reset( s, t, i ); }

      void reset( uint8_t s, uint8_t t, uint64_t i )
      {
         FC_ASSERT( i >> instance_bits == 0, "instance overflow", ("instance",i) );
         number = ( (uint64_t(s) << type_and_instance_bits) | (uint64_t(t) << instance_bits) ) | i;
      }

      uint8_t  space()const      { return number >> type_and_instance_bits; }
      uint8_t  type()const       { return (number >> instance_bits) & one_byte_mask; }
      uint16_t space_type()const { return number >> instance_bits; }
      uint64_t instance()const   { return number & max_instance; }
      bool     is_null()const { return 0 == number; }
      explicit operator uint64_t()const { return number; }

      friend bool  operator == ( const object_id_type& a, const object_id_type& b ) { return a.number == b.number; }
      friend bool  operator != ( const object_id_type& a, const object_id_type& b ) { return a.number != b.number; }
      friend bool  operator < ( const object_id_type& a, const object_id_type& b ) { return a.number < b.number; }
      friend bool  operator > ( const object_id_type& a, const object_id_type& b ) { return a.number > b.number; }

      object_id_type& operator++() { ++number; return *this; }

      friend object_id_type operator+(const object_id_type& a, int64_t delta ) {
         return object_id_type( a.space(), a.type(), a.instance() + delta );
      }
      friend size_t hash_value( const object_id_type& v ) { return std::hash<uint64_t>()(v.number); }

      template< typename T >
      bool is() const
      {
         return space_type() == T::space_type;
      }


      template< typename T >
      T as() const
      {
         return T( *this );
      }

      explicit operator std::string() const
      {
         return fc::to_string(space()) + "." + fc::to_string(type()) + "." + fc::to_string(instance());
      }

      uint64_t number = 0;
   };

   class object;
   class object_database;

   /// This template is used to downcast a generic object type to a specific xyz_object type.
   template<typename ObjectID>
   struct object_downcast { using type = object; };
   // This macro specializes the above template for a specific xyz_object type
#define MAP_OBJECT_ID_TO_TYPE(OBJECT) \
   namespace graphene { namespace db { \
   template<> \
   struct object_downcast<const graphene::db::object_id<OBJECT::space_id, \
                                                        OBJECT::type_id>&> { using type = OBJECT; }; \
   } }
   template<typename ObjectID>
   using object_downcast_t = typename object_downcast<ObjectID>::type;

   template<uint8_t SpaceID, uint8_t TypeID>
   struct object_id
   {
      static constexpr uint8_t type_bits = 8;
      static constexpr uint8_t instance_bits = 48;
      static constexpr uint64_t max_instance = 0x0000ffffffffffff;

      static constexpr uint8_t space_id = SpaceID;
      static constexpr uint8_t type_id = TypeID;

      static constexpr uint16_t space_type = uint16_t(uint16_t(space_id) << type_bits) | uint16_t(type_id);

      static constexpr object_id max()
      {
         return object_id( max_instance );
      }

      object_id() = default;
      explicit object_id( const fc::unsigned_int& i ):instance(i)
      {
         validate();
      }
      explicit object_id( uint64_t i ):instance(i)
      {
         validate();
      }
      explicit object_id( const object_id_type& id ):instance(id.instance())
      {
         // Won't overflow, but need to check space and type
         FC_ASSERT( id.is<std::remove_reference_t<decltype(*this)>>(), "space or type mismatch" );
      }

      void validate()const
      {
         FC_ASSERT( (instance.value >> instance_bits) == 0, "instance overflow", ("instance",instance) );
      }

      object_id& operator=( const object_id_type& o )
      {
         *this = object_id(o);
         return *this;
      }

      friend object_id operator+(const object_id& a, int64_t delta )
      { return object_id( uint64_t(a.instance.value+delta) ); }

      explicit operator object_id_type()const { return object_id_type( SpaceID, TypeID, instance.value ); }
      explicit operator uint64_t()const { return object_id_type( *this ).number; }

      template<typename DB>
      auto operator()(const DB& db)const -> const decltype(db.get(*this))& { return db.get(*this); }

      friend bool  operator == ( const object_id& a, const object_id& b ) { return a.instance == b.instance; }
      friend bool  operator != ( const object_id& a, const object_id& b ) { return a.instance != b.instance; }
      friend bool  operator == ( const object_id_type& a, const object_id& b ) { return a == object_id_type(b); }
      friend bool  operator != ( const object_id_type& a, const object_id& b ) { return a != object_id_type(b); }
      friend bool  operator == ( const object_id& a, const object_id_type& b ) { return object_id_type(a) == b; }
      friend bool  operator != ( const object_id& a, const object_id_type& b ) { return object_id_type(a) != b; }
      friend bool  operator == ( const object_id& a, const fc::unsigned_int& b ) { return a.instance == b; }
      friend bool  operator != ( const object_id& a, const fc::unsigned_int& b ) { return a.instance != b; }
      friend bool  operator == ( const fc::unsigned_int& a, const object_id& b ) { return a == b.instance; }
      friend bool  operator != ( const fc::unsigned_int& a, const object_id& b ) { return a != b.instance; }

      friend bool  operator < ( const object_id& a, const object_id& b )
      { return a.instance.value < b.instance.value; }
      friend bool  operator > ( const object_id& a, const object_id& b )
      { return a.instance.value > b.instance.value; }

      friend size_t hash_value( const object_id& v ) { return std::hash<uint64_t>()(v.instance.value); }

      explicit operator std::string() const
      {
         return fc::to_string(space_id) + "." + fc::to_string(type_id) + "." + fc::to_string(instance.value);
      }

      fc::unsigned_int instance; // default is 0
   };

} } // graphene::db

FC_REFLECT( graphene::db::object_id_type, (number) )

// REFLECT object_id manually because it has 2 template params
namespace fc {
template<uint8_t SpaceID, uint8_t TypeID>
struct get_typename<graphene::db::object_id<SpaceID,TypeID>>
{
   static const char* name() {
      return typeid(get_typename).name();
      static std::string _str = string("graphene::db::object_id<") + fc::to_string(SpaceID) + ":"
                                                                   + fc::to_string(TypeID)  + ">";
      return _str.c_str();
   }
};

template<uint8_t SpaceID, uint8_t TypeID>
struct reflector<graphene::db::object_id<SpaceID,TypeID> >
{
    using type = graphene::db::object_id<SpaceID,TypeID>;
    using is_defined = std::true_type;
    using native_members = typelist::list<fc::field_reflection<0, type, unsigned_int, &type::instance>>;
    using inherited_members = typelist::list<>;
    using members = native_members;
    using base_classes = typelist::list<>;
    enum  member_count_enum {
      local_member_count = 1,
      total_member_count = 1
    };
    template<typename Visitor>
    static inline void visit( const Visitor& visitor )
    {
       using member_type = decltype(((type*)nullptr)->instance);
       visitor.TEMPLATE operator()<member_type,type,&type::instance>( "instance" );
    }
};
namespace member_names {
template<uint8_t S, uint8_t T>
struct member_name<graphene::db::object_id<S,T>, 0> { static constexpr const char* value = "instance"; };
}


 inline void to_variant( const graphene::db::object_id_type& var,  fc::variant& vo, uint32_t max_depth = 1 )
 {
    vo = std::string( var );
 }

 inline void from_variant( const fc::variant& var,  graphene::db::object_id_type& vo, uint32_t max_depth = 1 )
 { try {
    const auto& s = var.get_string();
    auto first_dot = s.find('.');
    FC_ASSERT( first_dot != std::string::npos, "Missing the first dot" );
    FC_ASSERT( first_dot != 0, "Missing the space part" );
    auto second_dot = s.find('.',first_dot+1);
    FC_ASSERT( second_dot != std::string::npos, "Missing the second dot" );
    FC_ASSERT( second_dot != first_dot+1, "Missing the type part" );
    auto space_id = fc::to_uint64( s.substr( 0, first_dot ) );
    FC_ASSERT( space_id <= graphene::db::object_id_type::one_byte_mask, "space overflow" );
    auto type_id =  fc::to_uint64( s.substr( first_dot+1, (second_dot-first_dot)-1 ) );
    FC_ASSERT( type_id <= graphene::db::object_id_type::one_byte_mask, "type overflow");
    auto instance = fc::to_uint64(s.substr( second_dot+1 ));
    vo.reset( static_cast<uint8_t>(space_id), static_cast<uint8_t>(type_id), instance );
 } FC_CAPTURE_AND_RETHROW( (var) ) } // GCOVR_EXCL_LINE

 template<uint8_t SpaceID, uint8_t TypeID>
 void to_variant( const graphene::db::object_id<SpaceID,TypeID>& var,  fc::variant& vo, uint32_t max_depth = 1 )
 {
    vo = std::string( var );
 }

 template<uint8_t SpaceID, uint8_t TypeID>
 void from_variant( const fc::variant& var,  graphene::db::object_id<SpaceID,TypeID>& vo, uint32_t max_depth = 1 )
 { try {
    const auto& s = var.get_string();
    auto first_dot = s.find('.');
    FC_ASSERT( first_dot != std::string::npos, "Missing the first dot" );
    FC_ASSERT( first_dot != 0, "Missing the space part" );
    auto second_dot = s.find('.',first_dot+1);
    FC_ASSERT( second_dot != std::string::npos, "Missing the second dot" );
    FC_ASSERT( second_dot != first_dot+1, "Missing the type part" );
    FC_ASSERT( fc::to_uint64( s.substr( 0, first_dot ) ) == SpaceID &&
               fc::to_uint64( s.substr( first_dot+1, (second_dot-first_dot)-1 ) ) == TypeID,
               "Space.Type.0 (${SpaceID}.${TypeID}.0) doesn't match expected value ${var}",
               ("TypeID",TypeID)("SpaceID",SpaceID)("var",var) );
    graphene::db::object_id<SpaceID,TypeID> tmp { fc::to_uint64(s.substr( second_dot+1 )) };
    vo = tmp;
 } FC_CAPTURE_AND_RETHROW( (var) ) } // GCOVR_EXCL_LINE

} // namespace fc

namespace std {
     template <> struct hash<graphene::db::object_id_type>
     {
          size_t operator()(const graphene::db::object_id_type& x) const
          {
              return std::hash<uint64_t>()(x.number);
          }
     };
}
