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

#include <graphene/utilities/file_util.hpp>

#include <fc/exception/exception.hpp>

#include <ios>
#include <istream>

namespace graphene { namespace utilities {

   std::string read_file_contents( const std::string& path )
   {
       std::ifstream input( path );
       FC_ASSERT( !input.fail() && !input.bad(), "Failed to open file '${f}'", ("f",path) );
       input.seekg( 0, std::ios_base::end );
       const auto size = input.tellg();
       input.seekg( 0 );
       std::vector<char> result;
       result.resize( size );
       input.read( result.data(), size );
       FC_ASSERT( size == input.gcount(), "Incomplete file read from '${f}', expected ${s} but got ${c}?!",
                  ("f",path)("s",size)("c",input.gcount()) );
       return std::string( result.begin(), result.end() );
   }

} } // graphene::utilities
