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
#include <graphene/db/object.hpp>
#include <graphene/db/index.hpp>
#include <graphene/db/undo_database.hpp>

#include <fc/log/logger.hpp>

#include <map>

namespace graphene { namespace db {

   /**
    *   @class object_database
    *   @brief maintains a set of indexed objects that can be modified with multi-level rollback support
    */
   class object_database
   {
      public:
         object_database();
         virtual ~object_database() = default;

         static constexpr uint8_t _index_size = 255;

         void reset_indexes()
         {
            _index.clear();
            _index.resize(_index_size);
         }

         void open(const fc::path& data_dir );

         /**
          * Saves the complete state of the object_database to disk, this could take a while
          */
         void flush();
         void wipe(const fc::path& data_dir); // remove from disk
         void close();

         template<typename T, typename F>
         const T& create( F&& constructor )
         {
            auto& idx = get_mutable_index<T>();
            return static_cast<const T&>( idx.create( [&](object& o)
            {
               assert( dynamic_cast<T*>(&o) );
               constructor( static_cast<T&>(o) );
            } ));
         }

         /// These methods are used to retrieve indexes on the object_database. All public index accessors are
         /// const-access only.
         /// @{
         template<typename IndexType>
         const IndexType& get_index_type()const {
            static_assert( std::is_base_of<index,IndexType>::value, "Type must be an index type" );
            return static_cast<const IndexType&>( get_index( IndexType::object_type::space_id,
                                                             IndexType::object_type::type_id ) );
         }
         template<typename T>
         const index&  get_index()const { return get_index(T::space_id,T::type_id); }
         const index&  get_index(uint8_t space_id, uint8_t type_id)const;
         const index&  get_index(const object_id_type& id)const { return get_index(id.space(),id.type()); }
         /// @}

         const object& get_object( const object_id_type& id )const;
         const object* find_object( const object_id_type& id )const;

         /// These methods are mutators of the object_database.
         /// You must use these methods to make changes to the object_database,
         /// in order to maintain proper undo history.
         ///@{

         const object& insert( object&& obj ) { return get_mutable_index(obj.id).insert( std::move(obj) ); }
         void          remove( const object& obj ) { get_mutable_index(obj.id).remove( obj ); }
         template<typename T, typename Lambda>
         void modify( const T& obj, const Lambda& m ) {
            get_mutable_index(obj.id).modify(obj,m);
         }

         ///@}

         template<typename T>
         static const T& cast( const object& obj )
         {
            assert( nullptr != dynamic_cast<const T*>(&obj) );
            return static_cast<const T&>(obj);
         }
         template<typename T>
         static T& cast( object& obj )
         {
            assert( nullptr != dynamic_cast<T*>(&obj) );
            return static_cast<T&>(obj);
         }

         template<typename T>
         const T& get( const object_id_type& id )const
         {
            const object& obj = get_object( id );
            assert( nullptr != dynamic_cast<const T*>(&obj) );
            return static_cast<const T&>(obj);
         }
         template<typename T>
         const T* find( const object_id_type& id )const
         {
            const object* obj = find_object( id );
            assert(  !obj || nullptr != dynamic_cast<const T*>(obj) );
            return static_cast<const T*>(obj);
         }

         template<uint8_t SpaceID, uint8_t TypeID>
         auto find( const object_id<SpaceID,TypeID>& id )const -> const object_downcast_t<decltype(id)>* {
             return find<object_downcast_t<decltype(id)>>(object_id_type(id));
         }

         template<uint8_t SpaceID, uint8_t TypeID>
         auto get( const object_id<SpaceID,TypeID>& id )const -> const object_downcast_t<decltype(id)>& {
             return get<object_downcast_t<decltype(id)>>(object_id_type(id));
         }

         template<typename IndexType>
         IndexType* add_index()
         {
            using ObjectType = typename IndexType::object_type;
            const auto space_id = ObjectType::space_id;
            const auto type_id = ObjectType::type_id;
            FC_ASSERT( space_id < _index.size(), "Space ID ${s} overflow", ("s",space_id) );
            if( _index[space_id].size() <= type_id )
                _index[space_id].resize( _index_size );
            FC_ASSERT( type_id < _index[space_id].size(), "Type ID ${t} overflow", ("t",type_id) );
            FC_ASSERT( !_index[space_id][type_id], "Index ${s}.${t} already exists", ("s",space_id)("t",type_id) );
            _index[space_id][type_id] = std::make_unique<IndexType>(*this);
            return static_cast<IndexType*>(_index[space_id][type_id].get());
         }

         template<typename IndexType, typename SecondaryIndexType, typename... Args>
         SecondaryIndexType* add_secondary_index( Args... args )
         {
            return get_mutable_index_type<IndexType>().template
                      add_secondary_index<SecondaryIndexType, Args...>(args...);
         }

         void pop_undo();

         fc::path get_data_dir()const { return _data_dir; }

         /** public for testing purposes only... should be private in practice. */
         undo_database                          _undo_db;
     protected:
         template<typename IndexType>
         IndexType&    get_mutable_index_type() {
            static_assert( std::is_base_of<index,IndexType>::value, "Type must be an index type" );
            return static_cast<IndexType&>( get_mutable_index( IndexType::object_type::space_id,
                                                               IndexType::object_type::type_id ) );
         }
         template<typename T>
         index& get_mutable_index()                          { return get_mutable_index(T::space_id,T::type_id); }
         index& get_mutable_index(const object_id_type& id)  { return get_mutable_index(id.space(),id.type());   }
         index& get_mutable_index(uint8_t space_id, uint8_t type_id);

     private:

         friend class base_primary_index;
         friend class undo_database;
         void save_undo( const object& obj );
         void save_undo_add( const object& obj );
         void save_undo_remove( const object& obj );

         fc::path                                                  _data_dir;
         std::vector< std::vector< std::unique_ptr<index> > >      _index;
   };

} } // graphene::db


