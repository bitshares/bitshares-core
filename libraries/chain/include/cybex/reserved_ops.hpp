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

#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

   void assert_reserved();

#define RESERVED_OPERATION(x)                               \
   struct reserved##x##_operation : public base_operation    \
   {                                                        \
      struct fee_parameters_type {                          \
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION;      \
      };                                                    \
                                                            \
      asset                     fee;                        \
                                                            \
      account_id_type   fee_payer()const {                  \
               return account_id_type();                    \
      }                                                     \
      void        validate()const {assert_reserved();}      \
   };                                                       \

RESERVED_OPERATION(47)
RESERVED_OPERATION(48)
RESERVED_OPERATION(49)
RESERVED_OPERATION(50)
RESERVED_OPERATION(51)
RESERVED_OPERATION(52)
RESERVED_OPERATION(53)
RESERVED_OPERATION(54)
RESERVED_OPERATION(55)
RESERVED_OPERATION(56)
RESERVED_OPERATION(57)
RESERVED_OPERATION(58)
RESERVED_OPERATION(59)
RESERVED_OPERATION(60)
RESERVED_OPERATION(61)
RESERVED_OPERATION(62)
RESERVED_OPERATION(63)
RESERVED_OPERATION(64)
RESERVED_OPERATION(65)
RESERVED_OPERATION(66)
RESERVED_OPERATION(67)
RESERVED_OPERATION(68)
RESERVED_OPERATION(69)
RESERVED_OPERATION(70)
RESERVED_OPERATION(71)
RESERVED_OPERATION(72)
RESERVED_OPERATION(73)
RESERVED_OPERATION(74)
RESERVED_OPERATION(75)
RESERVED_OPERATION(76)
RESERVED_OPERATION(77)
RESERVED_OPERATION(78)
RESERVED_OPERATION(79)
RESERVED_OPERATION(80)
RESERVED_OPERATION(81)
RESERVED_OPERATION(82)
RESERVED_OPERATION(83)
RESERVED_OPERATION(84)
RESERVED_OPERATION(85)
RESERVED_OPERATION(86)
RESERVED_OPERATION(87)
RESERVED_OPERATION(88)
RESERVED_OPERATION(89)
RESERVED_OPERATION(90)
RESERVED_OPERATION(91)
RESERVED_OPERATION(92)
RESERVED_OPERATION(93)
RESERVED_OPERATION(94)
RESERVED_OPERATION(95)
RESERVED_OPERATION(96)
RESERVED_OPERATION(97)
RESERVED_OPERATION(98)
RESERVED_OPERATION(99)

} } // namespace graphene::chain


#define fc_reflect(x) \
FC_REFLECT( graphene::chain::reserved##x##_operation::fee_parameters_type,(fee) ) \
FC_REFLECT( graphene::chain::reserved##x##_operation,(fee) )


fc_reflect(47)
fc_reflect(48)
fc_reflect(49)
fc_reflect(50)
fc_reflect(51)
fc_reflect(52)
fc_reflect(53)
fc_reflect(54)
fc_reflect(55)
fc_reflect(56)
fc_reflect(57)
fc_reflect(58)
fc_reflect(59)
fc_reflect(60)
fc_reflect(61)
fc_reflect(62)
fc_reflect(63)
fc_reflect(64)
fc_reflect(65)
fc_reflect(66)
fc_reflect(67)
fc_reflect(68)
fc_reflect(69)
fc_reflect(70)
fc_reflect(71)
fc_reflect(72)
fc_reflect(73)
fc_reflect(74)
fc_reflect(75)
fc_reflect(76)
fc_reflect(77)
fc_reflect(78)
fc_reflect(79)
fc_reflect(80)
fc_reflect(81)
fc_reflect(82)
fc_reflect(83)
fc_reflect(84)
fc_reflect(85)
fc_reflect(86)
fc_reflect(87)
fc_reflect(88)
fc_reflect(89)
fc_reflect(90)
fc_reflect(91)
fc_reflect(92)
fc_reflect(93)
fc_reflect(94)
fc_reflect(95)
fc_reflect(96)
fc_reflect(97)
fc_reflect(98)
fc_reflect(99)

#define  _db_notify_reserved(x)                         \
   void operator()( const reserved##x##_operation& op )  \
   {                                                    \
   }                                                    \


#define db_notify_reserved \
_db_notify_reserved(47) \
_db_notify_reserved(48) \
_db_notify_reserved(49) \
_db_notify_reserved(50) \
_db_notify_reserved(51) \
_db_notify_reserved(52) \
_db_notify_reserved(53) \
_db_notify_reserved(54) \
_db_notify_reserved(55) \
_db_notify_reserved(56) \
_db_notify_reserved(57) \
_db_notify_reserved(58) \
_db_notify_reserved(59) \
_db_notify_reserved(60) \
_db_notify_reserved(61) \
_db_notify_reserved(62) \
_db_notify_reserved(63) \
_db_notify_reserved(64) \
_db_notify_reserved(65) \
_db_notify_reserved(66) \
_db_notify_reserved(67) \
_db_notify_reserved(68) \
_db_notify_reserved(69) \
_db_notify_reserved(70) \
_db_notify_reserved(71) \
_db_notify_reserved(72) \
_db_notify_reserved(73) \
_db_notify_reserved(74) \
_db_notify_reserved(75) \
_db_notify_reserved(76) \
_db_notify_reserved(77) \
_db_notify_reserved(78) \
_db_notify_reserved(79) \
_db_notify_reserved(80) \
_db_notify_reserved(81) \
_db_notify_reserved(82) \
_db_notify_reserved(83) \
_db_notify_reserved(84) \
_db_notify_reserved(85) \
_db_notify_reserved(86) \
_db_notify_reserved(87) \
_db_notify_reserved(88) \
_db_notify_reserved(89) \
_db_notify_reserved(90) \
_db_notify_reserved(91) \
_db_notify_reserved(92) \
_db_notify_reserved(93) \
_db_notify_reserved(94) \
_db_notify_reserved(95) \
_db_notify_reserved(96) \
_db_notify_reserved(97) \
_db_notify_reserved(98) \
_db_notify_reserved(99) \


#define impact_visit_reserved db_notify_reserved 
