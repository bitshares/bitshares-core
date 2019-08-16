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
#include <graphene/db/index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace graphene { namespace db {

   using boost::multi_index_container;
   using namespace boost::multi_index;

   struct by_id;
   /**
    *  Almost all objects can be tracked and managed via a boost::multi_index container that uses
    *  an unordered_unique key on the object ID.  This template class adapts the generic index interface
    *  to work with arbitrary boost multi_index containers on the same type.
    */
   template<typename ObjectType, typename MultiIndexType>
   class generic_index : public index
   {
      public:
         typedef MultiIndexType index_type;
         typedef ObjectType     object_type;

         virtual const object& insert( object&& obj )override
         {
            assert( nullptr != dynamic_cast<ObjectType*>(&obj) );
            auto insert_result = _indices.insert( std::move( static_cast<ObjectType&>(obj) ) );
            FC_ASSERT( insert_result.second, "Could not insert object, most likely a uniqueness constraint was violated" );
            return *insert_result.first;
         }

         virtual const object&  create(const std::function<void(object&)>& constructor )override
         {
            ObjectType item;
            item.id = get_next_id();
            constructor( item );
            auto insert_result = _indices.insert( std::move(item) );
            FC_ASSERT(insert_result.second, "Could not create object! Most likely a uniqueness constraint is violated.");
            use_next_id();
            return *insert_result.first;
         }

         virtual void modify( const object& obj, const std::function<void(object&)>& m )override
         {
            assert(nullptr != dynamic_cast<const ObjectType*>(&obj));
            std::exception_ptr exc;
            auto ok = _indices.modify(_indices.iterator_to(static_cast<const ObjectType&>(obj)),
                                       [&m, &exc](ObjectType& o) mutable {
                                          try {
                                             m(o);
                                          } catch (fc::exception& e) {
                                             exc = std::current_exception();
                                             elog("Exception while modifying object: ${e} -- object may be corrupted",
                                                  ("e", e));
                                          } catch (...) {
                                             exc = std::current_exception();
                                             elog("Unknown exception while modifying object");
                                          }
                                       }
                      );
            if (exc)
                std::rethrow_exception(exc);
            FC_ASSERT(ok, "Could not modify object, most likely an index constraint was violated");
         }

         virtual void remove( const object& obj )override
         {
            _indices.erase( _indices.iterator_to( static_cast<const ObjectType&>(obj) ) );
         }

         virtual const object* find( object_id_type id )const override
         {
            static_assert(std::is_same<typename MultiIndexType::key_type, object_id_type>::value,
                          "First index of MultiIndexType MUST be object_id_type!");
            auto itr = _indices.find( id );
            if( itr == _indices.end() ) return nullptr;
            return &*itr;
         }

         virtual void inspect_all_objects(std::function<void (const object&)> inspector)const override
         {
            try {
               for( const auto& ptr : _indices )
                  inspector(ptr);
            } FC_CAPTURE_AND_RETHROW()
         }

         const index_type& indices()const { return _indices; }

      private:
         index_type  _indices;
   };

   /**
    * @brief An index type for objects which may be deleted
    *
    * This is the preferred index type for objects which need only be referenced by ID, but may be deleted.
    */
   template< class T >
   struct sparse_index : public generic_index<T, boost::multi_index_container<
      T,
      indexed_by<
         ordered_unique<
            tag<by_id>,
            member<object, object_id_type, &object::id>
         >
      >
   >>{};

} }
