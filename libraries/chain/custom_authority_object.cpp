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

#include <graphene/chain/custom_authority_object.hpp>
#include <fc/reflect/reflect.hpp>

using namespace graphene::chain;

namespace  {
    struct type_name_visitor
    {
        typedef void result_type;
        
        template <class T>
        void operator () (const T&)
        {
            type_name = fc::get_typename<T>::name();
        }
        
        std::string type_name;
    };
    
    std::string get_operation_name(const operation& an_operation)
    {
        type_name_visitor type_name_retriver;
        an_operation.visit(type_name_retriver);
        
        return type_name_retriver.type_name;
    }
}

bool custom_authority_object::validate(const operation& an_operation) const
{
    return get_operation_name(an_operation) == operation_name;
}
