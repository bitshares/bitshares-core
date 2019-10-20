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

#include <graphene/protocol/restriction.hpp>

namespace graphene { namespace protocol {

struct adder {
   size_t sum = 0;
   void operator()(const restriction& r) { sum += r.restriction_count(); }
   void operator()(const vector<restriction>& r) { sum += std::for_each(r.begin(), r.end(), adder()).sum; }
};

size_t restriction::restriction_count(const vector<restriction>& restrictions) {
   return std::for_each(restrictions.begin(), restrictions.end(), adder()).sum;
}

size_t restriction::restriction_count() const {
   if (argument.is_type<vector<restriction>>()) {
      const vector<restriction>& rs = argument.get<vector<restriction>>();
      return 1 + std::for_each(rs.begin(), rs.end(), adder()).sum;
   } else if (argument.is_type<vector<vector<restriction>>>()) {
      const vector<vector<restriction>>& rs = argument.get<vector<vector<restriction>>>();
      return 1 + std::for_each(rs.begin(), rs.end(), adder()).sum;
   } else if (argument.is_type<variant_assert_argument_type>()) {
      const variant_assert_argument_type& arg = argument.get<variant_assert_argument_type>();
      return 1 + std::for_each(arg.second.begin(), arg.second.end(), adder()).sum;
   }
   return 1;
}

} } // namespace graphene::protocol
