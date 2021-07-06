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
#include <graphene/db/object_database.hpp>

#include <fc/io/raw.hpp>
#include <fc/container/flat.hpp>
#include <fc/thread/parallel.hpp>

namespace graphene { namespace db {

object_database::object_database()
:_undo_db(*this)
{
   _index.resize(255);
   _undo_db.enable();
}

object_database::~object_database(){}

void object_database::close()
{
}

const object* object_database::find_object( object_id_type id )const
{
   return get_index(id.space(),id.type()).find( id );
}
const object& object_database::get_object( object_id_type id )const
{
   return get_index(id.space(),id.type()).get( id );
}

const index& object_database::get_index(uint8_t space_id, uint8_t type_id)const
{
   FC_ASSERT( _index.size() > space_id,
              "Database index ${space_id}.${type_id} does not exist, index size is ${index.size}",
              ("space_id",space_id)("type_id",type_id)("index.size",_index.size()) );
   FC_ASSERT( _index[space_id].size() > type_id,
              "Database index ${space_id}.${type_id} does not exist, space size is ${index[space_id].size}",
              ("space_id",space_id)("type_id",type_id)("index[space_id].size",_index[space_id].size()) );
   const auto& tmp = _index[space_id][type_id]; // it is a unique_ptr
   FC_ASSERT( tmp != nullptr,
              "Database index ${space_id}.${type_id} has not been initialized",
              ("space_id",space_id)("type_id",type_id) );
   return *tmp;
}
index& object_database::get_mutable_index(uint8_t space_id, uint8_t type_id)
{
   FC_ASSERT( _index.size() > space_id,
              "Database index ${space_id}.${type_id} does not exist, index size is ${index.size}",
              ("space_id",space_id)("type_id",type_id)("index.size",_index.size()) );
   FC_ASSERT( _index[space_id].size() > type_id ,
              "Database index ${space_id}.${type_id} does not exist, space size is ${index[space_id].size}",
              ("space_id",space_id)("type_id",type_id)("index[space_id].size",_index[space_id].size()) );
   const auto& idx = _index[space_id][type_id]; // it is a unique_ptr
   FC_ASSERT( idx != nullptr,
              "Database index ${space_id}.${type_id} has not been initialized",
              ("space_id",space_id)("type_id",type_id) );
   return *idx;
}

void object_database::flush()
{
   const auto tmp_dir = _data_dir / "object_database.tmp";
   const auto old_dir = _data_dir / "object_database.old";
   const auto target_dir = _data_dir / "object_database";

   if( fc::exists( tmp_dir ) )
      fc::remove_all( tmp_dir );
   fc::create_directories( tmp_dir / "lock" );
   std::vector<fc::future<void>> tasks;
   constexpr size_t max_tasks = 200;
   tasks.reserve(max_tasks);

   auto push_task = [this,&tasks,&tmp_dir]( size_t space, size_t type ) {
      if( _index[space][type] )
         tasks.push_back( fc::do_parallel( [this,space,type,&tmp_dir] () {
            _index[space][type]->save( tmp_dir / fc::to_string(space) / fc::to_string(type) );
         } ) );
   };

   const auto spaces = _index.size();
   for( size_t space = 0; space < spaces; ++space )
   {
      fc::create_directories( tmp_dir / fc::to_string(space) );
      const auto types = _index[space].size();
      for( size_t type = 0; type  <  types; ++type )
         push_task( space, type );
   }
   for( auto& task : tasks )
      task.wait();
   fc::remove_all( tmp_dir / "lock" );
   if( fc::exists( target_dir ) )
   {
      if( fc::exists( old_dir ) )
         fc::remove_all( old_dir );
      fc::rename( target_dir, old_dir );
   }
   fc::rename( tmp_dir, target_dir );
   fc::remove_all( old_dir );
}

void object_database::wipe(const fc::path& data_dir)
{
   close();
   ilog("Wiping object database...");
   fc::remove_all(data_dir / "object_database");
   ilog("Done wiping object database.");
}

void object_database::open(const fc::path& data_dir)
{ try {
   _data_dir = data_dir;
   if( fc::exists( _data_dir / "object_database" / "lock" ) )
   {
       wlog("Ignoring locked object_database");
       return;
   }
   std::vector<fc::future<void>> tasks;
   tasks.reserve(200);

   auto push_task = [this,&tasks]( size_t space, size_t type ) {
      if( _index[space][type] )
         tasks.push_back( fc::do_parallel( [this,space,type] () {
            _index[space][type]->open( _data_dir / "object_database" / fc::to_string(space) / fc::to_string(type) );
         } ) );
   };

   ilog("Opening object database from ${d} ...", ("d", data_dir));
   const auto spaces = _index.size();
   for( size_t space = 0; space < spaces; ++space )
   {
      const auto types = _index[space].size();
      for( size_t type = 0; type  < types; ++type )
         push_task( space, type );
   }
   for( auto& task : tasks )
      task.wait();
   ilog( "Done opening object database." );

} FC_CAPTURE_AND_RETHROW( (data_dir) ) }


void object_database::pop_undo()
{ try {
   _undo_db.pop_commit();
} FC_CAPTURE_AND_RETHROW() }

void object_database::save_undo( const object& obj )
{
   _undo_db.on_modify( obj );
}

void object_database::save_undo_add( const object& obj )
{
   _undo_db.on_create( obj );
}

void object_database::save_undo_remove(const object& obj)
{
   _undo_db.on_remove( obj );
}

} } // namespace graphene::db
