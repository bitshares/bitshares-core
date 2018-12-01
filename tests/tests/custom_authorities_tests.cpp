/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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

#include <boost/test/unit_test.hpp>

#include <graphene/chain/custom_authority_object.hpp>
#include <graphene/chain/protocol/restriction.hpp>
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/account_object.hpp>

using namespace graphene::chain;

BOOST_AUTO_TEST_SUITE( custom_authority )

BOOST_AUTO_TEST_CASE( validation_for_correct_operation_name_is_passed )
{
    custom_authority_object authority;
    
    authority.operation_name = "graphene::chain::transfer_operation";
    BOOST_CHECK(authority.validate(transfer_operation(), time_point_sec(0)));
    
    authority.operation_name = "graphene::chain::asset_create_operation";
    BOOST_CHECK(authority.validate(asset_create_operation(), time_point_sec(0)));
}

BOOST_AUTO_TEST_CASE( validation_for_wrong_operation_name_is_failed )
{
    custom_authority_object authority;
    
    authority.operation_name = "graphene::chain::asset_create_operation";
    BOOST_CHECK(!authority.validate(transfer_operation(), time_point_sec(0)));
    
    authority.operation_name = "graphene::chain::transfer_operation";
    BOOST_CHECK(!authority.validate(asset_create_operation(), time_point_sec(0)));
}

BOOST_AUTO_TEST_CASE( validation_fails_when_now_is_after_valid_period )
{
    custom_authority_object authority;
    
    authority.operation_name = "graphene::chain::transfer_operation";
    authority.valid_from = time_point_sec(0);
    authority.valid_to = time_point_sec(5);
    BOOST_CHECK(!authority.validate(transfer_operation(), time_point_sec(6)));
}

BOOST_AUTO_TEST_CASE( validation_fails_when_now_is_before_valid_period )
{
    graphene::chain::custom_authority_object authority;
    
    authority.operation_name = "graphene::chain::transfer_operation";
    authority.valid_from = time_point_sec(3);
    authority.valid_to = time_point_sec(5);
    BOOST_CHECK(!authority.validate(transfer_operation(), time_point_sec(1)));
}

BOOST_AUTO_TEST_CASE( validation_passes_when_now_is_in_valid_period )
{
    custom_authority_object authority;
    
    authority.operation_name = "graphene::chain::transfer_operation";
    authority.valid_from = time_point_sec(3);
    authority.valid_to = time_point_sec(5);
    BOOST_CHECK(authority.validate(transfer_operation(), time_point_sec(4)));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( restrictions )

template <class Operation>
struct agrument_comparer
{
    std::string argument;
    
    template<typename Member, class Class, Member (Class::*member)>
    void operator()( const char* name )const
    {
       
    }
};

struct operation_argument_comparer
{
    typedef void result_type;
    
    template <class OperationT>
    void operator () (const OperationT& op)
    {
        
    }
};

struct eq_restriction
{
    fc::static_variant<asset> value;
    std::string argument;
    
    bool validate( const operation& op ) const
    {
        return false;
    }
};

BOOST_AUTO_TEST_CASE( validation_passes_for_eq_restriction_when_assets_are_equal )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    eq_restriction restriction;
    restriction.value = asset(5);
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( custom_authorities_utils )

typedef fc::static_variant<asset, account_id_type, fc::optional<memo_data>, extensions_type> generic_member;

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

BOOST_AUTO_TEST_CASE( get_amount_member_of_transfer_operation )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    BOOST_CHECK(asset(5) == get_member(operation, "amount").get<asset>());
}

BOOST_AUTO_TEST_CASE( get_amount_member_of_generic_operation )
{
    transfer_operation a_transfer_operation;
    a_transfer_operation.amount = asset(5);
    
    operation a_operation = a_transfer_operation;
    
    BOOST_CHECK(asset(5) == get_operation_member(a_operation, "amount").get<asset>());
}

BOOST_AUTO_TEST_SUITE_END()

