#pragma once

#include "ClientDataModel.hpp"

#include <graphene/chain/protocol/transfer.hpp>

#include <QObject>

class TransferOperation {
   Q_GADGET
   Q_PROPERTY(qint64 fee READ fee)
   Q_PROPERTY(ObjectId feeType READ feeType)
   Q_PROPERTY(ObjectId sender READ sender WRITE setSender)
   Q_PROPERTY(ObjectId receiver READ receiver WRITE setReceiver)
   Q_PROPERTY(qint64 amount READ amount WRITE setAmount)
   Q_PROPERTY(ObjectId amountType READ amountType WRITE setAmountType)
   Q_PROPERTY(QString memo READ memo WRITE setMemo)

   graphene::chain::transfer_operation m_op;
   QString m_memo;

public:
   Q_INVOKABLE qint64 fee() const { return m_op.fee.amount.value; }
   Q_INVOKABLE void setFee(qint64 fee) { m_op.fee.amount = fee; }

   Q_INVOKABLE ObjectId feeType() const { return m_op.fee.asset_id.instance.value; }
   Q_INVOKABLE void setFeeType(ObjectId feeType) { m_op.fee.asset_id = feeType; }

   Q_INVOKABLE ObjectId sender() const { return m_op.from.instance.value; }
   Q_INVOKABLE void setSender(ObjectId sender) { m_op.from = sender; }

   Q_INVOKABLE ObjectId receiver() const { return m_op.to.instance.value; }
   Q_INVOKABLE void setReceiver(ObjectId receiver) { m_op.to = receiver; }

   Q_INVOKABLE qint64 amount() const { return m_op.amount.amount.value; }
   Q_INVOKABLE void setAmount(qint64 amount) { m_op.amount.amount = amount; }

   Q_INVOKABLE ObjectId amountType() const { return m_op.amount.asset_id.instance.value; }
   Q_INVOKABLE void setAmountType(ObjectId assetType) { m_op.amount.asset_id = assetType; }

   /// This does not deal with encrypted memos. The memo stored here is unencrypted, and does not get stored in the
   /// underlying graphene operation. The encryption and storage steps must be handled elsewhere.
   Q_INVOKABLE QString memo() const { return m_memo; }
   /// This does not deal with encrypted memos. The memo stored here is unencrypted, and does not get stored in the
   /// underlying graphene operation. The encryption and storage steps must be handled elsewhere.
   Q_INVOKABLE void setMemo(QString memo) { m_memo = memo; }

   const graphene::chain::transfer_operation& operation() const { return m_op; }
   graphene::chain::transfer_operation& operation() { return m_op; }
};
Q_DECLARE_METATYPE(TransferOperation)

class OperationBuilder : public QObject {
   Q_OBJECT

   ChainDataModel& model;

public:
   OperationBuilder(ChainDataModel& model, QObject* parent = nullptr)
      : QObject(parent), model(model){}

   Q_INVOKABLE TransferOperation transfer(ObjectId sender, ObjectId receiver,
                                          qint64 amount, ObjectId amountType, QString memo, ObjectId feeType);
};
