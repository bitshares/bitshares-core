/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/db/object_id.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/city.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace db {

   /**
    *  @brief base for all database objects
    *
    *  The object is the fundamental building block of the database and
    *  is the level upon which undo/redo operations are performed.  Objects
    *  are used to track data and their relationships and provide an effecient
    *  means to find and update information.
    *
    *  Objects are assigned a unique and sequential object ID by the database within
    *  the id_space defined in the object.
    *
    *  All objects must be serializable via FC_REFLECT() and their content must be
    *  faithfully restored.   Additionally all objects must be copy-constructable and
    *  assignable in a relatively efficient manner.  In general this means that objects
    *  should only refer to other objects by ID and avoid expensive operations when
    *  they are copied, especially if they are modified frequently.
    *
    *  Additionally all objects may be annotated by plugins which wish to maintain
    *  additional information to an object.  There can be at most one annotation
    *  per id_space for each object.   An example of an annotation would be tracking
    *  extra data not required by validation such as the name and description of
    *  a user asset.  By carefully organizing how information is organized and
    *  tracked systems can minimize the workload to only that which is necessary
    *  to perform their function.
    *
    *  @note Do not use multiple inheritance with object because the code assumes
    *  a static_cast will work between object and derived types.
    */
   class object
   {
      public:
         object(){}
         virtual ~object(){}

         static const uint8_t space_id = 0;
         static const uint8_t type_id  = 0;


         // serialized
         object_id_type          id;

         /// these methods are implemented for derived classes by inheriting abstract_object<DerivedClass>
         virtual unique_ptr<object> clone()const = 0;
         virtual void               move_from( object& obj ) = 0;
         virtual variant            to_variant()const  = 0;
         virtual vector<char>       pack()const = 0;
         virtual fc::uint128        hash()const = 0;
   };

   /**
    * @class abstract_object
    * @brief   Use the Curiously Recurring Template Pattern to automatically add the ability to
    *  clone, serialize, and move objects polymorphically.
    *
    *  http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
    */
   template<typename DerivedClass>
   class abstract_object : public object
   {
      public:
         virtual unique_ptr<object> clone()const
         {
            return unique_ptr<object>(new DerivedClass( *static_cast<const DerivedClass*>(this) ));
         }

         virtual void    move_from( object& obj )
         {
            static_cast<DerivedClass&>(*this) = std::move( static_cast<DerivedClass&>(obj) );
         }
         virtual variant to_variant()const { return variant( static_cast<const DerivedClass&>(*this) ); }
         virtual vector<char> pack()const  { return fc::raw::pack( static_cast<const DerivedClass&>(*this) ); }
         virtual fc::uint128  hash()const  {  
             auto tmp = this->pack();
             return fc::city_hash_crc_128( tmp.data(), tmp.size() );
         }
   };

   typedef flat_map<uint8_t, object_id_type> annotation_map;

   /**
    *  @class annotated_object
    *  @brief An object that is easily extended by providing pointers to other objects, one for each space.
    */
   template<typename DerivedClass>
   class annotated_object : public abstract_object<DerivedClass>
   {
      public:
         /** return object_id_type() if no anotation is found for id_space */
         object_id_type          get_annotation( uint8_t annotation_id_space )const
         {
            auto itr = annotations.find(annotation_id_space);
            if( itr != annotations.end() ) return itr->second;
            return object_id_type();
         }
         void                    set_annotation( object_id_type id )
         {
            annotations[id.space()] = id;
         }

         /**
          *  Annotations should be accessed via get_annotation and set_annotation so
          *  that they can be maintained in sorted order.
          */
         annotation_map annotations;
   };

} } // graphene::db

FC_REFLECT_TYPENAME( graphene::db::annotation_map )
FC_REFLECT( graphene::db::object, (id) )
FC_REFLECT_DERIVED_TEMPLATE( (typename Derived), graphene::db::annotated_object<Derived>, (graphene::db::object), (annotations) )
