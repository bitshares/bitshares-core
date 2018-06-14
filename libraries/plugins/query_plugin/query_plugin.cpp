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

#include <fc/io/json.hpp>
#include <fc/io/sstream.hpp>
#include <graphene/chain/database.hpp>

#include <graphene/utilities/key_conversion.hpp>
#include <graphene/app/database_api.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/network/http/server.hpp>
#include <fc/log/logger.hpp>
#include <cybex/parse_url.hpp>
#include <cybex/query_plugin.hpp>

#include <iostream>
#include <fc/smart_ref_impl.hpp>

using std::string;
using std::vector;

namespace bpo = boost::program_options;
using namespace graphene::cybex::query_plugin;

void query_plugin::plugin_set_program_options(
   boost::program_options::options_description& command_line_options,
   boost::program_options::options_description& config_file_options)
{
   command_line_options.add_options()
         ("query-endpoint,Q", bpo::value<string>()->implicit_value("127.0.0.1:80"), "Endpoint for HTTP query to listen on")
         ;

   config_file_options.add(command_line_options);
   ilog("query plugin:  plugin_set_program_options() ");
}

std::string query_plugin::plugin_name()const
{
   return "query";
}

void query_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ 
	try {
   	ilog("query plugin:  plugin_initialize() begin");
   	_options = options;
   	ilog("query plugin:  plugin_initialize() end");
	} FC_LOG_AND_RETHROW() 
}

void query_plugin::plugin_startup()
{
    graphene::cybex::Initialize_url_parser(); 
    try {
        ilog("query plugin:  plugin_startup() begin");
        chain::database& d = database();

        if (_options.count("query-endpoint")!=0)
        {
	    his_api = std::make_shared<graphene::app::history_api>(app());
	    db_api = std::make_shared<graphene::app::database_api>(d,&app().get_options()); 
            //auto _http_server = std::make_shared<fc::http::server>();
            ilog( "Listening for incoming HTTP query requests on ${p}", ("p", _options.at("query-endpoint").as<string>() ) );
            _http_server.listen( fc::ip::endpoint::from_string( _options.at( "query-endpoint" ).as<string>() ) );
            //
            // due to implementation, on_request() must come AFTER listen()
            //
            _http_server.on_request(
            [&,this]( const fc::http::request& req, const fc::http::server::response& resp )
            {
		 fc::http::reply::status_code resp_status;
                 fc::stringstream ss ;

                 try{
		     graphene::cybex::parse_result result;
		     int status = graphene::cybex::parse_url(req.path, result);
                     FC_ASSERT(status==1);

		     switch (result.action){
			case ID_ticker:
			{
			    auto iter = result.params.find("base");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto base = iter->second;
			    iter = result.params.find("quote");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto quote = iter->second;
		            auto data =  db_api->get_ticker(base,quote);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
			case ID_volume:
			{
			    auto iter = result.params.find("base");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto base = iter->second;
			    iter = result.params.find("quote");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto quote = iter->second;
		            auto data =  db_api->get_24_volume(base,quote);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
			case ID_trade:
			{
			    auto iter = result.params.find("base");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto base = iter->second;
			    iter = result.params.find("quote");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto quote = iter->second;
			    iter = result.params.find("start");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto start = fc::time_point::from_iso_string(iter->second);
			    iter = result.params.find("end");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto end = fc::time_point::from_iso_string(iter->second);
			    iter = result.params.find("limit");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto limit = stoi(iter->second);
		            auto data = db_api->get_trade_history(base,quote,start,end,limit);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
        		case ID_market:
			{
			    auto iter = result.params.find("base");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto base = iter->second;
			    iter = result.params.find("quote");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto quote = iter->second;
			    auto assets = db_api->lookup_asset_symbols({base, quote});
			    FC_ASSERT(assets[0], "Invalid asset symbol: ${s}", ("s",base));
			    FC_ASSERT(assets[1], "Invalid asset symbol: ${s}", ("s",quote));
			    auto base_id = assets[0]->id;
			    auto quote_id = assets[1]->id;
			    iter = result.params.find("bucket_seconds");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto bucket_seconds = stoi(iter->second);
			    iter = result.params.find("start");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto start = fc::time_point::from_iso_string(iter->second);
			    iter = result.params.find("end");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto end = fc::time_point::from_iso_string(iter->second);
		            auto data =  his_api->get_market_history(base_id,quote_id,bucket_seconds,start,end);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
        		case ID_order:
			{
			    auto iter = result.params.find("base");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto base = iter->second;
			    iter = result.params.find("quote");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto quote = iter->second;
			    iter = result.params.find("limit");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto limit = stoi(iter->second);
		            auto data =  db_api->get_order_book(base,quote,limit);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
        		case ID_assets:
			{
		            auto data =  db_api->list_assets("",100);
		      	    ss << fc::json::to_pretty_string(data);
			    break;
			}
        		case ID_account:
			{
			    auto iter = result.params.find("name");
			    FC_ASSERT(iter!=result.params.end());
	 	            auto name = iter->second;
                            vector<string> names;
                            names.push_back(name);
		            auto data =  db_api->get_full_accounts(names, false);
			    auto iter1 = data.find(name);
                            FC_ASSERT(iter1!=data.end(),"account not found.");
		      	    ss << fc::json::to_pretty_string(iter1->second);
			    break;
			}
		     }
                     resp_status=fc::http::reply::OK;
		  }
		 catch ( const fc::exception& e )
         	 {
            	     ss <<  e.to_detail_string();
                     resp_status = fc::http::reply::BadRequest;
                 }
		 catch ( const std::exception& e )
         	 {
            	     ss <<  e.what() << "error";
                     resp_status = fc::http::reply::BadRequest;
                 }
    
                 auto s = ss.str();
		 try{
                     resp.set_status(resp_status);
                     resp.set_length(s.length());
          	     resp.write( s.c_str(), s.length() );
		 }
		 catch( const fc::exception& e )
   		 {
      		     wdump((e.to_detail_string()));
  		 }
            } );
        }
    } FC_CAPTURE_AND_RETHROW() 
}

void query_plugin::plugin_shutdown()
{
   // nothing to do
}
