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

#include <leveldb/cache.h>
#include <leveldb/comparator.h>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include <graphene/db/exception.hpp>
#include <graphene/db/upgrade_leveldb.hpp>

#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/io/raw.hpp>
#include <fc/reflect/reflect.hpp>

#include <fstream>

namespace graphene { namespace db {

  namespace ldb = leveldb;

  /**
   *  @brief implements a high-level API on top of Level DB that stores items using fc::raw / reflection
   */
  template<typename Key, typename Value>
  class level_map
  {
     public:
        void open( const fc::path& dir, bool create = true, size_t cache_size = 0 )
        { try {
           idump( (dir)(create));
           FC_ASSERT( !is_open(), "Database is already open!" );

           ldb::Options opts;
           opts.comparator = &_comparer;
           opts.create_if_missing = create;
           opts.max_open_files = 64;
           opts.compression = leveldb::kNoCompression;

           if( cache_size > 0 )
           {
               opts.write_buffer_size = cache_size / 4; // up to two write buffers may be held in memory simultaneously
               _cache.reset( leveldb::NewLRUCache( cache_size / 2 ) );
               opts.block_cache = _cache.get();
           }

           if( ldb::kMajorVersion > 1 || ( leveldb::kMajorVersion == 1 && leveldb::kMinorVersion >= 16 ) )
           {
               // LevelDB versions before 1.16 consider short writes to be corruption. Only trigger error
               // on corruption in later versions.
               opts.paranoid_checks = true;
           }

           _read_options.verify_checksums = true;
           _iter_options.verify_checksums = true;
           _iter_options.fill_cache = false;
           _sync_options.sync = true;

           // Given path must exist to succeed toNativeAnsiPath
           fc::create_directories( dir );
           std::string ldbPath = dir.to_native_ansi_path();

           ldb::DB* ndb = nullptr;
           const auto ntrxstat = ldb::DB::Open( opts, ldbPath.c_str(), &ndb );
           if( !ntrxstat.ok() )
           {
               elog( "Failure opening database: ${db}\nStatus: ${msg}", ("db",dir)("msg",ntrxstat.ToString()) );
               FC_THROW_EXCEPTION( level_map_open_failure, "Failure opening database: ${db}\nStatus: ${msg}",
                                   ("db",dir)("msg",ntrxstat.ToString()) );
           }
           _db.reset( ndb );

           try_upgrade_db( dir, ndb, fc::get_typename<Value>::name(), sizeof( Value ) );
        } FC_CAPTURE_AND_RETHROW( (dir)(create)(cache_size) ) }

        bool is_open()const
        {
          return !!_db;
        }

        void close()
        {
          _db.reset();
          _cache.reset();
        }

        fc::optional<Value> fetch_optional( const Key& k )const
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           auto itr = find( k );
           if( itr.valid() ) return itr.value();
           return fc::optional<Value>();
        } FC_RETHROW_EXCEPTIONS( warn, "" ) }

        Value fetch( const Key& k )
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::vector<char> kslice = fc::raw::pack( k );
           ldb::Slice ks( kslice.data(), kslice.size() );
           std::string value;
           auto status = _db->Get( _read_options, ks, &value );
           if( status.IsNotFound() )
           {
             FC_THROW_EXCEPTION( fc::key_not_found_exception, "unable to find key ${key}", ("key",k) );
           }
           if( !status.ok() )
           {
               FC_THROW_EXCEPTION( level_map_failure, "database error: ${msg}", ("msg", status.ToString() ) );
           }
           fc::datastream<const char*> ds(value.c_str(), value.size());
           Value tmp;
           fc::raw::unpack(ds, tmp);
           return tmp;
        } FC_RETHROW_EXCEPTIONS( warn, "failure fetching key ${key}", ("key",k) ); }

        class iterator
        {
           public:
             iterator(){}
             bool valid()const
             {
                return _it && _it->Valid();
             }

             Key key()const
             {
                 Key tmp_key;
                 fc::datastream<const char*> ds2( _it->key().data(), _it->key().size() );
                 fc::raw::unpack( ds2, tmp_key );
                 return tmp_key;
             }

             Value value()const
             {
               Value tmp_val;
               fc::datastream<const char*> ds( _it->value().data(), _it->value().size() );
               fc::raw::unpack( ds, tmp_val );
               return tmp_val;
             }

             iterator& operator++()    { _it->Next(); return *this; }
             iterator& operator--()    { _it->Prev(); return *this; }

           protected:
             friend class level_map;
             iterator( ldb::Iterator* it )
             :_it(it){}

             std::shared_ptr<ldb::Iterator> _it;
        };

        iterator begin() const
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           iterator itr( _db->NewIterator( _iter_options ) );
           itr._it->SeekToFirst();

           if( itr._it->status().IsNotFound() )
           {
             FC_THROW_EXCEPTION( fc::key_not_found_exception, "" );
           }
           if( !itr._it->status().ok() )
           {
               FC_THROW_EXCEPTION( level_map_failure, "database error: ${msg}", ("msg", itr._it->status().ToString() ) );
           }

           if( itr.valid() )
           {
              return itr;
           }
           return iterator();
        } FC_RETHROW_EXCEPTIONS( warn, "error seeking to first" ) }

        iterator find( const Key& key )const
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           ldb::Slice key_slice;

           /** avoid dynamic memory allocation at this step if possible, most
            * keys should be relatively small in size and not require dynamic
            * memory allocation to seralize the key.
            */
           fc::array<char,256+sizeof(Key)>  stack_buffer;

           size_t pack_size = fc::raw::pack_size(key);
           if( pack_size <= stack_buffer.size() )
           {
              fc::datastream<char*> ds( stack_buffer.data, stack_buffer.size() );
              fc::raw::pack( ds ,key );
              key_slice = ldb::Slice( stack_buffer.data, pack_size );
           }
           else
           {
              auto kslice = fc::raw::pack( key );
              key_slice = ldb::Slice( kslice.data(), kslice.size() );
           }

           iterator itr( _db->NewIterator( _iter_options ) );
           itr._it->Seek( key_slice );
           if( itr.valid() && itr.key() == key )
           {
              return itr;
           }
           return iterator();
        } FC_RETHROW_EXCEPTIONS( warn, "error finding ${key}", ("key",key) ) }

        iterator lower_bound( const Key& key )const
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::vector<char> kslice = fc::raw::pack( key );
           ldb::Slice key_slice( kslice.data(), kslice.size() );

           iterator itr( _db->NewIterator( _iter_options ) );
           itr._it->Seek( key_slice );
           return itr;
        } FC_RETHROW_EXCEPTIONS( warn, "error finding ${key}", ("key",key) ) }

        iterator last( )const
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           iterator itr( _db->NewIterator( _iter_options ) );
           itr._it->SeekToLast();
           return itr;
        } FC_RETHROW_EXCEPTIONS( warn, "error finding last" ) }

        bool last( Key& k )
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::unique_ptr<ldb::Iterator> it( _db->NewIterator( _iter_options ) );
           FC_ASSERT( it != nullptr );
           it->SeekToLast();
           if( !it->Valid() )
           {
             return false;
           }
           fc::datastream<const char*> ds2( it->key().data(), it->key().size() );
           fc::raw::unpack( ds2, k );
           return true;
        } FC_RETHROW_EXCEPTIONS( warn, "error reading last item from database" ); }

        bool last( Key& k, Value& v )
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::unique_ptr<ldb::Iterator> it( _db->NewIterator( _iter_options ) );
           FC_ASSERT( it != nullptr );
           it->SeekToLast();
           if( !it->Valid() )
           {
             return false;
           }
           fc::datastream<const char*> ds( it->value().data(), it->value().size() );
           fc::raw::unpack( ds, v );

           fc::datastream<const char*> ds2( it->key().data(), it->key().size() );
           fc::raw::unpack( ds2, k );
           return true;
        } FC_RETHROW_EXCEPTIONS( warn, "error reading last item from database" ); }

        /** this class allows batched, atomic database writes.
         *  usage:
         *  {
         *    write_batch batch = _db.create_batch();
         *    batch.store(key1, value1);
         *    batch.store(key2, value2);
         *  }
         *  when the batch goes out of scope, the operations are commited to the database
         */
        class write_batch
        {
            private:
                leveldb::WriteBatch   _batch;
                level_map*            _map = nullptr;
                leveldb::WriteOptions _write_options;

                friend class level_map;
                write_batch( level_map* map, bool sync = false ) : _map(map)
                {
                    _write_options.sync = sync;
                }
            public:
                ~write_batch()
                {
                  try
                  {
                    commit();
                  }
                  catch (const fc::canceled_exception&)
                  {
                    throw;
                  }
                  catch (const fc::exception&)
                  {
                    // we're in a destructor, nothing we can do...
                  }
                }

                void commit()
                {
                  try
                  {
                    FC_ASSERT(_map->is_open(), "Database is not open!");

                    ldb::Status status = _map->_db->Write( _write_options, &_batch );
                    if (!status.ok())
                      FC_THROW_EXCEPTION(level_map_failure, "database error while applying batch: ${msg}", ("msg", status.ToString()));
                    _batch.Clear();
                  }
                  FC_RETHROW_EXCEPTIONS(warn, "error applying batch");
                }

                void abort()
                {
                  _batch.Clear();
                }

                void store( const Key& k, const Value& v )
                {
                  std::vector<char> kslice = fc::raw::pack(k);
                  ldb::Slice ks(kslice.data(), kslice.size());

                  auto vec = fc::raw::pack(v);
                  ldb::Slice vs(vec.data(), vec.size());

                  _batch.Put(ks, vs);
                }

                void remove( const Key& k )
                {
                  std::vector<char> kslice = fc::raw::pack(k);
                  ldb::Slice ks(kslice.data(), kslice.size());
                  _batch.Delete(ks);
                }
        };

        write_batch create_batch( bool sync = false )
        {
           FC_ASSERT( is_open(), "Database is not open!" );
           return write_batch( this, sync );
        }

        void store(const Key& k, const Value& v, bool sync = false)
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::vector<char> kslice = fc::raw::pack( k );
           ldb::Slice ks( kslice.data(), kslice.size() );

           auto vec = fc::raw::pack(v);
           ldb::Slice vs( vec.data(), vec.size() );

           auto status = _db->Put( sync ? _sync_options : _write_options, ks, vs );
           if( !status.ok() )
           {
               FC_THROW_EXCEPTION( level_map_failure, "database error: ${msg}", ("msg", status.ToString() ) );
           }
        } FC_RETHROW_EXCEPTIONS( warn, "error storing ${key} = ${value}", ("key",k)("value",v) ); }

        void remove( const Key& k, bool sync = false )
        { try {
           FC_ASSERT( is_open(), "Database is not open!" );

           std::vector<char> kslice = fc::raw::pack( k );
           ldb::Slice ks( kslice.data(), kslice.size() );
           auto status = _db->Delete( sync ? _sync_options : _write_options, ks );
           if( !status.ok() )
           {
               FC_THROW_EXCEPTION( level_map_failure, "database error: ${msg}", ("msg", status.ToString() ) );
           }
        } FC_RETHROW_EXCEPTIONS( warn, "error removing ${key}", ("key",k) ); }

        void export_to_json( const fc::path& path )const
        { try {
            FC_ASSERT( is_open(), "Database is not open!" );
            FC_ASSERT( !fc::exists( path ) );

            std::ofstream fs( path.string() );
            fs.write( "[\n", 2 );

            auto iter = begin();
            while( iter.valid() )
            {
                auto str = fc::json::to_pretty_string( std::make_pair( iter.key(), iter.value() ) );
                if( (++iter).valid() ) str += ",";
                str += "\n";
                fs.write( str.c_str(), str.size() );
            }

            fs.write( "]", 1 );
        } FC_CAPTURE_AND_RETHROW( (path) ) }

        // note: this loops through all the items in the database, so it's not exactly fast.  it's intended for debugging, nothing else.
        size_t size() const
        {
          FC_ASSERT( is_open(), "Database is not open!" );

          iterator it = begin();
          size_t count = 0;
          while (it.valid())
          {
            ++count;
            ++it;
          }
          return count;
        }

     private:
        class key_compare : public leveldb::Comparator
        {
          public:
            int Compare( const leveldb::Slice& a, const leveldb::Slice& b )const
            {
               Key ak,bk;
               fc::datastream<const char*> dsa( a.data(), a.size() );
               fc::raw::unpack( dsa, ak );
               fc::datastream<const char*> dsb( b.data(), b.size() );
               fc::raw::unpack( dsb, bk );

               if( ak  < bk ) return -1;
               if( ak == bk ) return 0;
               return 1;
            }

            const char* Name()const { return "key_compare"; }
            void FindShortestSeparator( std::string*, const leveldb::Slice& )const{}
            void FindShortSuccessor( std::string* )const{};
        };

        std::unique_ptr<leveldb::DB>    _db;
        std::unique_ptr<leveldb::Cache> _cache;
        key_compare                     _comparer;

        ldb::ReadOptions                _read_options;
        ldb::ReadOptions                _iter_options;
        ldb::WriteOptions               _write_options;
        ldb::WriteOptions               _sync_options;
  };

} } // graphene::db
