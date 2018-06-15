#pragma once
#include <fc/io/stdio.hpp>
#include <fc/io/json.hpp>
#include <fc/io/buffered_iostream.hpp>
#include <fc/io/sstream.hpp>
#include <fc/rpc/api_connection.hpp>
#include <fc/thread/thread.hpp>
#include <fc/variant.hpp>

#include <iostream>
#include <functional>

using namespace fc;

namespace cybex {

   /**
    *  Provides a simple wrapper for RPC calls to a given interface.
    */
   class api_wrapper : public api_connection
   {
      public:
         api_wrapper(uint32_t max_depth  ) : api_connection(max_depth) {}
         ~api_wrapper() {};

         virtual variant send_call( api_id_type api_id, string method_name, variants args = variants() );
         virtual variant send_callback( uint64_t callback_id, variants args = variants() );
         virtual void    send_notice( uint64_t callback_id, variants args = variants() );

         void format_result( const string& method, std::function<string(variant,const variants&)> formatter);

         int exec( const std::string& line, std::string & result);

         std::map<string,std::function<string(variant,const variants&)> > _result_formatters;         
   };
} 
