/*
 * Copyright (c) 2018 Blockchain B.V.
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

#include <graphene/chain/protocol/version.hpp>

namespace graphene { namespace chain {

   version::version( uint8_t major, uint8_t minor, uint16_t patch, fc::time_point_sec hf_time )
   {
      hardfork_time = hf_time;
      
      v_num = (v_num | major) << 8;
      v_num = (v_num | minor) << 16;
      v_num = (v_num | patch);
   }

   version::operator fc::string()const
   {
      std::stringstream s;
      s << ( v_num & 0xFF000000 )
      << '.'
      << ( v_num & 0x00FF0000 )
      << '.'
      << ( v_num & 0x0000FFFF );
      
      return s.str();
   }

} } // graphene::chain
