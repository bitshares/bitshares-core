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
#include <graphene/db/exception.hpp>
#include <graphene/db/upgrade_leveldb.hpp>
#include <fc/log/logger.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <boost/regex.hpp>
#include <boost/filesystem/fstream.hpp>

namespace graphene { namespace db {

    upgrade_db_mapper& upgrade_db_mapper::instance()
    {
        static upgrade_db_mapper  mapper;
        return mapper;
    }

    int32_t upgrade_db_mapper::add_type( const std::string& type_name, const upgrade_db_function& function)
    {
        _upgrade_db_function_registry[type_name] = function;
        return 0;
    }


    // this code has no graphene dependencies, and it
    // could be moved to fc, if fc ever adds a leveldb dependency
    void try_upgrade_db( const fc::path& dir, leveldb::DB* dbase, const char* record_type, size_t record_type_size )
    {
      size_t old_record_type_size = 0;
      std::string old_record_type;
      fc::path record_type_filename = dir / "RECORD_TYPE";
      //if no RECORD_TYPE file exists
      if ( !boost::filesystem::exists( record_type_filename ) )
      {
        //must be original type for the database
        old_record_type = record_type;
        int last_char = old_record_type.length() - 1;
        //strip version number from current_record_name and append 0 to set old_record_type (e.g. mytype0)
        while (last_char >= 0 && isdigit(old_record_type[last_char]))
        {
          --last_char;
        }

        //upgradeable record types should always end with version number
        if( 'v' != old_record_type[last_char] )
        {
          //ilog("Database ${db} is not upgradeable",("db",dir.to_native_ansi_path()));
          return;
        }

        ++last_char;
        old_record_type[last_char] = '0';
        old_record_type.resize(last_char+1);
      }
      else //read record type from file
      {
        boost::filesystem::ifstream is(record_type_filename);
        char buffer[120];
        is.getline(buffer,120);
        old_record_type = buffer;
        is >> old_record_type_size;
      }
      if (old_record_type != record_type)
      {
        //check if upgrade function in registry
        auto upgrade_function_itr = upgrade_db_mapper::instance()._upgrade_db_function_registry.find( old_record_type );
        if (upgrade_function_itr != upgrade_db_mapper::instance()._upgrade_db_function_registry.end())
        {
          ilog("Upgrading database ${db} from ${old} to ${new}",("db",dir.preferred_string())
                                                                ("old",old_record_type)
                                                                ("new",record_type));
          //update database's RECORD_TYPE to new record type name
          boost::filesystem::ofstream os(record_type_filename);
          os << record_type << std::endl;
          os << record_type_size;
          //upgrade the database using upgrade function
          upgrade_function_itr->second(dbase);
        }
        else
        {
          elog("In ${db}, record types ${old} and ${new} do not match, but no upgrade function found!",
                   ("db",dir.preferred_string())("old",old_record_type)("new",record_type));
        }
      }
      else if (old_record_type_size == 0) //if record type file never created, create it now
      {
        boost::filesystem::ofstream os(record_type_filename);
          os << record_type << std::endl;
          os << record_type_size;
      }
      else if (old_record_type_size != record_type_size)
      {
        elog("In ${db}, record type matches ${new}, but record sizes do not match!",
                 ("db",dir.preferred_string())("new",record_type));

      }
    }
} } // namespace graphene::db
