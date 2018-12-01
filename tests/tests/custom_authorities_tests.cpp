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
#include <graphene/chain/custom_authorities_utils.hpp>

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
        FC_ASSERT(m_left.get<T>() == right);
    }
    
private:
    generic_member m_left;
};

struct eq_restriction
{
    generic_member value;
    std::string argument;
    
    bool validate( const operation& op ) const
    {
        try
        {
            auto member = get_operation_member(op, argument);
            
            static_variable_comparer comparer(value);
            member.visit(comparer);
            
            return true;
        }
        catch (...)
        {
            return false;
        }
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

BOOST_AUTO_TEST_CASE( validation_fails_for_eq_restriction_when_assets_are_not_equal )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    eq_restriction restriction;
    restriction.value = asset(6);
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_eq_restriction_when_comparing_asset_and_account )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    eq_restriction restriction;
    restriction.value = account_id_type(1);
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( custom_authorities_utils )

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

