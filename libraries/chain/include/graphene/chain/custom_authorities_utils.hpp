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

namespace graphene { namespace chain {

typedef fc::static_variant<asset, account_id_type, extensions_type> generic_member;

template <class T>
class member_fetcher
{
public:
    member_fetcher(const T& object, const std::string& member_name)
    : m_object(object)
    , m_member_name(member_name)
    {}
    
    template<typename Member, class Class, Member (Class::*member)>
    void operator () ( const char* name ) const
    {
        if (name == m_member_name)
        {
            set_value(m_object.*member);
        }
    }
    
    const generic_member& get_member_value()
    {
        return m_value;
    }
    
private:
    template <class Value>
    void set_value(const Value&) const
    {}
    
    //TODO: need to add new definition for every new supported type
    void set_value(const asset& value) const
    {
        m_value = value;
    }
    
private:
    const T& m_object;
    std::string m_member_name;
    mutable generic_member m_value;
};

template <class T>
generic_member get_member(const T& object, const std::string& member_name)
{
    member_fetcher<T> fetcher(object, member_name);
    fc::reflector<T>::visit(fetcher);
    
    return fetcher.get_member_value();
}

class operation_member_fetcher
{
public:
    operation_member_fetcher(const std::string& member_name)
    : m_member_name(member_name)
    {}
    
    const generic_member& get_member_value()
    {
        return m_value;
    }
    
    typedef void result_type;
    
    template <class Operation>
    void operator () ( const Operation& a_operation) const
    {
        m_value = get_member(a_operation, m_member_name);
    }
    
private:
    std::string m_member_name;
    mutable generic_member m_value;
};

generic_member get_operation_member(operation a_operation, const std::string& member_name)
{
    operation_member_fetcher member_fetcher(member_name);
    a_operation.visit(member_fetcher);
    
    return member_fetcher.get_member_value();
}
    
struct equal
{
    template <class T>
    bool operator () (const T& left, const T& right) const
    {
        return left == right;
    }
};

struct not_equal
{
    template <class T>
    bool operator () (const T& left, const T& right) const
    {
        return left != right;
    }
};

template <typename Comparer>
class static_variable_comparer
{
public:
    static_variable_comparer(const generic_member& left)
    : m_left(left)
    {}
    
    typedef void result_type;
    
    template <class T>
    result_type operator () (const T& right)
    {
        Comparer comparer;
        FC_ASSERT(comparer(m_left.get<T>(), right));
    }
    
private:
    generic_member m_left;
};

} } 
