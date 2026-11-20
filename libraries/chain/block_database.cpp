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
#include <graphene/chain/block_database.hpp>
#include <graphene/protocol/fee_schedule.hpp>
#include <fc/io/raw.hpp>
#include <boost/endian/buffers.hpp>

namespace graphene { namespace chain {

namespace {

/**
 * Post-quantum: decode a stored block, trying both serialization formats and letting the
 * recorded block id decide which one was right.
 *
 * store() packs a block under whichever format was in effect on the writing thread, which
 * follows chain state at the moment the block was applied. Reads happen on entirely
 * different threads -- API calls, p2p item requests, startup index checks -- whose format
 * is whatever their own context happened to set, and is normally the legacy one. So from
 * the PQ hardfork onward a reader decodes a post-quantum-format block using the legacy
 * format; the unpack, or the id comparison after it, then fails.
 *
 * Every caller in this file swallows that exception, so the failure is silent and the
 * consequences are severe: get_block() returns null for every block after activation, the
 * node cannot serve those blocks to peers (so nobody can sync past the hardfork), and the
 * index check on startup treats them as corruption and truncates the index file, throwing
 * away every block after the activation point.
 *
 * Trying both formats needs no change to the on-disk layout and cannot accept a wrong
 * decode, because a block decoded under the wrong format does not hash to the id stored
 * alongside it.
 */
fc::optional<signed_block> unpack_block_checked( const std::vector<char>& data,
                                                 const block_id_type& expected_id )
{
   const fc::raw::pq_format current_fmt = fc::raw::get_pq_format();
   const fc::raw::pq_format other_fmt = ( current_fmt == fc::raw::pq_format::current )
                                           ? fc::raw::pq_format::legacy
                                           : fc::raw::pq_format::current;
   for( const auto fmt : { current_fmt, other_fmt } )
   {
      try
      {
         fc::raw::scoped_pq_format scoped( fmt );
         auto result = fc::raw::unpack<signed_block>( data );
         if( result.id() == expected_id )
            return result;
      }
      catch (const fc::exception&)
      {
      }
      catch (const std::exception&)
      {
      }
   }
   return {};
}

} // anonymous namespace

struct index_entry
{
   index_entry() {
      block_pos = 0;
      block_size = 0;
   };
   boost::endian::little_uint64_buf_t block_pos;
   boost::endian::little_uint32_buf_t block_size;
   block_id_type                      block_id;
};
 }}
FC_REFLECT( graphene::chain::index_entry, (block_pos)(block_size)(block_id) );

namespace graphene { namespace chain {

void block_database::open( const fc::path& dbdir )
{ try {
   fc::create_directories(dbdir);
   _block_num_to_pos.exceptions(std::ios_base::failbit | std::ios_base::badbit);
   _blocks.exceptions(std::ios_base::failbit | std::ios_base::badbit);

   _index_filename = dbdir / "index";
   if( !fc::exists( _index_filename ) )
   {
     _block_num_to_pos.open( _index_filename.generic_string().c_str(), std::fstream::binary | std::fstream::in | std::fstream::out | std::fstream::trunc);
     _blocks.open( (dbdir/"blocks").generic_string().c_str(), std::fstream::binary | std::fstream::in | std::fstream::out | std::fstream::trunc);
   }
   else
   {
     _block_num_to_pos.open( _index_filename.generic_string().c_str(), std::fstream::binary | std::fstream::in | std::fstream::out );
     _blocks.open( (dbdir/"blocks").generic_string().c_str(), std::fstream::binary | std::fstream::in | std::fstream::out );
   }
} FC_CAPTURE_AND_RETHROW( (dbdir) ) }

bool block_database::is_open()const
{
  return _blocks.is_open();
}

void block_database::close()
{
  _blocks.close();
  _block_num_to_pos.close();
}

void block_database::flush()
{
  _blocks.flush();
  _block_num_to_pos.flush();
}

void block_database::store( const block_id_type& _id, const signed_block& b )
{
   block_id_type id = _id;
   if( id == block_id_type() )
   {
      id = b.id();
      elog( "id argument of block_database::store() was not initialized for block ${id}", ("id", id) );
   }
   _block_num_to_pos.seekp( sizeof( index_entry ) * int64_t(block_header::num_from_id(id)) );
   index_entry e;
   _blocks.seekp( 0, _blocks.end );
   auto vec = fc::raw::pack( b );
   e.block_pos  = _blocks.tellp();
   e.block_size = vec.size();
   e.block_id   = id;
   _blocks.write( vec.data(), vec.size() );
   _block_num_to_pos.write( (char*)&e, sizeof(e) );
}

void block_database::remove( const block_id_type& id )
{ try {
   index_entry e;
   int64_t index_pos = sizeof(e) * int64_t(block_header::num_from_id(id));
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   if ( _block_num_to_pos.tellg() <= index_pos )
      FC_THROW_EXCEPTION(fc::key_not_found_exception, "Block ${id} not contained in block database", ("id", id));

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   if( e.block_id == id )
   {
      e.block_size = 0;
      _block_num_to_pos.seekp( sizeof(e) * int64_t(block_header::num_from_id(id)) );
      _block_num_to_pos.write( (char*)&e, sizeof(e) );
   }
} FC_CAPTURE_AND_RETHROW( (id) ) }

bool block_database::contains( const block_id_type& id )const
{
   if( id == block_id_type() )
      return false;

   index_entry e;
   int64_t index_pos = sizeof(e) * int64_t(block_header::num_from_id(id));
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   if ( _block_num_to_pos.tellg() < int64_t(index_pos + sizeof(e)) )
      return false;
   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   return e.block_id == id && e.block_size.value() > 0;
}

block_id_type block_database::fetch_block_id( uint32_t block_num )const
{
   assert( block_num != 0 );
   index_entry e;
   int64_t index_pos = sizeof(e) * int64_t(block_num);
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   if ( _block_num_to_pos.tellg() <= index_pos )
      FC_THROW_EXCEPTION(fc::key_not_found_exception, "Block number ${block_num} not contained in block database", ("block_num", block_num));

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   FC_ASSERT( e.block_id != block_id_type(), "Empty block_id in block_database (maybe corrupt on disk?)" );
   return e.block_id;
}

optional<signed_block> block_database::fetch_optional( const block_id_type& id )const
{
   try
   {
      index_entry e;
      int64_t index_pos = sizeof(e) * int64_t(block_header::num_from_id(id));
      _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
      if ( _block_num_to_pos.tellg() <= index_pos )
         return {};

      _block_num_to_pos.seekg( index_pos );
      _block_num_to_pos.read( (char*)&e, sizeof(e) );

      if( e.block_id != id ) return optional<signed_block>();

      vector<char> data( e.block_size.value() );
      _blocks.seekg( e.block_pos.value() );
      if (e.block_size.value())
         _blocks.read( data.data(), e.block_size.value() );
      auto result = unpack_block_checked( data, e.block_id );
      FC_ASSERT( result.valid() );
      return result;
   }
   catch (const fc::exception&)
   {
   }
   catch (const std::exception&)
   {
   }
   return optional<signed_block>();
}

optional<signed_block> block_database::fetch_by_number( uint32_t block_num )const
{
   try
   {
      index_entry e;
      int64_t index_pos = sizeof(e) * int64_t(block_num);
      _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
      if ( _block_num_to_pos.tellg() <= index_pos )
         return {};

      _block_num_to_pos.seekg( index_pos, _block_num_to_pos.beg );
      _block_num_to_pos.read( (char*)&e, sizeof(e) );

      vector<char> data( e.block_size.value() );
      _blocks.seekg( e.block_pos.value() );
      _blocks.read( data.data(), e.block_size.value() );
      auto result = unpack_block_checked( data, e.block_id );
      FC_ASSERT( result.valid() );
      return result;
   }
   catch (const fc::exception&)
   {
   }
   catch (const std::exception&)
   {
   }
   return optional<signed_block>();
}

optional<index_entry> block_database::last_index_entry()const {
   try
   {
      index_entry e;

      _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
      std::streampos pos = _block_num_to_pos.tellg();
      if( pos < long(sizeof(index_entry)) )
         return optional<index_entry>();

      pos -= pos % sizeof(index_entry);

      _blocks.seekg( 0, _block_num_to_pos.end );
      const std::streampos blocks_size = _blocks.tellg();
      while( pos > 0 )
      {
         pos -= sizeof(index_entry);
         _block_num_to_pos.seekg( pos );
         _block_num_to_pos.read( (char*)&e, sizeof(e) );
         if( _block_num_to_pos.gcount() == sizeof(e) && e.block_size.value() > 0
                && int64_t(e.block_pos.value() + e.block_size.value()) <= blocks_size )
            try
            {
               vector<char> data( e.block_size.value() );
               _blocks.seekg( e.block_pos.value() );
               _blocks.read( data.data(), e.block_size.value() );
               if( _blocks.gcount() == long(e.block_size.value()) )
               {
                  // unpack_block_checked() only returns a block whose id matches, so a
                  // valid result here is the same guarantee the id comparison used to give
                  // -- without mistaking a format difference for a corrupt index and
                  // truncating away every block written since the hardfork.
                  if( unpack_block_checked( data, e.block_id ).valid() )
                     return e;
               }
            }
            catch (const fc::exception&)
            {
            }
            catch (const std::exception&)
            {
            }
         fc::resize_file( _index_filename, pos );
      }
   }
   catch (const fc::exception&)
   {
   }
   catch (const std::exception&)
   {
   }
   return optional<index_entry>();
}

optional<signed_block> block_database::last()const
{
   optional<index_entry> entry = last_index_entry();
   if( entry.valid() ) return fetch_by_number( block_header::num_from_id(entry->block_id) );
   return optional<signed_block>();
}

optional<block_id_type> block_database::last_id()const
{
   optional<index_entry> entry = last_index_entry();
   if( entry.valid() ) return entry->block_id;
   return optional<block_id_type>();
}

size_t block_database::blocks_current_position()const
{
   return (size_t)_blocks.tellg();
}

size_t block_database::total_block_size()const
{
   _blocks.seekg( 0, _blocks.end );
   return (size_t)_blocks.tellg();
}

} }
