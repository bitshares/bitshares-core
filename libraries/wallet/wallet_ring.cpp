#include <graphene/wallet/wallet.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/utilities/key_conversion.hpp>

#include <fc/crypto/aes.hpp>

#include "wallet_api_impl.hpp"

#include <bts_ct/bulletproofs_plus.hpp>
#include <bts_ct/clsag.hpp>

#include <cstring>

namespace graphene { namespace wallet {

namespace {

fc::time_point_sec head_time(const detail::wallet_api_impl& impl)
{
   return impl._remote_db->get_dynamic_global_properties().time;
}

btsct::Scalar must_scalar(const fc::sha256& bytes)
{
   btsct::Scalar out;
   FC_ASSERT(btsct::Scalar::from_bytes(reinterpret_cast<const uint8_t*>(bytes.data()), 32, out),
             "value is not a canonical secp256k1 scalar");
   return out;
}

fc::ecc::private_key spend_private(const blind_receipt& receipt, const fc::ecc::private_key& to_priv)
{
   fc::ecc::public_key one_time_pub = receipt.conf.one_time_key;
   auto secret = to_priv.get_shared_secret(one_time_pub);
   auto child = fc::sha256::hash(secret);
   auto spend = to_priv.child(child);
   FC_ASSERT(receipt.control_authority.key_auths.size() == 1, "ring spend needs a single-key receipt");
   FC_ASSERT(receipt.control_authority.key_auths.begin()->first == public_key_type(spend.get_public_key()),
             "receipt authority does not match the derived spend key");
   return spend;
}

stealth_confirmation make_memo(const fc::ecc::private_key& one_time,
                               const public_key_type& to,
                               const public_key_type& from,
                               const asset& amount,
                               const fc::sha256& blind,
                               const commitment_type& commitment)
{
   auto secret = one_time.get_shared_secret(to);
   stealth_confirmation::memo_data memo;
   memo.from = from;
   memo.amount = amount;
   memo.blinding_factor = blind;
   memo.commitment = commitment;
   memo.check = secret._hash[0].value();
   stealth_confirmation conf;
   conf.one_time_key = one_time.get_public_key();
   conf.encrypted_memo = fc::aes_encrypt(secret, fc::raw::pack(memo));
   return conf;
}

vector<blinded_balance_object> list_asset_blinds(detail::wallet_api_impl& impl, asset_id_type asset)
{
   vector<blinded_balance_object> rows;
   commitment_type cursor;
   while (rows.size() < 256) {
      auto page = impl._remote_db->list_blinded_balances(asset, cursor, 100);
      if (page.empty())
         break;
      rows.insert(rows.end(), page.begin(), page.end());
      cursor = page.back().commitment;
      if (page.size() < 100)
         break;
   }
   return rows;
}

struct ring_choice {
   vector<blinded_balance_object> members;
   size_t real = 0;
};

ring_choice choose_ring(const vector<blinded_balance_object>& rows, const commitment_type& real_commitment)
{
   FC_ASSERT(rows.size() >= GRAPHENE_RING_SIZE,
             "need ${n} blinded balances of this asset before a ring spend",
             ("n", GRAPHENE_RING_SIZE)("have", rows.size()));
   size_t real = rows.size();
   for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].commitment == real_commitment) {
         real = i;
         break;
      }
   }
   FC_ASSERT(real < rows.size(), "the receipt commitment is not on the chain");
   size_t begin = real >= GRAPHENE_RING_SIZE - 1 ? real - (GRAPHENE_RING_SIZE - 1) : 0;
   if (begin + GRAPHENE_RING_SIZE > rows.size())
      begin = rows.size() - GRAPHENE_RING_SIZE;
   ring_choice choice;
   choice.members.assign(rows.begin() + static_cast<std::ptrdiff_t>(begin),
                         rows.begin() + static_cast<std::ptrdiff_t>(begin + GRAPHENE_RING_SIZE));
   choice.real = real - begin;
   return choice;
}

} // namespace

blind_confirmation wallet_api::ring_blind_transfer(const string& from_key_or_label,
                                                   const string& to_key_or_label,
                                                   const string& amount_in,
                                                   const string& symbol,
                                                   bool broadcast) const
{ try {
   FC_ASSERT(!is_locked());
   FC_ASSERT(HARDFORK_BSIP_CT3_PASSED(head_time(*my)), "ring transfers are not enabled yet");
   public_key_type from_key = get_public_key(from_key_or_label);
   public_key_type to_key = get_public_key(to_key_or_label);
   auto asset_obj = get_asset(symbol);
   auto amount = asset_obj.amount_from_string(amount_in);
   FC_ASSERT(amount.amount > 0);

   const auto& idx = my->_wallet.blind_receipts.get<by_to_asset_used>();
   auto start = idx.lower_bound(std::make_tuple(from_key, amount.asset_id, false));
   auto end = idx.lower_bound(std::make_tuple(from_key, amount.asset_id, true));
   const blind_receipt* chosen = nullptr;
   for (; start != end; ++start) {
      if (start->amount.amount > amount.amount) {
         chosen = &(*start);
         break;
      }
   }
   FC_ASSERT(chosen != nullptr, "no unspent blinded balance large enough for the amount and a fee");
   auto to_priv = utilities::wif_to_key(get_private_key(from_key));
   FC_ASSERT(to_priv.valid());
   auto spend = spend_private(*chosen, *to_priv);
   auto rows = list_asset_blinds(*my, amount.asset_id);
   auto ring = choose_ring(rows, chosen->commitment());

   const int64_t input_value = chosen->amount.amount.value;
   auto fees = my->_remote_db->get_global_properties().parameters.get_current_fees();
   ring_blind_transfer_operation skeleton;
   skeleton.inputs.resize(1);
   skeleton.inputs[0].members.resize(GRAPHENE_RING_SIZE);
   skeleton.inputs[0].clsag.assign(3 + 33 + 33 + 32 + 1 + 32 * GRAPHENE_RING_SIZE, '\0');
   skeleton.outputs.resize(2);
   skeleton.outputs[0].owner = authority(1, to_key, 1);
   skeleton.outputs[1].owner = authority(1, from_key, 1);
   skeleton.outputs[0].stealth_memo = make_memo(fc::ecc::private_key::generate(), to_key, from_key,
                                                amount, chosen->data.blinding_factor, chosen->commitment());
   skeleton.outputs[1].stealth_memo = skeleton.outputs[0].stealth_memo;
   skeleton.range_proof.assign(662, '\0');
   const int64_t fee_two = fees.calculate_fee(skeleton).amount.value;
   skeleton.outputs.resize(1);
   skeleton.range_proof.assign(596, '\0');
   const int64_t fee_one = fees.calculate_fee(skeleton).amount.value;

   const bool two = input_value > amount.amount.value + fee_two;
   const int64_t fee_value = two ? fee_two : fee_one;
   FC_ASSERT(input_value >= amount.amount.value + fee_value, "blinded balance does not cover the amount and the fee");
   const uint64_t change_value = static_cast<uint64_t>(input_value - amount.amount.value - fee_value);

   fc::sha256 pseudo_blind = fc::sha256::hash(fc::sha256::hash(chosen->data.blinding_factor));
   btsct::Scalar pseudo_scalar = must_scalar(pseudo_blind);
   btsct::Scalar real_blind = must_scalar(chosen->data.blinding_factor);
   btsct::Scalar z = real_blind - pseudo_scalar;
   commitment_type pseudo = fc::ecc::blind(pseudo_blind, static_cast<uint64_t>(input_value));

   auto one_time = fc::ecc::private_key::generate();
   auto from_one_time = fc::ecc::private_key::generate();
   struct planned {
      ring_output out;
      uint64_t value;
      btsct::Scalar blind;
      blind_confirmation::output conf;
   };
   vector<planned> planned_outs;
   auto push_out = [&](const public_key_type& dest, const string& label, const fc::sha256& blind,
                      uint64_t value, const fc::ecc::private_key& ot, const fc::sha512& secret_for_child) {
      auto child = fc::sha256::hash(secret_for_child);
      fc::ecc::public_key dest_pub = dest;
      planned item;
      item.value = value;
      item.blind = must_scalar(blind);
      item.out.commitment = fc::ecc::blind(blind, value);
      item.out.owner = authority(1, public_key_type(dest_pub.child(child)), 1);
      item.out.stealth_memo = make_memo(ot, dest, from_key, asset(value, amount.asset_id), blind, item.out.commitment);
      item.conf.label = label;
      item.conf.pub_key = dest;
      item.conf.decrypted_memo.from = from_key;
      item.conf.decrypted_memo.amount = asset(value, amount.asset_id);
      item.conf.decrypted_memo.blinding_factor = blind;
      item.conf.decrypted_memo.commitment = item.out.commitment;
      item.conf.decrypted_memo.check = secret_for_child._hash[0].value();
      item.conf.confirmation = *item.out.stealth_memo;
      // The chain memo omits `to` (BSIP-CT1). The local receipt keeps it so
      // receive_blind_transfer can select the spend key without scanning.
      item.conf.confirmation.to = dest;
      item.conf.confirmation_receipt = item.conf.confirmation;
      item.conf.auth = item.out.owner;
      planned_outs.push_back(item);
   };

   fc::sha256 to_blind;
   if (!two) {
      to_blind = pseudo_blind;
      auto secret = one_time.get_shared_secret(to_key);
      push_out(to_key, to_key_or_label, to_blind, static_cast<uint64_t>(amount.amount.value), one_time, secret);
   } else {
      auto secret = one_time.get_shared_secret(to_key);
      to_blind = fc::sha256::hash(secret);
      auto from_secret = from_one_time.get_shared_secret(from_key);
      fc::sha256 change_blind = fc::ecc::blind_sum(vector<fc::sha256>{pseudo_blind, to_blind}, 1);
      push_out(to_key, to_key_or_label, to_blind, static_cast<uint64_t>(amount.amount.value), one_time, secret);
      push_out(from_key, from_key_or_label, change_blind, change_value, from_one_time, from_secret);
   }
   std::sort(planned_outs.begin(), planned_outs.end(),
             [](const planned& a, const planned& b) { return a.out.commitment < b.out.commitment; });

   ring_blind_transfer_operation op;
   op.fee = asset(fee_value, amount.asset_id);
   ring_input rin;
   rin.pseudo_commitment = pseudo;
   vector<btsct::Point> pubs, commits;
   for (const auto& member : ring.members) {
      rin.members.push_back(member.commitment);
      FC_ASSERT(member.owner.key_auths.size() == 1, "decoy owner is not a single key");
      auto key = member.owner.key_auths.begin()->first.key_data;
      btsct::Point pub, commit;
      FC_ASSERT(btsct::Point::from_compressed(key.data(), key.size(), pub));
      FC_ASSERT(btsct::Point::from_compressed(member.commitment.data(), member.commitment.size(), commit));
      pubs.push_back(pub);
      commits.push_back(commit);
   }
   btsct::Point real_pub = pubs[ring.real];
   btsct::Scalar spend_scalar = must_scalar(spend.get_secret());
   btsct::Point image = btsct::clsag_key_image(real_pub, spend_scalar);
   uint8_t image_bytes[33];
   image.to_compressed(image_bytes);
   memcpy(rin.key_image.data(), image_bytes, 33);
   op.inputs = {rin};

   vector<uint64_t> values;
   vector<btsct::Scalar> blinds;
   vector<commitment_type> out_c;
   blind_confirmation confirm;
   for (const auto& item : planned_outs) {
      op.outputs.push_back(item.out);
      values.push_back(item.value);
      blinds.push_back(item.blind);
      out_c.push_back(item.out.commitment);
      confirm.outputs.push_back(item.conf);
   }
   auto proof = btsct::bulletproof_plus_prove(values, blinds);
   auto proof_bytes = btsct::bulletproof_plus_serialize(proof);
   op.range_proof.assign(reinterpret_cast<const char*>(proof_bytes.data()),
                         reinterpret_cast<const char*>(proof_bytes.data()) + proof_bytes.size());

   auto digest = ring_signature_message(op.fee, asset(), account_id_type(), blind_factor_type(),
                                        op.inputs, out_c, op.range_proof);
   uint8_t msg[32];
   memcpy(msg, digest.data(), 32);
   btsct::Point pseudo_point;
   FC_ASSERT(btsct::Point::from_compressed(pseudo.data(), pseudo.size(), pseudo_point));
   auto sig = btsct::clsag_sign(msg, pubs, spend_scalar, commits, z, pseudo_point, ring.real);
   auto sig_bytes = btsct::clsag_serialize(sig);
   op.inputs[0].clsag.assign(reinterpret_cast<const char*>(sig_bytes.data()),
                             reinterpret_cast<const char*>(sig_bytes.data()) + sig_bytes.size());
   FC_ASSERT(fees.calculate_fee(op).amount <= op.fee.amount, "fee grew after the signature was attached");
   op.validate();
   confirm.trx.operations = {op};
   confirm.trx = my->sign_transaction(confirm.trx, broadcast);
   if (broadcast) {
      auto itr = my->_wallet.blind_receipts.get<by_commitment>().find(chosen->commitment());
      if (itr != my->_wallet.blind_receipts.get<by_commitment>().end())
         my->_wallet.blind_receipts.modify(itr, [](blind_receipt& r) { r.used = true; });
      for (const auto& out : confirm.outputs)
         receive_blind_transfer(out.confirmation_receipt, from_key_or_label, "");
   }
   return confirm;
} FC_CAPTURE_AND_RETHROW((from_key_or_label)(to_key_or_label)(amount_in)(symbol)(broadcast)) }

blind_confirmation wallet_api::ring_transfer_from_blind(const string& from_key_or_label,
                                                       const string& to_account_id_or_name,
                                                       const string& amount_in,
                                                       const string& symbol,
                                                       bool broadcast) const
{ try {
   FC_ASSERT(!is_locked());
   FC_ASSERT(HARDFORK_BSIP_CT3_PASSED(head_time(*my)), "ring transfers are not enabled yet");
   public_key_type from_key = get_public_key(from_key_or_label);
   account_object to_account = my->get_account(to_account_id_or_name);
   auto asset_obj = get_asset(symbol);
   auto amount = asset_obj.amount_from_string(amount_in);
   FC_ASSERT(amount.amount > 0);

   const auto& idx = my->_wallet.blind_receipts.get<by_to_asset_used>();
   auto start = idx.lower_bound(std::make_tuple(from_key, amount.asset_id, false));
   auto end = idx.lower_bound(std::make_tuple(from_key, amount.asset_id, true));
   const blind_receipt* chosen = nullptr;
   for (; start != end; ++start) {
      if (start->amount.amount > amount.amount) {
         chosen = &(*start);
         break;
      }
   }
   FC_ASSERT(chosen != nullptr, "no unspent blinded balance large enough to exit");
   auto to_priv = utilities::wif_to_key(get_private_key(from_key));
   FC_ASSERT(to_priv.valid());
   auto spend = spend_private(*chosen, *to_priv);
   auto rows = list_asset_blinds(*my, amount.asset_id);
   auto ring = choose_ring(rows, chosen->commitment());
   const int64_t input_value = chosen->amount.amount.value;

   auto fees = my->_remote_db->get_global_properties().parameters.get_current_fees();
   ring_transfer_from_blind_operation skeleton;
   skeleton.amount = amount;
   skeleton.to = to_account.id;
   skeleton.inputs.resize(1);
   skeleton.inputs[0].members.resize(GRAPHENE_RING_SIZE);
   skeleton.inputs[0].clsag.assign(3 + 33 + 33 + 32 + 1 + 32 * GRAPHENE_RING_SIZE, '\0');
   skeleton.changes.resize(1);
   skeleton.changes[0].owner = authority(1, from_key, 1);
   skeleton.changes[0].stealth_memo = make_memo(fc::ecc::private_key::generate(), from_key, from_key,
                                                amount, chosen->data.blinding_factor, chosen->commitment());
   skeleton.range_proof.assign(596, '\0');
   const int64_t fee_change = fees.calculate_fee(skeleton).amount.value;
   skeleton.changes.clear();
   skeleton.range_proof.clear();
   const int64_t fee_full = fees.calculate_fee(skeleton).amount.value;
   const bool with_change = input_value > amount.amount.value + fee_change;
   const int64_t fee_value = with_change ? fee_change : fee_full;
   FC_ASSERT(input_value >= amount.amount.value + fee_value);
   const uint64_t change_value = static_cast<uint64_t>(input_value - amount.amount.value - fee_value);

   fc::sha256 pseudo_blind = fc::sha256::hash(fc::sha256::hash(chosen->data.blinding_factor));
   btsct::Scalar z = must_scalar(chosen->data.blinding_factor) - must_scalar(pseudo_blind);
   commitment_type pseudo = fc::ecc::blind(pseudo_blind, static_cast<uint64_t>(input_value));

   ring_transfer_from_blind_operation op;
   op.fee = asset(fee_value, amount.asset_id);
   op.amount = amount;
   op.to = to_account.id;
   op.blinding_factor = pseudo_blind;
   if (with_change) {
      fc::sha256 change_blind = pseudo_blind;
      // Public commitment uses a different blind so the two output blinds sum to the pseudo blind.
      fc::sha256 public_blind = fc::sha256::hash(pseudo_blind);
      change_blind = fc::ecc::blind_sum(vector<fc::sha256>{pseudo_blind, public_blind}, 1);
      op.blinding_factor = public_blind;
      auto ot = fc::ecc::private_key::generate();
      auto secret = ot.get_shared_secret(from_key);
      auto child = fc::sha256::hash(secret);
      fc::ecc::public_key from_pub = from_key;
      ring_output change;
      change.commitment = fc::ecc::blind(change_blind, change_value);
      change.owner = authority(1, public_key_type(from_pub.child(child)), 1);
      change.stealth_memo = make_memo(ot, from_key, from_key, asset(change_value, amount.asset_id),
                                     change_blind, change.commitment);
      op.changes = {change};
      auto proof = btsct::bulletproof_plus_prove({change_value}, {must_scalar(change_blind)});
      auto bytes = btsct::bulletproof_plus_serialize(proof);
      op.range_proof.assign(reinterpret_cast<const char*>(bytes.data()),
                            reinterpret_cast<const char*>(bytes.data()) + bytes.size());
   }

   ring_input rin;
   rin.pseudo_commitment = pseudo;
   vector<btsct::Point> pubs, commits;
   for (const auto& member : ring.members) {
      rin.members.push_back(member.commitment);
      auto key = member.owner.key_auths.begin()->first.key_data;
      btsct::Point pub, commit;
      FC_ASSERT(btsct::Point::from_compressed(key.data(), key.size(), pub));
      FC_ASSERT(btsct::Point::from_compressed(member.commitment.data(), member.commitment.size(), commit));
      pubs.push_back(pub);
      commits.push_back(commit);
   }
   btsct::Scalar spend_scalar = must_scalar(spend.get_secret());
   btsct::Point image = btsct::clsag_key_image(pubs[ring.real], spend_scalar);
   uint8_t image_bytes[33];
   image.to_compressed(image_bytes);
   memcpy(rin.key_image.data(), image_bytes, 33);
   op.inputs = {rin};

   vector<commitment_type> out_c;
   for (const auto& c : op.changes)
      out_c.push_back(c.commitment);
   auto digest = ring_signature_message(op.fee, op.amount, op.to, op.blinding_factor, op.inputs, out_c, op.range_proof);
   uint8_t msg[32];
   memcpy(msg, digest.data(), 32);
   btsct::Point pseudo_point;
   FC_ASSERT(btsct::Point::from_compressed(pseudo.data(), pseudo.size(), pseudo_point));
   auto sig = btsct::clsag_sign(msg, pubs, spend_scalar, commits, z, pseudo_point, ring.real);
   auto sig_bytes = btsct::clsag_serialize(sig);
   op.inputs[0].clsag.assign(reinterpret_cast<const char*>(sig_bytes.data()),
                             reinterpret_cast<const char*>(sig_bytes.data()) + sig_bytes.size());
   FC_ASSERT(fees.calculate_fee(op).amount <= op.fee.amount);
   op.validate();

   blind_confirmation confirm;
   if (with_change) {
      blind_confirmation::output conf;
      conf.label = from_key_or_label;
      conf.pub_key = from_key;
      conf.confirmation = *op.changes[0].stealth_memo;
      conf.confirmation.to = from_key;
      conf.confirmation_receipt = conf.confirmation;
      conf.auth = op.changes[0].owner;
      conf.decrypted_memo.amount = asset(change_value, amount.asset_id);
      conf.decrypted_memo.blinding_factor = fc::ecc::blind_sum(vector<fc::sha256>{pseudo_blind, op.blinding_factor}, 1);
      conf.decrypted_memo.commitment = op.changes[0].commitment;
      confirm.outputs.push_back(conf);
   }
   confirm.trx.operations = {op};
   confirm.trx = my->sign_transaction(confirm.trx, broadcast);
   if (broadcast) {
      auto itr = my->_wallet.blind_receipts.get<by_commitment>().find(chosen->commitment());
      if (itr != my->_wallet.blind_receipts.get<by_commitment>().end())
         my->_wallet.blind_receipts.modify(itr, [](blind_receipt& r) { r.used = true; });
      for (const auto& out : confirm.outputs)
         receive_blind_transfer(out.confirmation_receipt, from_key_or_label, "ring change");
   }
   return confirm;
} FC_CAPTURE_AND_RETHROW((from_key_or_label)(to_account_id_or_name)(amount_in)(symbol)(broadcast)) }

} } // graphene::wallet
