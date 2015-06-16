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
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/tag.hpp>

#include <fc/io/raw.hpp>
#include <fc/io/raw_variant.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/json.hpp>

#include <graphene/net/peer_database.hpp>
//#include <graphene/db/level_pod_map.hpp>



namespace graphene { namespace net {
  namespace detail
  {
    using namespace boost::multi_index;

    struct potential_peer_database_entry
    {
      uint32_t              database_key;
      potential_peer_record peer_record;

      potential_peer_database_entry(uint32_t database_key, const potential_peer_record& peer_record) :
        database_key(database_key),
        peer_record(peer_record)
      {}
      potential_peer_database_entry(const potential_peer_database_entry& other) :
        database_key(other.database_key),
        peer_record(other.peer_record)
      {}

      const fc::time_point_sec& get_last_seen_time() const { return peer_record.last_seen_time; }
      const fc::ip::endpoint&   get_endpoint() const { return peer_record.endpoint; }
    };

    class peer_database_impl
    {
    public:
      struct last_seen_time_index {};
      struct endpoint_index {};
      typedef boost::multi_index_container< potential_peer_database_entry, 
                                              indexed_by< ordered_non_unique< tag<last_seen_time_index>, 
                                                                              const_mem_fun< potential_peer_database_entry, 
                                                                                             const fc::time_point_sec&, 
                                                                                             &potential_peer_database_entry::get_last_seen_time> 
                                                                            >,
                                                          hashed_unique< tag<endpoint_index>, 
                                                                         const_mem_fun< potential_peer_database_entry, 
                                                                                        const fc::ip::endpoint&, 
                                                                                        &potential_peer_database_entry::get_endpoint 
                                                                                      >, 
                                                                         std::hash<fc::ip::endpoint>  
                                                                       > 
                                                        > 
                                          > potential_peer_set;
    //private:
      //typedef graphene::db::level_pod_map<uint32_t, potential_peer_record> potential_peer_leveldb;
      //potential_peer_leveldb    _leveldb;

      potential_peer_set     _potential_peer_set;

    public:
      void open(const fc::path& databaseFilename);
      void close();
      void clear();
      void erase(const fc::ip::endpoint& endpointToErase);
      void update_entry(const potential_peer_record& updatedRecord);
      potential_peer_record lookup_or_create_entry_for_endpoint(const fc::ip::endpoint& endpointToLookup);
      fc::optional<potential_peer_record> lookup_entry_for_endpoint(const fc::ip::endpoint& endpointToLookup);

      peer_database::iterator begin() const;
      peer_database::iterator end() const;
      size_t size() const;
    };

    class peer_database_iterator_impl
    {
    public:
      typedef peer_database_impl::potential_peer_set::index<peer_database_impl::last_seen_time_index>::type::iterator last_seen_time_index_iterator;
      last_seen_time_index_iterator _iterator;
      peer_database_iterator_impl(const last_seen_time_index_iterator& iterator) :
        _iterator(iterator)
      {}
    };
    peer_database_iterator::peer_database_iterator( const peer_database_iterator& c )
    :boost::iterator_facade<peer_database_iterator, const potential_peer_record, boost::forward_traversal_tag>(c){}

    void peer_database_impl::open(const fc::path& databaseFilename)
    {
       /*
      try
      {
        _leveldb.open(databaseFilename);
      }
      catch (const graphene::db::level_pod_map_open_failure&) 
      {
        fc::remove_all(databaseFilename);
        _leveldb.open(databaseFilename);
      }

      _potential_peer_set.clear();

      for (auto iter = _leveldb.begin(); iter.valid(); ++iter)
        _potential_peer_set.insert(potential_peer_database_entry(iter.key(), iter.value()));
#define MAXIMUM_PEERDB_SIZE 1000
      if (_potential_peer_set.size() > MAXIMUM_PEERDB_SIZE)
      {
        // prune database to a reasonable size
        auto iter = _potential_peer_set.begin();
        std::advance(iter, MAXIMUM_PEERDB_SIZE);
        while (iter != _potential_peer_set.end())
        {
          _leveldb.remove(iter->database_key);
          iter = _potential_peer_set.erase(iter);
        }
      }
      */
    }

    void peer_database_impl::close()
    {
      //_leveldb.close();
      _potential_peer_set.clear();
    }

    void peer_database_impl::clear()
    {
       /*
      auto iter = _leveldb.begin();
      while (iter.valid())
      {
        uint32_t key_to_remove = iter.key();
        ++iter;
        try
        {
          _leveldb.remove(key_to_remove);
        }
        catch (fc::exception&)
        {
          // shouldn't happen, and if it does there's not much we can do
        }
      }
      */
      _potential_peer_set.clear();
    }

    void peer_database_impl::erase(const fc::ip::endpoint& endpointToErase)
    {
      auto iter = _potential_peer_set.get<endpoint_index>().find(endpointToErase);
      if (iter != _potential_peer_set.get<endpoint_index>().end())
      {
        //_leveldb.remove(iter->database_key);
        _potential_peer_set.get<endpoint_index>().erase(iter);
      }
    }

    void peer_database_impl::update_entry(const potential_peer_record& updatedRecord)
    {
      auto iter = _potential_peer_set.get<endpoint_index>().find(updatedRecord.endpoint);
      if (iter != _potential_peer_set.get<endpoint_index>().end())
      {
        _potential_peer_set.get<endpoint_index>().modify(iter, [&updatedRecord](potential_peer_database_entry& entry) { entry.peer_record = updatedRecord; });
        //_leveldb.store(iter->database_key, updatedRecord);
      }
      else
      {
        uint32_t last_database_key;
        //_leveldb.last(last_database_key);
        uint32_t new_database_key = last_database_key + 1;
        potential_peer_database_entry new_database_entry(new_database_key, updatedRecord);
        _potential_peer_set.get<endpoint_index>().insert(new_database_entry);
        //_leveldb.store(new_database_key, updatedRecord);
      }
    }

    potential_peer_record peer_database_impl::lookup_or_create_entry_for_endpoint(const fc::ip::endpoint& endpointToLookup)
    {
      auto iter = _potential_peer_set.get<endpoint_index>().find(endpointToLookup);
      if (iter != _potential_peer_set.get<endpoint_index>().end())
        return iter->peer_record;
      return potential_peer_record(endpointToLookup);
    }

    fc::optional<potential_peer_record> peer_database_impl::lookup_entry_for_endpoint(const fc::ip::endpoint& endpointToLookup)
    {
      auto iter = _potential_peer_set.get<endpoint_index>().find(endpointToLookup);
      if (iter != _potential_peer_set.get<endpoint_index>().end())
        return iter->peer_record;
      return fc::optional<potential_peer_record>();
    }

    peer_database::iterator peer_database_impl::begin() const
    {
      return peer_database::iterator(new peer_database_iterator_impl(_potential_peer_set.get<last_seen_time_index>().begin()));
    }

    peer_database::iterator peer_database_impl::end() const
    {
      return peer_database::iterator(new peer_database_iterator_impl(_potential_peer_set.get<last_seen_time_index>().end()));
    }

    size_t peer_database_impl::size() const
    {
      return _potential_peer_set.size();
    }

    peer_database_iterator::peer_database_iterator()
    {
    }

    peer_database_iterator::~peer_database_iterator()
    {
    }

    peer_database_iterator::peer_database_iterator(peer_database_iterator_impl* impl) :
      my(impl)
    {
    }

    void peer_database_iterator::increment()
    {
      ++my->_iterator;
    }

    bool peer_database_iterator::equal(const peer_database_iterator& other) const
    {
      return my->_iterator == other.my->_iterator;
    }

    const potential_peer_record& peer_database_iterator::dereference() const
    {
      return my->_iterator->peer_record;
    }

  } // end namespace detail



  peer_database::peer_database() :
    my(new detail::peer_database_impl)
  {
  }

  peer_database::~peer_database()
  {}

  void peer_database::open(const fc::path& databaseFilename)
  {
    my->open(databaseFilename);
  }

  void peer_database::close()
  {
    my->close();
  }

  void peer_database::clear()
  {
    my->clear();
  }

  void peer_database::erase(const fc::ip::endpoint& endpointToErase)
  {
    my->erase(endpointToErase);
  }

  void peer_database::update_entry(const potential_peer_record& updatedRecord)
  {
    my->update_entry(updatedRecord);
  }

  potential_peer_record peer_database::lookup_or_create_entry_for_endpoint(const fc::ip::endpoint& endpointToLookup)
  {
    return my->lookup_or_create_entry_for_endpoint(endpointToLookup);
  }

  fc::optional<potential_peer_record> peer_database::lookup_entry_for_endpoint(const fc::ip::endpoint& endpoint_to_lookup)
  {
    return my->lookup_entry_for_endpoint(endpoint_to_lookup);
  }

  peer_database::iterator peer_database::begin() const
  {
    return my->begin();
  }

  peer_database::iterator peer_database::end() const
  {
    return my->end();
  }

  size_t peer_database::size() const
  {
    return my->size();
  }
    std::vector<potential_peer_record> peer_database::get_all()const
    {
        std::vector<potential_peer_record> results;
        /*
        auto itr = my->_leveldb.begin();
        while( itr.valid() )
        {
           results.push_back( itr.value() );
           ++itr;
        }
        */
        return results;
    }


} } // end namespace graphene::net
