#include <graphene/chain/block_database.hpp>

namespace graphene { namespace chain {

struct index_entry
{
   uint64_t      block_pos = 0;
   uint32_t      block_size = 0;
   block_id_type block_id;
};

void block_database::open( const fc::path& dbdir )
{
  _block_num_to_pos.open( (dbdir/"index").generic_string().c_str(), std::fstream::binary );
  _blocks.open( (dbdir/"blocks").generic_string().c_str(), std::fstream::binary );
}


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


void block_database::store( const block_id_type& id, const signed_block& b )
{
   auto num = block_header::num_from_id(id);
   _block_num_to_pos.seekp( sizeof( index_entry ) * num );
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
{
   index_entry e;
   auto index_pos = sizeof(e)*block_header::num_from_id(id);
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   FC_ASSERT( _block_num_to_pos.tellg() > index_pos );

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   FC_ASSERT( e.block_id == id );

   e.block_size = 0;
   _block_num_to_pos.seekp( sizeof(e)*block_header::num_from_id(id) );
   _block_num_to_pos.write( (char*)&e, sizeof(e) );
}





bool  block_database::contains( const block_id_type& id )const
{
   index_entry e;
   auto index_pos = sizeof(e)*block_header::num_from_id(id);
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   FC_ASSERT( _block_num_to_pos.tellg() > index_pos );

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   return e.block_id == id;
}


block_id_type          block_database::fetch_block_id( uint32_t block_num )const
{
   index_entry e;
   auto index_pos = sizeof(e)*block_num;
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   FC_ASSERT( _block_num_to_pos.tellg() > index_pos );

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   return e.block_id;
}


optional<signed_block> block_database::fetch_optional( const block_id_type& id )const
{
   index_entry e;
   auto index_pos = sizeof(e)*block_header::num_from_id(id);
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   FC_ASSERT( _block_num_to_pos.tellg() > index_pos );

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   if( e.block_id != id ) return optional<signed_block>();

   vector<char> data( e.block_size );
   _blocks.seekg( e.block_pos );
   _blocks.read( data.data(), e.block_size );
   return fc::raw::unpack<signed_block>(data);
}


optional<signed_block> block_database::fetch_by_number( uint32_t block_num )const
{
   index_entry e;
   auto index_pos = sizeof(e)*block_num;
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );
   FC_ASSERT( _block_num_to_pos.tellg() > index_pos );

   _block_num_to_pos.seekg( index_pos );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );

   vector<char> data( e.block_size );
   _blocks.seekg( e.block_pos );
   _blocks.read( data.data(), e.block_size );
   return fc::raw::unpack<signed_block>(data);
}


optional<signed_block> block_database::last()const
{
   index_entry e;
   _block_num_to_pos.seekg( 0, _block_num_to_pos.end );

   if( _block_num_to_pos.tellp() < sizeof(index_entry) )
      return optional<signed_block>();

   _block_num_to_pos.seekg( -sizeof(index_entry), _block_num_to_pos.end );
   _block_num_to_pos.read( (char*)&e, sizeof(e) );
  
   if( e.block_size == 0 ) 
      return optional<signed_block>();

   vector<char> data( e.block_size );
   _blocks.seekg( e.block_pos );
   _blocks.read( data.data(), e.block_size );
   return fc::raw::unpack<signed_block>(data);
}


} }
