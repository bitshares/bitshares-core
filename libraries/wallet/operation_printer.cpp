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

string operation_printer::print_memo( const fc::optional<graphene::protocol::memo_data>& memo )const
{
   std::string outstr;
   if( memo )
   {
      if( wallet.is_locked() )
      {
         out << " -- Unlock wallet to see memo.";
      } else {
         try {
            FC_ASSERT( wallet._keys.count(memo->to) > 0 || wallet._keys.count(memo->from) > 0,
                       "Memo is encrypted to a key ${to} or ${from} not in this wallet.",
                       ("to", memo->to)("from",memo->from) );
            if( wallet._keys.count(memo->to) > 0 ) {
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

void operation_printer::print_preimage(const std::vector<char>& preimage)const
{
   if (preimage.size() == 0)
      return;
   out << " with preimage \"";
   // cut it at 300 bytes max
   auto flags = out.flags();
   out << std::hex << setw(2) << setfill('0');
   for (size_t i = 0; i < std::min<size_t>(300, preimage.size()); i++)
      out << +preimage[i];
   out.flags(flags);
   if (preimage.size() > 300)
      out << "...(truncated due to size)";
   out << "\"";
}

string operation_printer::print_redeem(const graphene::protocol::htlc_id_type& id,
      const std::string& redeemer, const std::vector<char>& preimage, 
      const graphene::protocol::asset& op_fee)const
{
   out << redeemer << " redeemed HTLC with id "
         << std::string( static_cast<object_id_type>(id));
   print_preimage( preimage );
   return fee(op_fee);
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

string operation_printer::operator()(const transfer_operation& op) const
{
   out << "Transfer " << wallet.get_asset(op.amount.asset_id).amount_to_pretty_string(op.amount)
       << " from " << wallet.get_account(op.from).name << " to " << wallet.get_account(op.to).name;
   std::string memo = print_memo( op.memo );
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
   return print_redeem(op.htlc_id, wallet.get_account(op.redeemer).name, op.preimage, op.fee);
}

std::string operation_printer::operator()(const htlc_redeemed_operation& op) const
{
   return print_redeem(op.htlc_id, wallet.get_account(op.redeemer).name, op.preimage, op.fee);
}

std::string operation_printer::operator()(const htlc_create_operation& op) const
{
   static htlc_hash_to_string_visitor vtor;

   auto fee_asset = wallet.get_asset( op.fee.asset_id );
   auto to = wallet.get_account( op.to );
   auto from = wallet.get_account( op.from );
   operation_result_printer rprinter(wallet);
   std::string database_id = result.visit(rprinter);

   out << "Create HTLC from " << from.name << " to " << to.name 
         << " with id " << database_id
         << " preimage hash: [" << op.preimage_hash.visit( vtor ) << "] ";
   print_memo( op.extensions.value.memo );
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

std::string operation_result_printer::operator()(const generic_operation_result& r)
{
   return fc::json::to_string(r);
}

std::string operation_result_printer::operator()(const generic_exchange_operation_result& r)
{
   // TODO show pretty amounts instead of raw json
   return fc::json::to_string(r);
}

}}} // graphene::wallet::detail
