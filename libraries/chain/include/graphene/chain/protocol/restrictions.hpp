/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/custom_authorities_utils.hpp>

namespace graphene { namespace chain {
   
template <typename Action>
struct base_restriction
{
   generic_member value;
   std::string argument;
   
   template <typename Operation>
   bool validate( const Operation& op ) const
   {
      try
      {
         member_visitor<Operation, Action> visitor(argument, Action(value), op);
         fc::reflector<Operation>::visit(visitor);
         return true;
      }
      catch (const fc::exception&)
      {
         return false;
      }
   }
};

template <typename Action>
struct base_list_restriction
{
   std::vector<generic_member> values;
   std::string argument;
   
   template <typename Operation>
   bool validate( const Operation& op ) const
   {
      try
      {
         member_visitor<Operation, Action> visitor(argument, Action(values), op);
         fc::reflector<Operation>::visit(visitor);
         return true;
      }
      catch (const fc::exception&)
      {
         return false;
      }
   }
};

class equal
{
public:
   equal(const generic_member& value)
   : m_value(value)
   {}
   
   template <class T>
   void operator () (const T& member) const
   {
      FC_ASSERT(is_equal(get<T>(m_value), member));
   }
   
private:
   generic_member m_value;
};

class not_equal
{
public:
   not_equal(const generic_member& value)
   : m_value(value)
   {}
   
   template <class T>
   void operator () (const T& member) const
   {
      FC_ASSERT(!is_equal(get<T>(m_value), member));
   }
   
private:
   generic_member m_value;
};

class any_of
{
public:
   any_of(const std::vector<generic_member>& values)
   : m_values(values)
   {}
   
   template <class T>
   void operator () (const T& member) const
   {
      for (const generic_member& value: m_values)
      {
         if (is_equal(get<T>(value), member))
         {
            return;
         }
      }
      
      FC_THROW("Argument was not present in the list.");
   }
   
private:
   std::vector<generic_member> m_values;
};

class none_of
{
public:
   none_of(const std::vector<generic_member>& values)
   : m_values(values)
   {}
   
   template <class T>
   void operator () (const T& member) const
   {
      for (const generic_member& value: m_values)
      {
         if (is_equal(get<T>(value), member))
         {
            FC_THROW("Operation member is present in the list.");
         }
      }
   }
   
private:
   std::vector<generic_member> m_values;
};

class contains_all
{
public:
   contains_all(const std::vector<generic_member>& values)
   : m_values(values)
   {}
   
   template <class T>
   void operator () (const T&) const
   {
      FC_ASSERT("Not list type come.");
   }
   
   template <class T>
   void operator () (const flat_set<T>& list) const
   {
      for (const generic_member& value: m_values)
      {
         bool contains = false;
         for (const auto& item: list)
         {
            contains |= (item == value.get<T>());
         }
         
         FC_ASSERT(contains);
      }
   }
   
private:
   std::vector<generic_member> m_values;
};

class contains_none
{
public:
   contains_none(const std::vector<generic_member>& values)
   : m_values(values)
   {}
   
   template <class T>
   void operator () (const T&) const
   {
      FC_ASSERT("Not list type come.");
   }
   
   template <class T>
   void operator () (const flat_set<T>& list) const
   {
      for (const generic_member& value: m_values)
      {
         for (const auto& item: list)
         {
            if (is_equal(item, get<T>(value)))
            {
               FC_THROW("Should not contain any of same items.");
            }
         }
      }
   }
   
private:
   std::vector<generic_member> m_values;
};

typedef base_restriction<equal>              eq_restriction;
typedef base_restriction<not_equal>          neq_restriction;
typedef base_list_restriction<any_of>        any_restriction;
typedef base_list_restriction<none_of>       none_restriction;
typedef base_list_restriction<contains_all>  contains_all_restriction;
typedef base_list_restriction<contains_none> contains_none_restriction;
    
typedef fc::static_variant<eq_restriction, neq_restriction, any_restriction, none_restriction, contains_all_restriction, contains_none_restriction> restriction_v2;

} }

FC_REFLECT( graphene::chain::eq_restriction,
           (value)
           (argument)
           )

FC_REFLECT( graphene::chain::neq_restriction,
           (value)
           (argument)
           )

FC_REFLECT( graphene::chain::any_restriction,
           (values)
           (argument)
           )

FC_REFLECT( graphene::chain::none_restriction,
           (values)
           (argument)
           )

FC_REFLECT( graphene::chain::contains_all_restriction,
           (values)
           (argument)
           )

FC_REFLECT( graphene::chain::contains_none_restriction,
           (values)
           (argument)
           )
