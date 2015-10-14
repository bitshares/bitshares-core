/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#include "ChainDataModel.hpp"

#include <graphene/chain/protocol/transfer.hpp>

#include <QObject>
#include <QtQml>

class OperationBase : public QObject {
   Q_OBJECT
   Q_PROPERTY(OperationType operationType READ operationType CONSTANT STORED false)

public:
   enum OperationType {
      TransferOperationType = graphene::chain::operation::tag<graphene::chain::transfer_operation>::value
   };
   Q_ENUM(OperationType);

   OperationBase(QObject* parent = nullptr)
      : QObject(parent) {}
   virtual ~OperationBase() {}

   virtual OperationType operationType() const = 0;
   virtual graphene::chain::operation genericOperation() const = 0;
};
QML_DECLARE_INTERFACE(OperationBase)

class TransferOperation : public OperationBase {
   Q_OBJECT
   Q_PROPERTY(qint64 fee READ fee WRITE setFee NOTIFY feeChanged)
   Q_PROPERTY(ObjectId feeType READ feeType WRITE setFeeType NOTIFY feeTypeChanged)
   Q_PROPERTY(ObjectId sender READ sender WRITE setSender NOTIFY senderChanged)
   Q_PROPERTY(ObjectId receiver READ receiver WRITE setReceiver NOTIFY receiverChanged)
   Q_PROPERTY(qint64 amount READ amount WRITE setAmount NOTIFY amountChanged)
   Q_PROPERTY(ObjectId amountType READ amountType WRITE setAmountType NOTIFY amountTypeChanged)
   Q_PROPERTY(QString memo READ memo WRITE setMemo NOTIFY memoChanged)
   Q_PROPERTY(bool memoIsEncrypted READ memoIsEncrypted NOTIFY memoChanged)

   graphene::chain::transfer_operation m_op;

public:
   TransferOperation(){}
   TransferOperation(const graphene::chain::transfer_operation& op)
      : m_op(op) {}

   virtual OperationBase::OperationType operationType() const override {
      return OperationBase::TransferOperationType;
   }
   virtual graphene::chain::operation genericOperation() const override {
      return m_op;
   }

   qint64 fee() const { return m_op.fee.amount.value; }
   ObjectId feeType() const { return m_op.fee.asset_id.instance.value; }
   ObjectId sender() const { return m_op.from.instance.value; }
   ObjectId receiver() const { return m_op.to.instance.value; }
   qint64 amount() const { return m_op.amount.amount.value; }
   ObjectId amountType() const { return m_op.amount.asset_id.instance.value; }
   /// This does not deal with encrypted memos. The memo stored here is unencrypted. The encryption step must be
   /// performed by calling encryptMemo()
   QString memo() const;

   bool memoIsEncrypted()const;
   Q_INVOKABLE bool canEncryptMemo(Wallet* wallet, ChainDataModel* model) const;
   Q_INVOKABLE bool canDecryptMemo(Wallet* wallet, ChainDataModel* model) const;
   Q_INVOKABLE QString decryptedMemo(Wallet* wallet, ChainDataModel* model) const;

   const graphene::chain::transfer_operation& operation() const { return m_op; }
   graphene::chain::transfer_operation& operation() { return m_op; }

public Q_SLOTS:
   void setFee(qint64 arg) {
      if (arg == fee())
         return;
      m_op.fee.amount = arg;
      Q_EMIT feeChanged();
   }
   void setFeeType(ObjectId arg) {
      if (arg == feeType())
         return;
      m_op.fee.asset_id = arg;
      Q_EMIT feeTypeChanged();
   }
   void setSender(ObjectId arg) {
      if (arg == sender())
         return;
      m_op.from = arg;
      Q_EMIT senderChanged();
   }
   void setReceiver(ObjectId arg) {
      if (arg == receiver())
         return;
      m_op.to = arg;
      Q_EMIT receiverChanged();
   }
   void setAmount(qint64 arg) {
      if (arg == amount())
         return;
      m_op.amount.amount = arg;
      Q_EMIT amountChanged();
   }
   void setAmountType(ObjectId arg) {
      if (arg == amountType())
         return;
      m_op.amount.asset_id = arg;
      Q_EMIT amountTypeChanged();
   }
   /// This does not deal with encrypted memos. The memo stored here is unencrypted. The encryption step must be
   /// performed by calling encryptMemo()
   void setMemo(QString memo);
   void encryptMemo(Wallet* wallet, ChainDataModel* model);

Q_SIGNALS:
   void feeChanged();
   void feeTypeChanged();
   void senderChanged();
   void receiverChanged();
   void amountChanged();
   void amountTypeChanged();
   void memoChanged();
};
QML_DECLARE_TYPE(TransferOperation)

/**
 * @brief The OperationBuilder class creates operations which are inspectable by the GUI
 *
 * @note All operations returned by OperationBuilder are heap allocated on-demand and do not have parents. The caller
 * must take ownership of these objects to prevent them from leaking.
 */
class OperationBuilder : public QObject {
   Q_OBJECT

   ChainDataModel& model;

public:
   OperationBuilder(ChainDataModel& model, QObject* parent = nullptr)
      : QObject(parent), model(model){}

   Q_INVOKABLE TransferOperation* transfer(ObjectId sender, ObjectId receiver, qint64 amount,
                                           ObjectId amountType, QString memo, ObjectId feeType);

};
