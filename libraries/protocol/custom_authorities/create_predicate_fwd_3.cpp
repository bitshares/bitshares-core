/*
 * Copyright (c) 2019 Contributors.
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

#include "restriction_predicate.hxx"

/* This file contains explicit specializations of the create_predicate_function template.
 * Generate using the following shell code, and paste below.

FWD_FIELD_TYPES="bool uint8_t uint16_t uint32_t unsigned_int extensions_type"

for T in $FWD_FIELD_TYPES; do
    echo "template"
    echo "object_restriction_predicate<$T> create_predicate_function( "
    echo "    restriction_function func, restriction_argument arg );"
done
 */

namespace graphene { namespace protocol {

template
object_restriction_predicate<bool> create_predicate_function(
    restriction_function func, restriction_argument arg );
template
object_restriction_predicate<uint8_t> create_predicate_function(
    restriction_function func, restriction_argument arg );
template
object_restriction_predicate<uint16_t> create_predicate_function(
    restriction_function func, restriction_argument arg );
template
object_restriction_predicate<uint32_t> create_predicate_function(
    restriction_function func, restriction_argument arg );
template
object_restriction_predicate<unsigned_int> create_predicate_function(
    restriction_function func, restriction_argument arg );
template
object_restriction_predicate<extensions_type> create_predicate_function(
    restriction_function func, restriction_argument arg );

} } // namespace graphene::protocol
