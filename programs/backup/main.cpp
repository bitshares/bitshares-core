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
#include <iostream>
#include <fc/io/json.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/protocol/block.hpp>
#include <graphene/chain/block_database.hpp>

#include <boost/program_options.hpp>


using namespace graphene::chain;
using namespace std;
namespace bpo = boost::program_options;

int main( int argc, char** argv )
{
   

   block_database src,dst;

   string input,output;   
   int i,j,start,last;

   std::cout << "backup bitshares block database." <<std::endl;
   try {

      boost::program_options::options_description opts;
         opts.add_options()
         ("start,s",bpo::value<int>(),"start")
         ("input",bpo::value(&input),"input")
         ("output",bpo::value(&output),"output")
         ("last",bpo::value(&last),"last")
         ("help,h", "Print this help message and exit.");

      boost::program_options::positional_options_description pos_opts; 
      pos_opts.add( "input", 1); 
      pos_opts.add( "output", 1); 
      pos_opts.add( "last", 1); 

      bpo::variables_map options;

      bpo::store( bpo::command_line_parser(argc, argv).options(opts).positional(pos_opts).run(), options );
      bpo::notify(options);

      if( options.count("help") ) 
      {
         std::cout << opts << "\n";
         return 0;
      }
      if( options.count("start") )
      {
         start=options.at("start").as<int>();
         if( start <1) start=1;
      }
      else start=1;

      
      src.open(input);
      dst.open(output);
    
      j=0;
      for(i=start;i<=last;i++)
      {
                optional<block_id_type> id=src.fetch_block_id(i);
                optional<signed_block>  blk= src.fetch_by_number(i);
                FC_ASSERT( id.valid(),"block database corrupted.block:${n}",("n",i) );
                FC_ASSERT( blk.valid(),"block database corrupted.block:${n}",("n",i) );
 
                dst.store(*id,*blk);
                if((i%10000)==0)
                {
                     std::cout << i << "th block."<< std::endl;
                }
                j=i;
      }

   }
   catch ( const fc::exception& e )
   { 
       std::cout << e.to_detail_string(); 
   }
   src.close();
   dst.close();
   if(j)
   {
        std::cout << std::endl << "last block:" << j << std::endl;
   }

   return 0;
}
