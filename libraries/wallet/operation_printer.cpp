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
#include "operation_printer.hpp"
#include <graphene/protocol/base.hpp>
#include "wallet_api_impl.hpp"
#include <graphene/utilities/key_conversion.hpp>

namespace graphene { namespace wallet { namespace detail {

class htlc_hash_to_string_visitor
{
public:
   typedef std::string result_type;

   result_type operator()( const fc::ripemd160& hash )const
   {
      return "RIPEMD160 " + hash.str();
   }

   result_type operator()( const fc::sha1& hash )const
   {
      return "SHA1 " + hash.str();
   }

   result_type operator()( const fc::sha256& hash )const
   {
      return "SHA256 " + hash.str();
   }
   result_type operator()( const fc::hash160& hash )const
   {
      return "HASH160 " + hash.str();
   }
};

std::string operation_printer::fee(const graphene::protocol::asset& a)const {
   out << "   (Fee: " << wallet.get_asset(a.asset_id).amount_to_pretty_string(a) << ")";
   return "";
}

std::string operation_printer::operator()(const transfer_from_blind_operation& op)const
{
   auto a = wallet.get_asset( op.fee.asset_id );
   auto receiver = wallet.get_account( op.to );

   out <<  receiver.name
       << " received " << a.amount_to_pretty_string( op.amount ) << " from blinded balance";
   return "";
}
std::string operation_printer::operator()(const transfer_to_blind_operation& op)const
{
   auto fa = wallet.get_asset( op.fee.asset_id );
   auto a = wallet.get_asset( op.amount.asset_id );
   auto sender = wallet.get_account( op.from );

   out <<  sender.name
       << " sent " << a.amount_to_pretty_string( op.amount ) << " to " << op.outputs.size()
       << " blinded balance" << (op.outputs.size()>1?"s":"")
       << " fee: " << fa.amount_to_pretty_string( op.fee );
   return "";
}

string print_memo( const graphene::wallet::detail::wallet_api_impl& wallet, 
      const fc::optional<graphene::protocol::memo_data>& memo, ostream& out)
{
   std::string outstr;
   if( memo )
   {
      if( wallet.is_locked() )
      {
         out << " -- Unlock wallet to see memo.";
      } else {
         try {
            FC_ASSERT( wallet._keys.count(memo->to) || wallet._keys.count(memo->from),
                       "Memo is encrypted to a key ${to} or ${from} not in this wallet.",
                       ("to", memo->to)("from",memo->from) );
            if( wallet._keys.count(memo->to) ) {
               auto my_key = wif_to_key(wallet._keys.at(memo->to));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               outstr = memo->get_message(*my_key, memo->from);
               out << " -- Memo: " << outstr;
            } else {
               auto my_key = wif_to_key(wallet._keys.at(memo->from));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               outstr = memo->get_message(*my_key, memo->to);
               out << " -- Memo: " << outstr;
            }
         } catch (const fc::exception& e) {
            out << " -- could not decrypt memo";
         }
      }
   } 
   return outstr;  
}

string operation_printer::operator()(const transfer_operation& op) const
{
   out << "Transfer " << wallet.get_asset(op.amount.asset_id).amount_to_pretty_string(op.amount)
       << " from " << wallet.get_account(op.from).name << " to " << wallet.get_account(op.to).name;
   std::string memo = print_memo( wallet, op.memo, out );
   fee(op.fee);
   return memo;
}

std::string operation_printer::operator()(const account_create_operation& op) const
{
   out << "Create Account '" << op.name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const account_update_operation& op) const
{
   out << "Update Account '" << wallet.get_account(op.account).name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const asset_create_operation& op) const
{
   out << "Create ";
   if( op.bitasset_opts.valid() )
      out << "BitAsset ";
   else
      out << "User-Issue Asset ";
   out << "'" << op.symbol << "' with issuer " << wallet.get_account(op.issuer).name;
   return fee(op.fee);
}

std::string operation_printer::operator()(const htlc_redeem_operation& op) const
{
   auto flags = out.flags();
   out << "Redeem HTLC with database id "
         << std::to_string(op.htlc_id.space_id)
         << "." << std::to_string(op.htlc_id.type_id)
         << "." << std::to_string((uint64_t)op.htlc_id.instance)
         << " with preimage \"";
   out << std::hex;
   for (unsigned char c : op.preimage)
      out << c;
   out.flags(flags);
   out << "\"";
   return fee(op.fee);
}

std::string operation_printer::operator()(const htlc_redeemed_operation& op) const
{
   auto flags = out.flags();
   out << "Redeem HTLC with database id "
         << std::to_string(op.htlc_id.space_id)
         << "." << std::to_string(op.htlc_id.type_id)
         << "." << std::to_string((uint64_t)op.htlc_id.instance)
         << " with preimage \"";
   out << std::hex;
   for (unsigned char c : op.preimage)
      out << c;
   out.flags(flags);
   out << "\"";
   return fee(op.fee);
}

std::string operation_printer::operator()(const htlc_create_operation& op) const
{
   static htlc_hash_to_string_visitor vtor;

   auto fee_asset = wallet.get_asset( op.fee.asset_id );
   auto to = wallet.get_account( op.to );
   operation_result_printer rprinter(wallet);
   std::string database_id = result.visit(rprinter);

   out << "Create HTLC to " << to.name << " with id " << database_id
         << " preimage hash: [" << op.preimage_hash.visit( vtor ) << "] ";
   print_memo(wallet, op.extensions.value.memo, out);
   // determine if the block that the HTLC is in is before or after LIB
   int32_t pending_blocks = hist.block_num - wallet.get_dynamic_global_properties().last_irreversible_block_num;
   if (pending_blocks > 0)
      out << " (pending " << std::to_string(pending_blocks) << " blocks)";
   return fee(op.fee);
}

std::string operation_result_printer::operator()(const void_result& x) const
{
   return "";
}

std::string operation_result_printer::operator()(const object_id_type& oid)
{
   return std::string(oid);
}

std::string operation_result_printer::operator()(const asset& a)
{
   return _wallet.get_asset(a.asset_id).amount_to_pretty_string(a);
}

}}} // graphene::wallet::detail
