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
#pragma once

#include <graphene/protocol/restriction.hpp>
#include <graphene/protocol/operations.hpp>

#include <functional>

namespace graphene { namespace protocol {

/// A type describing the result of a restriction predicate
struct predicate_result {
   /// Whether or not the operation complied with the restrictions or not
   bool success = false;

   /// Either the index of a restriction in a list, or a list of indexes of restrictions (for logical OR branches)
   using restriction_index = static_variant<size_t, vector<predicate_result>>;
   /// The path of indexes to the restriction(s) that failed
   vector<restriction_index> failure_path;

   operator bool() const { return success; }
};

/// A restriction predicate is a function accepting an operation and returning a boolean which indicates whether the
/// operation complies with the restrictions or not
using restriction_predicate_function = std::function<predicate_result(const operation&)>;

/**
 * @brief get_restriction_predicate Get a predicate function for the supplied restriction
 * @param rs The restrictions to evaluate operations against
 * @param op_type The tag specifying which operation type the restrictions apply to
 * @return A predicate function which evaluates an operation to determine whether it complies with the restriction
 */
restriction_predicate_function get_restriction_predicate(vector<restriction> rs, operation::tag_type op_type);

} } // namespace graphene::protocol

FC_REFLECT_TYPENAME(graphene::protocol::predicate_result);
