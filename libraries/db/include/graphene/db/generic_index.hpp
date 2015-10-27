/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
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
#include <graphene/db/index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace graphene { namespace chain {

   using boost::multi_index_container;
   using namespace boost::multi_index;

   struct by_id{};
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
            assert( nullptr != dynamic_cast<const ObjectType*>(&obj) );
            auto ok = _indices.modify( _indices.iterator_to( static_cast<const ObjectType&>(obj) ),
                                       [&m]( ObjectType& o ){ m(o); } );
            FC_ASSERT( ok, "Could not modify object, most likely a index constraint was violated" );
         }

         virtual void remove( const object& obj )override
         {
            _indices.erase( _indices.iterator_to( static_cast<const ObjectType&>(obj) ) );
         }

         virtual const object* find( object_id_type id )const override
         {
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

         virtual fc::uint128 hash()const override {
            fc::uint128 result;
            for( const auto& ptr : _indices )
            {
               result += ptr.hash();
            }

            return result;
         }

      private:
         fc::uint128 _current_hash;
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
