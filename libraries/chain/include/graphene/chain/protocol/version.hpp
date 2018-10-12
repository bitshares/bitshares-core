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

#pragma once
#include <fc/string.hpp>
#include <fc/time.hpp>
#include <fc/reflect/reflect.hpp>

namespace graphene { namespace chain {
   /*
   * This class represents an example versioning scheme for the Bitshares Blockchain.
   * Implemented in a way which was discussed in https://github.com/bitshares/bitshares-core/issues/1173
   * The versioning here is Major.Minor.Patch
   * Major == Changes to the protocol
   * Minor == Feature with non protocol related changes
   * Patch == Patch/Hotfix
   */
   struct version 
   {
      version() {}
      version( uint8_t major, uint8_t minor, uint16_t patch, fc::time_point_sec hardfork_time );
      ~version() {}

      bool operator == ( const version &o )const { return v_num == o.v_num; }
      bool operator != ( const version &o )const { return v_num != o.v_num; }
      bool operator <  ( const version &o )const { return v_num <  o.v_num; }
      bool operator <= ( const version &o )const { return v_num <= o.v_num; }
      bool operator >  ( const version &o )const { return v_num >  o.v_num; }
      bool operator >= ( const version &o )const { return v_num >= o.v_num; }

      bool operator == ( const fc::time_point_sec &o)const { return hardfork_time == o; }
      bool operator != ( const fc::time_point_sec &o)const { return hardfork_time != o; }
      bool operator <  ( const fc::time_point_sec &o)const { return hardfork_time <  o; }
      bool operator <= ( const fc::time_point_sec &o)const { return hardfork_time <= o; }
      bool operator >  ( const fc::time_point_sec &o)const { return hardfork_time >  o; }
      bool operator >= ( const fc::time_point_sec &o)const { return hardfork_time >= o; }

      friend bool operator == ( const fc::time_point_sec &o, const version &_this) { return _this == o; }
      friend bool operator != ( const fc::time_point_sec &o, const version &_this) { return _this != o; }
      friend bool operator <  ( const fc::time_point_sec &o, const version &_this) { return _this >  o; }
      friend bool operator <= ( const fc::time_point_sec &o, const version &_this) { return _this >= o; }
      friend bool operator >  ( const fc::time_point_sec &o, const version &_this) { return _this <  o; }
      friend bool operator >= ( const fc::time_point_sec &o, const version &_this) { return _this <= o; }

      /** 
       * in parts of the code the old HARDFORK_*_TIME var is used in a way
       * that time is added on it. To have the same behaviour as before
       * this operator overloads were added
       */
      version operator + ( const uint32_t offset ) {
         return version( major(), minor(), patch(), hardfork_time + offset );
      }
      version operator - ( const uint32_t offset ) {
         return version( major(), minor(), patch(), hardfork_time - offset );
      }
      version& operator += ( const uint32_t offset ) {
         hardfork_time += offset;
         return *this;
      }
      version& operator -= ( const uint32_t offset ) {
         hardfork_time -= offset;
         return *this;
      }

      // same as the above operators
      version operator + ( const fc::microseconds offset ) {
         return version( major(), minor(), patch(), hardfork_time + offset );
      }
      version operator - ( const fc::microseconds offset ) {
         return version( major(), minor(), patch(), hardfork_time - offset );
      }
      version& operator += ( const fc::microseconds offset ) {
         hardfork_time += offset;
         return *this;
      }
      version& operator -= ( const fc::microseconds offset ) {
         hardfork_time -= offset;
         return *this;
      }

      operator fc::string()const;

      uint32_t major() { return (v_num & 0xFF000000) >> 24; }
      uint32_t minor() { return (v_num & 0x00FF0000) >> 16; }
      uint32_t patch() { return (v_num & 0x0000FFFF) >>  0; }
 
      uint32_t v_num     = 0;
      fc::time_point_sec hardfork_time;
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::version, (v_num) (hardfork_time) )