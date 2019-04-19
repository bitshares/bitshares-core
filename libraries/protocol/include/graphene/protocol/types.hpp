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
#pragma once
#include <fc/container/flat_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>

#include <graphene/protocol/ext.hpp>

#include <fc/io/raw.hpp>
#include <fc/uint128.hpp>
#include <fc/static_variant.hpp>

#include <memory>
#include <vector>
#include <deque>
#include <cstdint>
#include <graphene/protocol/address.hpp>
#include <graphene/protocol/object_id.hpp>
#include <graphene/protocol/config.hpp>

#include <boost/rational.hpp>

namespace graphene { namespace protocol {
using namespace graphene::db;

using std::map;
using std::vector;
using std::unordered_map;
using std::string;
using std::deque;
using std::shared_ptr;
using std::weak_ptr;
using std::unique_ptr;
using std::set;
using std::pair;
using std::enable_shared_from_this;
using std::tie;
using std::make_pair;

using fc::variant_object;
using fc::variant;
using fc::enum_type;
using fc::optional;
using fc::unsigned_int;
using fc::time_point_sec;
using fc::time_point;
using fc::safe;
using fc::flat_map;
using fc::flat_set;
using fc::static_variant;
using fc::ecc::range_proof_type;
using fc::ecc::range_proof_info;
using fc::ecc::commitment_type;
struct void_t{};

using private_key_type = fc::ecc::private_key;
using chain_id_type = fc::sha256;
using ratio_type = boost::rational<int32_t>;

enum asset_issuer_permission_flags {
    charge_market_fee    = 0x01, /**< an issuer-specified percentage of all market trades in this asset is paid to the issuer */
    white_list           = 0x02, /**< accounts must be whitelisted in order to hold this asset */
    override_authority   = 0x04, /**< issuer may transfer asset back to himself */
    transfer_restricted  = 0x08, /**< require the issuer to be one party to every transfer */
    disable_force_settle = 0x10, /**< disable force settling */
    global_settle        = 0x20, /**< allow the bitasset issuer to force a global settling -- this may be set in permissions, but not flags */
    disable_confidential = 0x40, /**< allow the asset to be used with confidential transactions */
    witness_fed_asset    = 0x80, /**< allow the asset to be fed by witnesses */
    committee_fed_asset  = 0x100 /**< allow the asset to be fed by the committee */
};
const static uint32_t ASSET_ISSUER_PERMISSION_MASK =
        charge_market_fee
        | white_list
        | override_authority
        | transfer_restricted
        | disable_force_settle
        | global_settle
        | disable_confidential
        | witness_fed_asset
        | committee_fed_asset;
const static uint32_t UIA_ASSET_ISSUER_PERMISSION_MASK =
        charge_market_fee
        | white_list
        | override_authority
        | transfer_restricted
        | disable_confidential;

enum reserved_spaces {
    relative_protocol_ids = 0,
    protocol_ids          = 1,
    implementation_ids    = 2
};

inline bool is_relative(object_id_type o) { return o.space() == 0; }

/**
 *  List all object types from all namespaces here so they can
 *  be easily reflected and displayed in debug output.  If a 3rd party
 *  wants to extend the core code then they will have to change the
 *  packed_object::type field from enum_type to uint16 to avoid
 *  warnings when converting packed_objects to/from json.
 */
enum object_type {
    null_object_type,
    base_object_type,
    account_object_type,
    asset_object_type,
    force_settlement_object_type,
    committee_member_object_type,
    witness_object_type,
    limit_order_object_type,
    call_order_object_type,
    custom_object_type,
    proposal_object_type,
    operation_history_object_type,
    withdraw_permission_object_type,
    vesting_balance_object_type,
    worker_object_type,
    balance_object_type,
    htlc_object_type,
    OBJECT_TYPE_COUNT ///< Sentry value which contains the number of different object types
};

using account_id_type = object_id<protocol_ids, account_object_type>;
using asset_id_type = object_id<protocol_ids, asset_object_type>;
using force_settlement_id_type = object_id<protocol_ids, force_settlement_object_type>;
using committee_member_id_type = object_id<protocol_ids, committee_member_object_type>;
using witness_id_type = object_id<protocol_ids, witness_object_type>;
using limit_order_id_type = object_id<protocol_ids, limit_order_object_type>;
using call_order_id_type = object_id<protocol_ids, call_order_object_type>;
using custom_id_type = object_id<protocol_ids, custom_object_type>;
using proposal_id_type = object_id<protocol_ids, proposal_object_type>;
using operation_history_id_type = object_id<protocol_ids, operation_history_object_type>;
using withdraw_permission_id_type = object_id<protocol_ids, withdraw_permission_object_type>;
using vesting_balance_id_type = object_id<protocol_ids, vesting_balance_object_type>;
using worker_id_type = object_id<protocol_ids, worker_object_type>;
using balance_id_type = object_id<protocol_ids, balance_object_type>;
using htlc_id_type = object_id<protocol_ids, htlc_object_type>;

using block_id_type = fc::ripemd160;
using checksum_type = fc::ripemd160;
using transaction_id_type = fc::ripemd160;
using digest_type = fc::sha256;
using signature_type = fc::ecc::compact_signature;
using share_type = safe<int64_t>;
using weight_type = uint16_t;

struct public_key_type {
    struct binary_key {
        binary_key() = default;
        uint32_t check = 0;
        fc::ecc::public_key_data data;
    };
    fc::ecc::public_key_data key_data;
    public_key_type();
    public_key_type(const fc::ecc::public_key_data& data);
    public_key_type(const fc::ecc::public_key& pubkey);
    explicit public_key_type(const std::string& base58str);
    operator fc::ecc::public_key_data() const;
    operator fc::ecc::public_key() const;
    explicit operator std::string() const;
    friend bool operator == (const public_key_type& p1, const fc::ecc::public_key& p2);
    friend bool operator == (const public_key_type& p1, const public_key_type& p2);
    friend bool operator != (const public_key_type& p1, const public_key_type& p2);
};

class pubkey_comparator {
public:
    inline bool operator()(const public_key_type& a, const public_key_type& b) const {
        return a.key_data < b.key_data;
    }
};

struct extended_public_key_type {
    struct binary_key {
        binary_key() = default;
        uint32_t check = 0;
        fc::ecc::extended_key_data data;
    };

    fc::ecc::extended_key_data key_data;

    extended_public_key_type();
    extended_public_key_type(const fc::ecc::extended_key_data& data);
    extended_public_key_type(const fc::ecc::extended_public_key& extpubkey);
    explicit extended_public_key_type(const std::string& base58str);
    operator fc::ecc::extended_public_key() const;
    explicit operator std::string() const;
    friend bool operator == (const extended_public_key_type& p1, const fc::ecc::extended_public_key& p2);
    friend bool operator == (const extended_public_key_type& p1, const extended_public_key_type& p2);
    friend bool operator != (const extended_public_key_type& p1, const extended_public_key_type& p2);
};

struct extended_private_key_type {
    struct binary_key {
        binary_key() = default;
        uint32_t check = 0;
        fc::ecc::extended_key_data data;
    };

    fc::ecc::extended_key_data key_data;

    extended_private_key_type();
    extended_private_key_type(const fc::ecc::extended_key_data& data);
    extended_private_key_type(const fc::ecc::extended_private_key& extprivkey);
    explicit extended_private_key_type(const std::string& base58str);
    operator fc::ecc::extended_private_key() const;
    explicit operator std::string() const;
    friend bool operator == (const extended_private_key_type& p1, const fc::ecc::extended_private_key& p);
    friend bool operator == (const extended_private_key_type& p1, const extended_private_key_type& p);
    friend bool operator != (const extended_private_key_type& p1, const extended_private_key_type& p);
};

struct fee_schedule;
} }  // graphene::protocol

namespace fc {
void to_variant(const graphene::protocol::public_key_type& var,  fc::variant& vo, uint32_t max_depth = 2);
void from_variant(const fc::variant& var,  graphene::protocol::public_key_type& vo, uint32_t max_depth = 2);
void to_variant(const graphene::protocol::extended_public_key_type& var, fc::variant& vo, uint32_t max_depth = 2);
void from_variant(const fc::variant& var, graphene::protocol::extended_public_key_type& vo, uint32_t max_depth = 2);
void to_variant(const graphene::protocol::extended_private_key_type& var, fc::variant& vo, uint32_t max_depth = 2);
void from_variant(const fc::variant& var, graphene::protocol::extended_private_key_type& vo, uint32_t max_depth = 2);


template<>
struct get_typename<std::shared_ptr<const graphene::protocol::fee_schedule>> { static const char* name() {
    return "shared_ptr<const fee_schedule>";
} };
template<>
struct get_typename<std::shared_ptr<graphene::protocol::fee_schedule>> { static const char* name() {
    return "shared_ptr<fee_schedule>";
} };
void from_variant( const fc::variant& var, std::shared_ptr<const graphene::protocol::fee_schedule>& vo,
                   uint32_t max_depth = 2 );
}

FC_REFLECT_ENUM(graphene::protocol::object_type,
                (null_object_type)
                (base_object_type)
                (account_object_type)
                (force_settlement_object_type)
                (asset_object_type)
                (committee_member_object_type)
                (witness_object_type)
                (limit_order_object_type)
                (call_order_object_type)
                (custom_object_type)
                (proposal_object_type)
                (operation_history_object_type)
                (withdraw_permission_object_type)
                (vesting_balance_object_type)
                (worker_object_type)
                (balance_object_type)
                (htlc_object_type)
                (OBJECT_TYPE_COUNT))

FC_REFLECT_TYPENAME(graphene::protocol::account_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::asset_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::force_settlement_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::committee_member_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::witness_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::limit_order_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::call_order_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::custom_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::proposal_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::operation_history_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::withdraw_permission_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::vesting_balance_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::worker_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::balance_id_type)
FC_REFLECT_TYPENAME(graphene::protocol::htlc_id_type)

FC_REFLECT(graphene::protocol::public_key_type, (key_data))
FC_REFLECT(graphene::protocol::public_key_type::binary_key, (data)(check))
FC_REFLECT(graphene::protocol::extended_public_key_type, (key_data))
FC_REFLECT(graphene::protocol::extended_public_key_type::binary_key, (check)(data))
FC_REFLECT(graphene::protocol::extended_private_key_type, (key_data))
FC_REFLECT(graphene::protocol::extended_private_key_type::binary_key, (check)(data))

FC_REFLECT_TYPENAME(graphene::protocol::share_type)
FC_REFLECT(graphene::protocol::void_t,)

FC_REFLECT_ENUM(graphene::protocol::asset_issuer_permission_flags,
                (charge_market_fee)
                (white_list)
                (transfer_restricted)
                (override_authority)
                (disable_force_settle)
                (global_settle)
                (disable_confidential)
                (witness_fed_asset)
                (committee_fed_asset))
