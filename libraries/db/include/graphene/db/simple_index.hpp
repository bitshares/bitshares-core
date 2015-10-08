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
#pragma once
#include <graphene/db/index.hpp>

namespace graphene { namespace db {

   /**
    *  @class simple_index
    *  @brief A simple index uses a vector<unique_ptr<T>> to store data
    *
    *  This index is preferred in situations where the data will never be
    *  removed from main memory and when access by ID is the only kind
    *  of access that is necessary.
    */
   template<typename T>
   class simple_index : public index
   {
      public:
         typedef T object_type;

         virtual const object&  create( const std::function<void(object&)>& constructor ) override
         {
             auto id = get_next_id();
             auto instance = id.instance();
             if( instance >= _objects.size() ) _objects.resize( instance + 1 );
             _objects[instance].reset(new T);
             _objects[instance]->id = id;
             constructor( *_objects[instance] );
             _objects[instance]->id = id; // just in case it changed
             use_next_id();
             return *_objects[instance];
         }

         virtual void modify( const object& obj, const std::function<void(object&)>& modify_callback ) override
         {
            assert( obj.id.instance() < _objects.size() );
            modify_callback( *_objects[obj.id.instance()] );
         }

         virtual const object& insert( object&& obj )override
         {
            auto instance = obj.id.instance();
            assert( nullptr != dynamic_cast<T*>(&obj) );
            if( _objects.size() <= instance ) _objects.resize( instance+1 );
            assert( !_objects[instance] );
            _objects[instance].reset( new T( std::move( static_cast<T&>(obj) ) ) );
            return *_objects[instance];
         }

         virtual void remove( const object& obj ) override
         {
            assert( nullptr != dynamic_cast<const T*>(&obj) );
            const auto instance = obj.id.instance();
            _objects[instance].reset();
            while( (_objects.size() > 0) && (_objects.back() == nullptr) )
               _objects.pop_back();
         }

         virtual const object* find( object_id_type id )const override
         {
            assert( id.space() == T::space_id );
            assert( id.type() == T::type_id );

            const auto instance = id.instance();
            if( instance >= _objects.size() ) return nullptr;
            return _objects[instance].get();
         }

         virtual void inspect_all_objects(std::function<void (const object&)> inspector)const override
         {
            try {
               for( const auto& ptr : _objects )
               {
                  if( ptr.get() )
                     inspector(*ptr);
               }
            } FC_CAPTURE_AND_RETHROW()
         }
         virtual fc::uint128 hash()const override {
            fc::uint128 result;
            for( const auto& ptr : _objects )
               result += ptr->hash();

            return result;
         }

         class const_iterator
         {
            public:
               const_iterator( const vector<unique_ptr<object>>& objects ):_objects(objects) {}
               const_iterator(
                  const vector<unique_ptr<object>>& objects,
                  const vector<unique_ptr<object>>::const_iterator& a ):_itr(a),_objects(objects){}
               friend bool operator==( const const_iterator& a, const const_iterator& b ) { return a._itr == b._itr; }
               friend bool operator!=( const const_iterator& a, const const_iterator& b ) { return a._itr != b._itr; }
               const T& operator*()const { return static_cast<const T&>(*_itr->get()); }
               const_iterator operator++(int)     // postfix
               {
                  const_iterator result( *this );
                  ++(*this);
                  return result;
               }
               const_iterator& operator++()       // prefix
               {
                  ++_itr;
                  while( (_itr != _objects.end()) && ( (*_itr) == nullptr ) )
                     ++_itr;
                  return *this;
               }
               typedef std::forward_iterator_tag iterator_category;
               typedef vector<unique_ptr<object> >::value_type value_type;
               typedef vector<unique_ptr<object> >::difference_type difference_type;
               typedef vector<unique_ptr<object> >::pointer pointer;
               typedef vector<unique_ptr<object> >::reference reference;
            private:
               vector<unique_ptr<object>>::const_iterator _itr;
               const vector<unique_ptr<object>>& _objects;
         };
         const_iterator begin()const { return const_iterator(_objects, _objects.begin()); }
         const_iterator end()const   { return const_iterator(_objects, _objects.end());   }

         size_t size()const { return _objects.size(); }
      private:
         vector< unique_ptr<object> > _objects;
   };

} } // graphene::db
