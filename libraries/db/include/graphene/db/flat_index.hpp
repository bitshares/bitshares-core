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
#include <graphene/db/index.hpp>

namespace graphene { namespace db {

   /**
    *  @class flat_index
    *  @brief A flat index uses a vector<T> to store data
    *
    *  This index is preferred in situations where the data will never be
    *  removed from main memory and when lots of small objects that
    *  are accessed in order are required.
    */
   template<typename T>
   class flat_index : public index
   {
      public:
         typedef T object_type;

         virtual const object&  create( const std::function<void(object&)>& constructor ) override
         {
             auto id = get_next_id();
             auto instance = id.instance();
             if( instance >= _objects.size() ) _objects.resize( instance + 1 );
             _objects[instance].id = id;
             constructor( _objects[instance] );
             use_next_id();
             return _objects[instance];
         }

         virtual void modify( const object& obj, const std::function<void(object&)>& modify_callback ) override
         {
            assert( obj.id.instance() < _objects.size() );
            modify_callback( _objects[obj.id.instance()] );
         }

         virtual const object& insert( object&& obj )override
         {
            auto instance = obj.id.instance();
            assert( nullptr != dynamic_cast<T*>(&obj) );
            if( _objects.size() <= instance ) _objects.resize( instance+1 );
            _objects[instance] = std::move( static_cast<T&>(obj) );
            return _objects[instance];
         }

         virtual void remove( const object& obj ) override
         {
            assert( nullptr != dynamic_cast<const T*>(&obj) );
            const auto instance = obj.id.instance();
            _objects[instance] = T();
         }

         virtual const object* find( object_id_type id )const override
         {
            assert( id.space() == T::space_id );
            assert( id.type() == T::type_id );

            const auto instance = id.instance();
            if( instance >= _objects.size() ) return nullptr;
            return &_objects[instance];
         }

         virtual void inspect_all_objects(std::function<void (const object&)> inspector)const override
         {
            try {
               for( const auto& ptr : _objects )
               {
                  inspector(ptr);
               }
            } FC_CAPTURE_AND_RETHROW()
         }

         virtual fc::uint128 hash()const override {
            fc::uint128 result;
            for( const auto& ptr : _objects )
               result += ptr.hash();

            return result;
         }

         class const_iterator
         {
            public:
               const_iterator(){}
               const_iterator( const typename vector<T>::const_iterator& a ):_itr(a){}
               friend bool operator==( const const_iterator& a, const const_iterator& b ) { return a._itr == b._itr; }
               friend bool operator!=( const const_iterator& a, const const_iterator& b ) { return a._itr != b._itr; }
               const T* operator*()const { return static_cast<const T*>(&*_itr); }
               const_iterator& operator++(int){ ++_itr; return *this; }
               const_iterator& operator++()   { ++_itr; return *this; }
            private:
               typename vector<T>::const_iterator _itr;
         };
         const_iterator begin()const { return const_iterator(_objects.begin()); }
         const_iterator end()const   { return const_iterator(_objects.end());   }

         size_t size()const{ return _objects.size(); }

         void resize( uint32_t s ) { 
            _objects.resize(s); 
            for( uint32_t i = 0; i < s; ++i )
               _objects[i].id = object_id_type(object_type::space_id,object_type::type_id,i);
         }

      private:
         vector< T > _objects;
   };

} } // graphene::db
