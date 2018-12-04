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

template <typename Comparer>
struct base_restriction
{
    generic_member value;
    std::string argument;
    
    bool validate( const operation& op ) const
    {
        try
        {
            auto member = get_operation_member(op, argument);
            
            static_variable_comparer<Comparer> comparer(value);
            member.visit(comparer);
            
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
};

typedef base_restriction<equal> eq_restriction;
typedef base_restriction<not_equal> neq_restriction;

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

template <typename Action>
struct list_restriction
{
    std::vector<generic_member> values;
    std::string argument;
    
    bool validate( const operation& op ) const
    {
        try
        {
            operation_member_visitor<Action> visitor(argument, Action(values));
            op.visit(visitor);
            return true;
        }
        catch (const fc::exception& e)
        {
            return false;
        }
    }
};

typedef list_restriction<any_of> any_restriction;
typedef list_restriction<none_of> none_restriction;
typedef list_restriction<contains_all> contains_all_restriction;
typedef list_restriction<contains_none> contains_none_restriction;

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

BOOST_AUTO_TEST_CASE( validation_passes_for_neq_restriction_when_assets_are_not_equal )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    neq_restriction restriction;
    restriction.value = asset(6);
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_neq_restriction_when_assets_are_equal )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    neq_restriction restriction;
    restriction.value = asset(5);
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_neq_restriction_when_comparing_different_types )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    neq_restriction restriction;
    restriction.value = account_id_type(1);
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_any_restriction_when_argument_value_is_present_in_the_list_with_single_value)
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    any_restriction restriction;
    restriction.values = {asset(5)};
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_any_restriction_when_argument_value_is_present_in_the_list_with_several_values )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    any_restriction restriction;
    restriction.values = {asset(1), asset(2), asset(5)};
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_any_restriction_when_argument_value_is_not_present_in_the_list_with_several_values )
{
    transfer_operation operation;
    operation.amount = asset(5);
    
    any_restriction restriction;
    restriction.values = {asset(1), asset(2), asset(3)};
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_none_restriction_when_argument_value_is_not_present_in_the_empty_list)
{
    transfer_operation operation;
    operation.amount = asset(4);
    
    none_restriction restriction;
    restriction.values = {};
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_none_restriction_when_argument_value_is_not_present_in_list)
{
    transfer_operation operation;
    operation.amount = asset(4);
    
    none_restriction restriction;
    restriction.values = {asset(1), asset(2)};
    restriction.argument = "amount";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_none_restriction_when_argument_value_is_present_in_list)
{
    transfer_operation operation;
    operation.amount = asset(2);
    
    none_restriction restriction;
    restriction.values = {asset(1), asset(2), asset(3)};
    restriction.argument = "amount";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_conatins_all_restriction_when_argument_contains_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(1), account_id_type(2), account_id_type(3)};
    
    contains_all_restriction restriction;
    restriction.values = {account_id_type(1), account_id_type(2), account_id_type(3)};
    restriction.argument = "required_auths";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_failes_for_conatins_all_restriction_when_argument_contains_subset_of_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(1), account_id_type(2), account_id_type(3)};
    
    contains_all_restriction restriction;
    restriction.values = {account_id_type(0), account_id_type(1), account_id_type(2), account_id_type(3), account_id_type(4)};
    restriction.argument = "required_auths";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_conatins_all_restriction_when_argument_contains_superset_of_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(0), account_id_type(1), account_id_type(2), account_id_type(3), account_id_type(4)};
    
    contains_all_restriction restriction;
    restriction.values = {account_id_type(1), account_id_type(2), account_id_type(3)};
    restriction.argument = "required_auths";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_passes_for_contains_none_restriction_when_argument_not_contains_any_of_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(0), account_id_type(1), account_id_type(2)};
    
    contains_none_restriction restriction;
    restriction.values = {account_id_type(3), account_id_type(4)};
    restriction.argument = "required_auths";
    
    BOOST_CHECK(restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_contains_none_restriction_when_argument_contained_any_of_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(0), account_id_type(1), account_id_type(2)};
    
    contains_none_restriction restriction;
    restriction.values = {account_id_type(1)};
    restriction.argument = "required_auths";
    
    BOOST_CHECK(!restriction.validate(operation));
}

BOOST_AUTO_TEST_CASE( validation_fails_for_contains_none_restriction_when_argument_contained_several_of_list_values)
{
    assert_operation operation;
    operation.required_auths = {account_id_type(0), account_id_type(1), account_id_type(2)};
    
    contains_none_restriction restriction;
    restriction.values = {account_id_type(1), account_id_type(2)};
    restriction.argument = "required_auths";
    
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

