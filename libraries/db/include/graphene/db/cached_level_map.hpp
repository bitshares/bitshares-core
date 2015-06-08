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
#include <graphene/db/level_map.hpp>
#include <fc/thread/thread.hpp>
#include <map>

namespace graphene { namespace db {

   template<typename Key, typename Value, class CacheType = std::map<Key,Value>>
   class cached_level_map
   {
      public:
        void open( const fc::path& dir, bool create = true, size_t leveldb_cache_size = 0, bool write_through = true, bool sync_on_write = false )
        { try {
            _db.open( dir, create, leveldb_cache_size );
            for( auto itr = _db.begin(); itr.valid(); ++itr )
                _cache.emplace_hint( _cache.end(), itr.key(), itr.value() );
            _write_through = write_through;
            _sync_on_write = sync_on_write;
        } FC_CAPTURE_AND_RETHROW( (dir)(create)(leveldb_cache_size)(write_through)(sync_on_write) ) }

        void close()
        { try {
            if( _db.is_open() ) flush();
            _db.close();
            _cache.clear();
            _dirty_store.clear();
            _dirty_remove.clear();
        } FC_CAPTURE_AND_RETHROW() }

        void set_write_through( bool write_through )
        { try {
            if( write_through == _write_through )
                return;

            if( write_through )
                flush();

            _write_through = write_through;
        } FC_CAPTURE_AND_RETHROW( (write_through) ) }

        void flush()
        { try {
            typename level_map<Key, Value>::write_batch batch = _db.create_batch( _sync_on_write );
            for( const auto& key : _dirty_store )
                batch.store( key, _cache.at( key ) );
            for( const auto& key : _dirty_remove )
                batch.remove( key );
            batch.commit();

            _dirty_store.clear();
            _dirty_remove.clear();
        } FC_CAPTURE_AND_RETHROW() }

        fc::optional<Value> fetch_optional( const Key& key )const
        { try {
            const auto itr = _cache.find( key );
            if( itr != _cache.end() )
                return itr->second;
            return fc::optional<Value>();
        } FC_CAPTURE_AND_RETHROW( (key) ) }

        Value fetch( const Key& key )const
        { try {
            const auto itr = _cache.find( key );
            if( itr != _cache.end() )
                return itr->second;
            FC_CAPTURE_AND_THROW( fc::key_not_found_exception, (key) );
        } FC_CAPTURE_AND_RETHROW( (key) ) }

        void store( const Key& key, const Value& value )
        { try {
            _cache[ key ] = value;
            if( _write_through )
            {
                _db.store( key, value, _sync_on_write );
            }
            else
            {
                _dirty_store.insert( key );
                _dirty_remove.erase( key );
            }
        } FC_CAPTURE_AND_RETHROW( (key)(value) ) }

        void remove( const Key& key )
        { try {
            _cache.erase( key );
            if( _write_through )
            {
                _db.remove( key, _sync_on_write );
            }
            else
            {
                _dirty_store.erase( key );
                _dirty_remove.insert( key );
            }
        } FC_CAPTURE_AND_RETHROW( (key) ) }

        size_t size()const
        { try {
            return _cache.size();
        } FC_CAPTURE_AND_RETHROW() }

        bool last( Key& key )const
        { try {
            const auto ritr = _cache.crbegin();
            if( ritr != _cache.crend() )
            {
                key = ritr->first;
                return true;
            }
            return false;
        } FC_CAPTURE_AND_RETHROW( (key) ) }

        bool last( Key& key, Value& value )
        { try {
            const auto ritr = _cache.crbegin();
            if( ritr != _cache.crend() )
            {
                key = ritr->first;
                value = ritr->second;
                return true;
            }
            return false;
        } FC_CAPTURE_AND_RETHROW( (key)(value) ) }

        class iterator
        {
           public:
             iterator(){}
             bool valid()const { return _it != _end; }

             Key   key()const { return _it->first; }
             Value value()const { return _it->second; }

             iterator& operator++()    { ++_it; return *this; }
             iterator  operator++(int) {
                auto backup = *this;
                ++_it;
                return backup;
             }

             iterator& operator--()
             {
                if( _it == _begin )
                   _it = _end;
                else
                   --_it;
                return *this;
             }

             iterator  operator--(int) {
                auto backup = *this;
                operator--();
                return backup;
             }

             void reset() { _it = _end; }

           protected:
             friend class cached_level_map;
             iterator( typename CacheType::const_iterator it, typename CacheType::const_iterator begin, typename CacheType::const_iterator end )
             :_it(it),_begin(begin),_end(end)
             { }

             typename CacheType::const_iterator _it;
             typename CacheType::const_iterator _begin;
             typename CacheType::const_iterator _end;
        };

        iterator begin()const
        {
           return iterator( _cache.begin(), _cache.begin(), _cache.end() );
        }

        iterator last()const
        {
           if( _cache.empty() )
              return iterator( _cache.end(), _cache.begin(), _cache.end() );
           return iterator( --_cache.end(), _cache.begin(), _cache.end() );
        }

        iterator find( const Key& key )const
        {
           return iterator( _cache.find(key), _cache.begin(), _cache.end() );
        }

        iterator lower_bound( const Key& key )const
        {
           return iterator( _cache.lower_bound(key), _cache.begin(), _cache.end() );
        }

        // TODO: Iterate over cache instead
        void export_to_json( const fc::path& path )const
        { try {
            _db.export_to_json( path );
        } FC_CAPTURE_AND_RETHROW( (path) ) }

      private:
        level_map<Key, Value>    _db;
        CacheType                _cache;
        std::set<Key>            _dirty_store;
        std::set<Key>            _dirty_remove;
        bool                     _write_through = true;
        bool                     _sync_on_write = false;
   };

} }
