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

typedef fc::static_variant<asset, account_id_type, extensions_type, future_extensions, public_key_type, time_point_sec, bool > generic_member;

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
        return !(left == right);
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

template <typename Comparer>
class static_variable_in_list_checker
{
public:
    static_variable_in_list_checker(const std::vector<generic_member>& data_list)
    : m_data_list(data_list)
    {}
    
    bool was_found() const
    {
        return m_was_found;
    }
    
    typedef void result_type;
    
    template <class T>
    result_type operator () (const T& right)
    {
        Comparer comparer;
        
        bool was_found = false;
        for (generic_member& value : m_data_list)
        {
            was_found |= comparer(value.get<T>(), right);
        }
        
        m_was_found = was_found;
    }
    
private:
    std::vector<generic_member> m_data_list;
    bool m_was_found = false;
};
    
template <typename T, typename Action>
struct member_visitor
{
    member_visitor(const std::string& member_name,  const Action& action, const T& object)
    : m_member_name(member_name)
    , m_action(action)
    , m_object(object)
    {}
    
    typedef void result_type;
    
    template<typename Member, class Class, Member (Class::*member)>
    void operator () ( const char* name ) const
    {
        if (name == m_member_name)
        {
            m_action(m_object.*member);
        }
    }
    
private:
    const std::string m_member_name;
    Action m_action;
    T m_object;
};

template <typename Action>
class operation_member_visitor
{
public:
    operation_member_visitor(const std::string& member_name,  const Action& action)
    : m_member_name(member_name)
    , m_action(action)
    {}
    
    typedef void result_type;
    
    template <typename Operation>
    void operator () (const Operation& op) const
    {
        member_visitor<Operation, Action> vistor(m_member_name, m_action, op);
        fc::reflector<Operation>::visit(vistor);
    }
    
private:
    const std::string m_member_name;
    Action m_action;
};

template <class T>
bool is_equal(const T& left, const T& right)
{
    FC_ASSERT(false);
}
    
bool is_equal(const asset& left, const asset& right)
{
    return left == right;
}

template <typename T>
const T& get(const generic_member& variant)
{
    FC_ASSERT(false);
}

template <>
const asset& get<asset>(const generic_member& variant)
{
    return variant.get<asset>();
}
    
} } 
