#include "Operations.hpp"

#include <fc/smart_ref_impl.hpp>

TransferOperation OperationBuilder::transfer(ObjectId sender, ObjectId receiver, qint64 amount,
                                             ObjectId amountType, QString memo, ObjectId feeType)
{
   static fc::ecc::private_key dummyPrivate = fc::ecc::private_key::generate();
   static fc::ecc::public_key dummyPublic = fc::ecc::private_key::generate().get_public_key();
   TransferOperation op;
   op.setSender(sender);
   op.setReceiver(receiver);
   op.setAmount(amount);
   op.setAmountType(amountType);
   op.setMemo(memo);
   op.setFeeType(feeType);
   auto feeParameters = model.global_properties().parameters.current_fees->get<graphene::chain::transfer_operation>();
   op.operation().memo = graphene::chain::memo_data();
   op.operation().memo->set_message(dummyPrivate, dummyPublic, memo.toStdString());
   op.setFee(op.operation().calculate_fee(feeParameters).value);
   return op;
}
