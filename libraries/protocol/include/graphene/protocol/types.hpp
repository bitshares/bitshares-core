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

#include <memory>
#include <vector>
#include <deque>
#include <cstdint>

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/cat.hpp>

#include <boost/rational.hpp>

#include <fc/container/flat_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>

#include <fc/io/datastream.hpp>
#include <fc/io/raw_fwd.hpp>
#include <fc/static_variant.hpp>

#include <graphene/protocol/object_id.hpp>
#include <graphene/protocol/config.hpp>

#define GRAPHENE_EXTERNAL_SERIALIZATION(ext, type) \
namespace fc { \
   ext template void from_variant( const variant& v, type& vo, uint32_t max_depth ); \
   ext template void to_variant( const type& v, variant& vo, uint32_t max_depth ); \
namespace raw { \
   ext template void pack< datastream<size_t>, type >( datastream<size_t>& s, const type& tx, uint32_t _max_depth ); \
   ext template void pack< sha256::encoder, type >( sha256::encoder& s, const type& tx, uint32_t _max_depth ); \
   ext template void pack< datastream<char*>, type >( datastream<char*>& s, const type& tx, uint32_t _max_depth ); \
   ext template void unpack< datastream<const char*>, type >( datastream<const char*>& s, type& tx, uint32_t _max_depth ); \
} } // fc::raw
#define GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION(type) GRAPHENE_EXTERNAL_SERIALIZATION(extern, type)
#define GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION(type) GRAPHENE_EXTERNAL_SERIALIZATION(/*not extern*/, type)

#define FC_REFLECT_DERIVED_NO_TYPENAME( TYPE, INHERITS, MEMBERS ) \
namespace fc { \
template<> struct reflector<TYPE> {\
    typedef TYPE type; \
    typedef std::true_type is_defined; \
    enum  member_count_enum {  \
      local_member_count = 0  BOOST_PP_SEQ_FOR_EACH( FC_REFLECT_MEMBER_COUNT, +, MEMBERS ),\
      total_member_count = local_member_count BOOST_PP_SEQ_FOR_EACH( FC_REFLECT_BASE_MEMBER_COUNT, +, INHERITS )\
    }; \
    FC_REFLECT_DERIVED_IMPL_INLINE( TYPE, INHERITS, MEMBERS ) \
}; \
} // fc

#define GRAPHENE_NAME_TO_OBJECT_TYPE(x, prefix, name) BOOST_PP_CAT(prefix, BOOST_PP_CAT(name, _object_type))
#define GRAPHENE_NAME_TO_ID_TYPE(x, y, name) BOOST_PP_CAT(name, _id_type)
#define GRAPHENE_DECLARE_ID(x, space_prefix_seq, name) \
    using BOOST_PP_CAT(name, _id_type) = object_id<BOOST_PP_TUPLE_ELEM(2, 0, space_prefix_seq), \
                            GRAPHENE_NAME_TO_OBJECT_TYPE(x, BOOST_PP_TUPLE_ELEM(2, 1, space_prefix_seq), name)>;
#define GRAPHENE_REFLECT_ID(x, id_namespace, name) FC_REFLECT_TYPENAME(graphene::id_namespace::name)

#define GRAPHENE_DEFINE_IDS(id_namespace, object_space, object_type_prefix, names_seq) \
   namespace graphene { namespace id_namespace { \
   \
   enum BOOST_PP_CAT(object_type_prefix, object_type) { \
      BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_TRANSFORM(GRAPHENE_NAME_TO_OBJECT_TYPE, object_type_prefix, names_seq)) \
   }; \
   \
   BOOST_PP_SEQ_FOR_EACH(GRAPHENE_DECLARE_ID, (object_space, object_type_prefix), names_seq) \
   \
   } } \
   \
   FC_REFLECT_ENUM(graphene::id_namespace::BOOST_PP_CAT(object_type_prefix, object_type), \
                   BOOST_PP_SEQ_TRANSFORM(GRAPHENE_NAME_TO_OBJECT_TYPE, object_type_prefix, names_seq)) \
   BOOST_PP_SEQ_FOR_EACH(GRAPHENE_REFLECT_ID, id_namespace, BOOST_PP_SEQ_TRANSFORM(GRAPHENE_NAME_TO_ID_TYPE, , names_seq))

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

struct fee_schedule;
} }  // graphene::protocol

namespace fc {
void to_variant(const graphene::protocol::public_key_type& var,  fc::variant& vo, uint32_t max_depth = 2);
void from_variant(const fc::variant& var,  graphene::protocol::public_key_type& vo, uint32_t max_depth = 2);


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

} // fc::raw

GRAPHENE_DEFINE_IDS(protocol, protocol_ids, /*protocol objects are not prefixed*/,
                    (null)
                    (base)
                    (account)
                    (asset)
                    (force_settlement)
                    (committee_member)
                    (witness)
                    (limit_order)
                    (call_order)
                    (custom)
                    (proposal)
                    (operation_history)
                    (withdraw_permission)
                    (vesting_balance)
                    (worker)
                    (balance)
                    (htlc))

FC_REFLECT(graphene::protocol::public_key_type, (key_data))
FC_REFLECT(graphene::protocol::public_key_type::binary_key, (data)(check))

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

namespace fc { namespace raw {
   extern template void pack( datastream<size_t>& s, const graphene::protocol::public_key_type& tx,
                              uint32_t _max_depth );
   extern template void pack( datastream<char*>& s, const graphene::protocol::public_key_type& tx,
                              uint32_t _max_depth );
   extern template void unpack( datastream<const char*>& s, graphene::protocol::public_key_type& tx,
                                uint32_t _max_depth );
} } // fc::raw
