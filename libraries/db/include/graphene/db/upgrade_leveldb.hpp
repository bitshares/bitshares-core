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
#include <leveldb/db.h>
#include <leveldb/comparator.h>
#include <fc/reflect/reflect.hpp>
#include <fc/io/raw.hpp>
#include <fc/exception/exception.hpp>
#include <functional>
#include <map>

namespace fc { class path; }

/**
 * This code has no graphene dependencies, and it
 * could be moved to fc, if fc ever adds a leveldb dependency
 *
 * This code enables legacy databases files created by older programs to
 * be upgraded to the current database formats. Whenever a database is first opened,
 * this code checks if the database is stored in an old format and looks for an
 * upgrade function to upgrade it to the current format. If found, the objects
 * in the database will be immediately upgraded to the current format.
 *
 * Upgrades are performed by executing a series of chained copy constructors
 * from the legacy object format to the current object format. This means
 * that only one new copy constructor typically needs to be written to support
 * upgrading any previous version of the object when an object type is modified.
 * 
 * - Database versioning is only supported for changes to database value types
 *   (databases with modified key types cannot currently be upgraded).
 * - The database versioning code requires that fc::get_typename is defined for
 *   all value types which are to be versioned.
 */

/*
    Below is a simple example of how client code needs to be written to support
    database versioning. Originally, a database stored values of record0, and
    record was typedef'd to be record0. A new type record1 was created to add
    "new_field" to record type, and record was typedef'd to record1. The typedef
    is used to minimize required changes to the client code that references
    record objects.
   
    @code

    struct record0
    {
        record0() : points(0) {}
        double    points;
    };

    FC_REFLECT( record0, (points) )
    REGISTER_DB_OBJECT(record,0) //This creates an upgrade function for record0 databases

    struct record1
    {
        record1() : points(0), new_field("EMPTY") {}
        
        record1(const record0& r0) //convert from record0 to record1 for legacy files
          {
          key = r0.key;
          new_field = "EMPTY";
          }
        std::string new_field; 
        double    points;
    };
    FC_REFLECT( record1, (points)(new_field) )

    typedef record1 record; //current databases store record1 objects

    @endcode
*/

namespace graphene { namespace db {

    typedef std::function<void(leveldb::DB*)> upgrade_db_function; 

    class upgrade_db_mapper
    {
        public:
          static  upgrade_db_mapper& instance();
          int32_t add_type( const std::string& type_name, const upgrade_db_function& function);

          std::map<std::string,upgrade_db_function> _upgrade_db_function_registry;
    };

    #define REGISTER_DB_OBJECT(TYPE,VERSIONNUM) \
        void UpgradeDb ## TYPE ## VERSIONNUM(leveldb::DB* dbase) \
        { \
          std::unique_ptr<leveldb::Iterator> dbase_itr( dbase->NewIterator(leveldb::ReadOptions()) ); \
          dbase_itr->SeekToFirst(); \
          if( dbase_itr->status().IsNotFound() ) /*if empty database, do nothing*/ \
            return; \
          if( !dbase_itr->status().ok() ) \
            FC_THROW_EXCEPTION( exception, "database error: ${msg}", ("msg", dbase_itr->status().ToString() ) ); \
          while( dbase_itr->Valid() ) /* convert dbase objects from legacy TypeVersionNum to current Type */ \
          { \
            TYPE ## VERSIONNUM old_value; /*load old record type*/ \
            fc::datastream<const char*> dstream( dbase_itr->value().data(), dbase_itr->value().size() ); \
            fc::raw::unpack( dstream, old_value ); \
            TYPE new_value(old_value);       /*convert to new record type*/ \
            leveldb::Slice key_slice = dbase_itr->key(); \
            auto vec = fc::raw::pack(new_value); \
            leveldb::Slice value_slice( vec.data(), vec.size() ); \
            auto status = dbase->Put( leveldb::WriteOptions(), key_slice, value_slice ); \
            if( !status.ok() ) \
            { \
              FC_THROW_EXCEPTION( exception, "database error: ${msg}", ("msg", status.ToString() ) ); \
            } \
            dbase_itr->Next(); \
          } /*while*/ \
        } \
        static int dummyResult ## TYPE ## VERSIONNUM  = \
          upgrade_db_mapper::instance()->add_type(fc::get_typename<TYPE ## VERSIONNUM>::name(), UpgradeDb ## TYPE ## VERSIONNUM);

    void try_upgrade_db( const fc::path& dir, leveldb::DB* dbase, const char* record_type, size_t record_type_size );

} } // namespace db
