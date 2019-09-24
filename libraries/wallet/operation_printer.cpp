#include <graphene/wallet/operation_printer.hpp>

#include <graphene/protocol/base.hpp>

#include <graphene/wallet/wallet_api_impl.hpp>

namespace graphene { namespace wallet { namespace detail {

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
string operation_printer::operator()(const transfer_operation& op) const
{
   out << "Transfer " << wallet.get_asset(op.amount.asset_id).amount_to_pretty_string(op.amount)
       << " from " << wallet.get_account(op.from).name << " to " << wallet.get_account(op.to).name;
   std::string memo;
   if( op.memo )
   {
      if( wallet.is_locked() )
      {
         out << " -- Unlock wallet to see memo.";
      } else {
         try {
            FC_ASSERT( wallet._keys.count(op.memo->to) || wallet._keys.count(op.memo->from),
                       "Memo is encrypted to a key ${to} or ${from} not in this wallet.",
                       ("to", op.memo->to)("from",op.memo->from) );
            if( wallet._keys.count(op.memo->to) ) {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->to));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->from);
               out << " -- Memo: " << memo;
            } else {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->from));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->to);
               out << " -- Memo: " << memo;
            }
         } catch (const fc::exception& e) {
            out << " -- could not decrypt memo";
         }
      }
   }
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
   out << "Redeem HTLC with database id "
         << std::to_string(op.htlc_id.space_id)
         << "." << std::to_string(op.htlc_id.type_id)
         << "." << std::to_string((uint64_t)op.htlc_id.instance)
         << " with preimage \"";
   for (unsigned char c : op.preimage)
      out << c;
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

   out << "Create HTLC to " << to.name
         << " with id " << database_id
         << " preimage hash: ["
         << op.preimage_hash.visit( vtor )
         << "] (Fee: " << fee_asset.amount_to_pretty_string( op.fee ) << ")";
   // determine if the block that the HTLC is in is before or after LIB
   int32_t pending_blocks = hist.block_num - wallet.get_dynamic_global_properties().last_irreversible_block_num;
   if (pending_blocks > 0)
      out << " (pending " << std::to_string(pending_blocks) << " blocks)";

   return "";
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