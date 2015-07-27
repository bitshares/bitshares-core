#include "Operations.hpp"

#include <fc/smart_ref_impl.hpp>

TransferOperation* OperationBuilder::transfer(ObjectId sender, ObjectId receiver, qint64 amount,
                                             ObjectId amountType, QString memo, ObjectId feeType)
{
   try {
      TransferOperation* op = new TransferOperation;
      op->setSender(sender);
      op->setReceiver(receiver);
      op->setAmount(amount);
      op->setAmountType(amountType);
      op->setMemo(memo);
      op->setFeeType(feeType);
      auto feeParameters = model.global_properties().parameters.current_fees->get<graphene::chain::transfer_operation>();
      op->setFee(op->operation().calculate_fee(feeParameters).value);
      return op;
   } catch (const fc::exception& e) {
      qDebug() << e.to_detail_string().c_str();
      return nullptr;
   }
}

QString TransferOperation::memo() const {
   if (!m_op.memo)
      return QString::null;
   QString memo = QString::fromStdString(m_op.memo->get_message({}, {}));
   while (memo.endsWith('\0'))
      memo.chop(1);
   return memo;
}

void TransferOperation::setMemo(QString memo) {
   if (memo == this->memo())
      return;
   if (!m_op.memo)
      m_op.memo = graphene::chain::memo_data();
   while (memo.size() % 32)
      memo.append('\0');
   m_op.memo->set_message({}, {}, memo.toStdString());
   Q_EMIT memoChanged();
}
