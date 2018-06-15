#include <fc/io/sstream.hpp>
#include <api_wrapper.hpp>

#include <iostream>

namespace cybex {


int  api_wrapper::exec(const std::string& line, std::string& result)
{
      int success=1;
      fc::stringstream ss;
      try {
         fc::variants args = fc::json::variants_from_string(line);;
         if( args.size() != 0 )
         {

            const string& method = args[0].get_string();

            auto result = receive_call( 0, method, variants( args.begin()+1,args.end() ) );

            auto itr = _result_formatters.find( method );
            if( itr == _result_formatters.end() )
            {
                ss << fc::json::to_pretty_string( result );
            }
            else
                ss << itr->second( result, args );
            }
      }
      catch ( const fc::exception& e )
      {
         success =0;
         ss <<  fc::json::to_pretty_string(e);
      }

      result = ss.str();
      return success;
}


variant api_wrapper::send_call( api_id_type api_id, string method_name, variants args /* = variants() */ )
{
   FC_ASSERT(false);
}

variant api_wrapper::send_callback( uint64_t callback_id, variants args /* = variants() */ )
{
   FC_ASSERT(false);
}

void api_wrapper::send_notice( uint64_t callback_id, variants args /* = variants() */ )
{
   FC_ASSERT(false);
}


void api_wrapper::format_result( const string& method, std::function<string(variant,const variants&)> formatter)
{
   _result_formatters[method] = formatter;
}



}  // namespace cybex
