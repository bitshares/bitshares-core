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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/authority.hpp>

namespace graphene { namespace protocol {

using fc::ecc::blind_factor_type;

/**
 * Consensus constants for the confidential-transfer hardforks.
 * Ring size and memo bounds are not chain parameters: changing them is a hardfork.
 * See libraries/ctcrypto/doc/bsip-ct*.md. Hardfork times are placeholders and must not be treated
 * as a mainnet activation.
 */
static constexpr uint32_t GRAPHENE_STEALTH_MEMO_MIN_SIZE = 32;
static constexpr uint32_t GRAPHENE_STEALTH_MEMO_MAX_SIZE = 1024;
static constexpr uint32_t GRAPHENE_RING_SIZE = 16;
static constexpr uint32_t GRAPHENE_RING_MAX_INPUTS = 8;
static constexpr uint32_t GRAPHENE_RING_MAX_OUTPUTS = 16;

/**
 * @defgroup stealth Stealth Transfer
 * @brief Operations related to stealth transfer of value
 *
 * Stealth Transfers enable users to maintain their finanical privacy against even
 * though all transactions are public.  Every account has three balances:
 *
 * 1. Public Balance - everyone can see the balance changes and the parties involved
 * 2. Blinded Balance - everyone can see who is transacting but not the amounts involved
 * 3. Stealth Balance - both the amounts and parties involved are obscured
 *
 * Account owners may set a flag that allows their account to receive(or not) transfers of these kinds
 * Asset issuers can enable or disable the use of each of these types of accounts.
 *
 * Using the "temp account" which has no permissions required, users can transfer a
 * stealth balance to the temp account and then use the temp account to register a new
 * account.  In this way users can use stealth funds to create anonymous accounts with which
 * they can perform other actions that are not compatible with blinded balances (such as market orders)
 *
 * @section referral_program Referral Progam
 *
 * Stealth transfers that do not specify any account id cannot pay referral fees so 100% of the
 * transaction fee is paid to the network.
 *
 * @section transaction_fees Fees
 *
 * Stealth transfers can have an arbitrarylly large size and therefore the transaction fee for
 * stealth transfers is based purley on the data size of the transaction.
 */
///@{

/**
 *  @ingroup stealth
 *  This data is encrypted and stored in the
 *  encrypted memo portion of the blind output.
 */
struct blind_memo
{
   account_id_type     from;
   share_type          amount;
   string              message;
   /** set to the first 4 bytes of the shared secret
    * used to encrypt the memo.  Used to verify that
    * decryption was successful.
    */
   uint32_t            check= 0;
};

/**
 *  @ingroup stealth
 */
struct blind_input
{
   fc::ecc::commitment_type      commitment;
   /** provided to maintain the invariant that all authority
    * required by an operation is explicit in the operation.  Must
    * match blinded_balance_id->owner
    */
   authority                      owner;
};

/**
 *  When sending a stealth tranfer we assume users are unable to scan
 *  the full blockchain; therefore, payments require confirmation data
 *  to be passed out of band.   We assume this out-of-band channel is
 *  not secure and therefore the contents of the confirmation must be
 *  encrypted. 
 */
struct stealth_confirmation
{
   struct memo_data
   {
      optional<public_key_type> from;
      asset                     amount;
      fc::sha256                blinding_factor;
      fc::ecc::commitment_type  commitment;
      uint32_t                  check = 0;
   };

   /**
    *  Packs *this then encodes as base58 encoded string.
    */
   operator string()const;
   /**
    * Unpacks from a base58 string
    */
   stealth_confirmation( const std::string& base58 );
   stealth_confirmation(){}

   public_key_type           one_time_key;
   optional<public_key_type> to;
   vector<char>              encrypted_memo;
};

/**
 *  @class blind_output
 *  @brief Defines data required to create a new blind commitment
 *  @ingroup stealth
 *
 *  The blinded output that must be proven to be greater than 0
 */
struct blind_output
{
   fc::ecc::commitment_type                commitment;
   /** only required if there is more than one blind output */
   range_proof_type                        range_proof;
   authority                               owner;
   optional<stealth_confirmation>          stealth_memo;
};


/**
 *  @class transfer_to_blind_operation
 *  @ingroup stealth
 *  @brief Converts public account balance to a blinded or stealth balance
 */
struct transfer_to_blind_operation : public base_operation
{
   struct fee_params_t {
      uint64_t fee              = 5*GRAPHENE_BLOCKCHAIN_PRECISION; ///< the cost to register the cheapest non-free account
      uint32_t price_per_output = 5*GRAPHENE_BLOCKCHAIN_PRECISION;
   };


   asset                 fee;
   asset                 amount;
   account_id_type       from;
   blind_factor_type     blinding_factor;
   vector<blind_output>  outputs;

   account_id_type fee_payer()const { return from; }
   void            validate()const;
   share_type      calculate_fee(const fee_params_t& )const;
};

/**
 *  @ingroup stealth
 *  @brief Converts blinded/stealth balance to a public account balance
 */
struct transfer_from_blind_operation : public base_operation
{
   struct fee_params_t {
      uint64_t fee              = 5*GRAPHENE_BLOCKCHAIN_PRECISION; ///< the cost to register the cheapest non-free account
   };

   asset                 fee;
   asset                 amount;
   account_id_type       to;
   blind_factor_type     blinding_factor;
   vector<blind_input>   inputs;

   account_id_type fee_payer()const { return GRAPHENE_TEMP_ACCOUNT; }
   void            validate()const;

   void            get_required_authorities( vector<authority>& a )const
   {
      for( const auto& in : inputs )
         a.push_back( in.owner ); 
   }
};

/**
 *  @ingroup stealth
 *  @brief Transfers from blind to blind
 *
 *  There are two ways to transfer value while maintaining privacy:
 *  1. account to account with amount kept secret
 *  2. stealth transfers with amount sender/receiver kept secret
 *
 *  When doing account to account transfers, everyone with access to the
 *  memo key can see the amounts, but they will not have access to the funds.
 *
 *  When using stealth transfers the same key is used for control and reading
 *  the memo.
 *
 *  This operation is more expensive than a normal transfer and has
 *  a fee proportional to the size of the operation.
 *
 *  All assets in a blind transfer must be of the same type: fee.asset_id
 *  The fee_payer is the temp account and can be funded from the blinded values.
 *
 *  Using this operation you can transfer from an account and/or blinded balances
 *  to an account and/or blinded balances.
 *
 *  Stealth Transfers:
 *
 *  Assuming Receiver has key pair R,r and has shared public key R with Sender
 *  Assuming Sender has key pair S,s
 *  Generate one time key pair  O,o  as s.child(nonce) where nonce can be inferred from transaction
 *  Calculate secret V = o*R
 *  blinding_factor = sha256(V)
 *  memo is encrypted via aes of V
 *  owner = R.child(sha256(blinding_factor))
 *
 *  Sender gives Receiver output ID to complete the payment.
 *
 *  This process can also be used to send money to a cold wallet without having to
 *  pre-register any accounts.
 *
 *  Outputs are assigned the same IDs as the inputs until no more input IDs are available,
 *  in which case a the return value will be the *first* ID allocated for an output.  Additional
 *  output IDs are allocated sequentially thereafter.   If there are fewer outputs than inputs
 *  then the input IDs are freed and never used again.
 */
struct blind_transfer_operation : public base_operation
{
   struct fee_params_t {
      uint64_t fee              = 5*GRAPHENE_BLOCKCHAIN_PRECISION; ///< the cost to register the cheapest non-free account
      uint32_t price_per_output = 5*GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   asset                 fee;
   vector<blind_input>   inputs;
   vector<blind_output>  outputs;
    
   /** graphene TEMP account */
   account_id_type fee_payer()const;
   void            validate()const;
   share_type      calculate_fee( const fee_params_t& k )const;

   void            get_required_authorities( vector<authority>& a )const
   {
      for( const auto& in : inputs )
         a.push_back( in.owner ); 
   }
};

/**
 * One member of a ring. The spend key is the single-key owner stored on the
 * blinded balance with this commitment; it is not repeated here.
 */
struct ring_member
{
   fc::ecc::commitment_type commitment;
};

/**
 * A ring spend. `clsag` is a btsct::ClsagSignature. `key_image` must equal the
 * image inside that signature. Members are strictly sorted by commitment and
 * the real spend is not identified.
 */
struct ring_input
{
   vector<commitment_type>   members;
   commitment_type           pseudo_commitment;
   commitment_type           key_image;
   vector<char>              clsag;
};

/**
 * A new confidential output created by a ring operation. The aggregated
 * Bulletproofs+ proof lives on the operation, not on each output.
 */
struct ring_output
{
   fc::ecc::commitment_type       commitment;
   authority                      owner;
   optional<stealth_confirmation> stealth_memo;
};

/**
 * @ingroup stealth
 * Blind-to-blind transfer that hides which input was spent.
 * Commitments are not deleted. The key image is.
 * Enabled at HARDFORK_BSIP_CT3_TIME. After that hardfork the legacy
 * blind_transfer and transfer_from_blind operations can no longer spend.
 */
struct ring_blind_transfer_operation : public base_operation
{
   struct fee_params_t {
      uint64_t fee             = 10 * GRAPHENE_BLOCKCHAIN_PRECISION;
      uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      uint32_t price_per_input = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   asset                  fee;
   vector<ring_input>     inputs;
   vector<ring_output>    outputs;
   /** Aggregated Bulletproofs+ over outputs, in output order. */
   vector<char>           range_proof;

   account_id_type fee_payer()const { return GRAPHENE_TEMP_ACCOUNT; }
   void            validate()const;
   share_type      calculate_fee( const fee_params_t& k )const;
};

/**
 * @ingroup stealth
 * Ring spend that pays a public account. Optional change outputs stay confidential.
 */
struct ring_transfer_from_blind_operation : public base_operation
{
   struct fee_params_t {
      uint64_t fee             = 10 * GRAPHENE_BLOCKCHAIN_PRECISION;
      uint32_t price_per_kbyte = GRAPHENE_BLOCKCHAIN_PRECISION;
      uint32_t price_per_input = 20 * GRAPHENE_BLOCKCHAIN_PRECISION;
   };

   asset                  fee;
   asset                  amount;
   account_id_type        to;
   blind_factor_type      blinding_factor;
   vector<ring_input>     inputs;
   vector<ring_output>    changes;
   /** Present iff changes is non-empty. Proves the change outputs. */
   vector<char>           range_proof;

   account_id_type fee_payer()const { return GRAPHENE_TEMP_ACCOUNT; }
   void            validate()const;
   share_type      calculate_fee( const fee_params_t& k )const;
};

void verify_blind_output_range_proofs( const vector<blind_output>& outputs );

/** BSIP-CT1. `to` must be unset. Memo ciphertext is size-bounded. */
void check_required_stealth_memo( const optional<stealth_confirmation>& memo );

/** BSIP-CT2. One aggregated Bulletproofs+ on the first output; later proofs empty. */
void verify_bulletproof_plus_outputs( const vector<blind_output>& outputs );

/** Same proof, carried once on a ring operation. */
void verify_bulletproof_plus_commitments( const vector<char>& proof,
                                          const vector<commitment_type>& commitments );

/**
 * Domain-separated message signed by every CLSAG in a ring operation.
 * public_amount/public_blind/to are zero/empty for ring_blind_transfer.
 * spend_keys is row-major, one compressed key per ring member, looked up
 * from the blinded-balance owner by the evaluator.
 */
fc::sha256 ring_signature_message( const asset& fee,
                                   const asset& public_amount,
                                   account_id_type to,
                                   const blind_factor_type& public_blind,
                                   const vector<ring_input>& inputs,
                                   const vector<commitment_type>& outputs,
                                   const vector<char>& range_proof );

void verify_ring_signatures( const fc::sha256& message,
                             const vector<ring_input>& inputs,
                             const vector<fc::ecc::public_key_data>& spend_keys );

/** Structural checks shared by both ring operations. Does not touch the database. */
void validate_ring_inputs( const vector<ring_input>& inputs );

///@} endgroup stealth

} } // graphene::protocol

FC_REFLECT( graphene::protocol::stealth_confirmation,
            (one_time_key)(to)(encrypted_memo) )

FC_REFLECT( graphene::protocol::stealth_confirmation::memo_data,
            (from)(amount)(blinding_factor)(commitment)(check) )

FC_REFLECT( graphene::protocol::blind_memo,
            (from)(amount)(message)(check) )
FC_REFLECT( graphene::protocol::blind_input,
            (commitment)(owner) )
FC_REFLECT( graphene::protocol::blind_output,
            (commitment)(range_proof)(owner)(stealth_memo) )
FC_REFLECT( graphene::protocol::transfer_to_blind_operation,
            (fee)(amount)(from)(blinding_factor)(outputs) )
FC_REFLECT( graphene::protocol::transfer_from_blind_operation,
            (fee)(amount)(to)(blinding_factor)(inputs) )
FC_REFLECT( graphene::protocol::blind_transfer_operation,
            (fee)(inputs)(outputs) )
FC_REFLECT( graphene::protocol::transfer_to_blind_operation::fee_params_t, (fee)(price_per_output) )
FC_REFLECT( graphene::protocol::transfer_from_blind_operation::fee_params_t, (fee) )
FC_REFLECT( graphene::protocol::blind_transfer_operation::fee_params_t, (fee)(price_per_output) )
FC_REFLECT( graphene::protocol::ring_member, (commitment) )
FC_REFLECT( graphene::protocol::ring_input, (members)(pseudo_commitment)(key_image)(clsag) )
FC_REFLECT( graphene::protocol::ring_output, (commitment)(owner)(stealth_memo) )
FC_REFLECT( graphene::protocol::ring_blind_transfer_operation, (fee)(inputs)(outputs)(range_proof) )
FC_REFLECT( graphene::protocol::ring_transfer_from_blind_operation,
            (fee)(amount)(to)(blinding_factor)(inputs)(changes)(range_proof) )
FC_REFLECT( graphene::protocol::ring_blind_transfer_operation::fee_params_t,
            (fee)(price_per_kbyte)(price_per_input) )
FC_REFLECT( graphene::protocol::ring_transfer_from_blind_operation::fee_params_t,
            (fee)(price_per_kbyte)(price_per_input) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::transfer_to_blind_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::transfer_from_blind_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::blind_transfer_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::transfer_to_blind_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::transfer_from_blind_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::blind_transfer_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ring_blind_transfer_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ring_transfer_from_blind_operation::fee_params_t )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ring_blind_transfer_operation )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::ring_transfer_from_blind_operation )
