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
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <graphene/chain/protocol/transaction.hpp>

#include <QDateTime>
#include <QObject>
#include <QQmlListProperty>
#include <QDebug>

class OperationBase;
class Transaction : public QObject {
   Q_OBJECT

public:
   enum Status { Unbroadcasted, Pending, Complete, Failed };
   Q_ENUM(Status);

   Status status() const { return m_status; }
   QString statusString() const;
   QQmlListProperty<OperationBase> operations();

   OperationBase* operationAt(int index) const;
   int operationCount() const {
      return m_transaction.operations.size();
   }

   graphene::chain::signed_transaction& internalTransaction() {
      return m_transaction;
   }

   QDateTime expiration() const
   {
      return QDateTime::fromTime_t(m_transaction.expiration.sec_since_epoch());
   }

public Q_SLOTS:
   void setStatus(Status status)
   {
      if (status == m_status)
         return;

      m_status = status;
      qDebug() << status;
      emit statusChanged(status);
   }

   /**
    * @brief Append the operation to the transaction
    * @param op The operation to append. This Transaction will take ownership of the operation.
    */
   void appendOperation(OperationBase* op);
   void clearOperations() {
      m_transaction.operations.clear();
      Q_EMIT operationsChanged();
   }

   void setExpiration(QDateTime expiration)
   {
      fc::time_point_sec exp(expiration.toTime_t());
      if (exp == m_transaction.expiration)
         return;

      m_transaction.expiration = exp;
      emit expirationChanged(expiration);
   }

signals:
   void statusChanged(Status status);
   void operationsChanged();

   void expirationChanged(QDateTime expiration);

private:
   Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged)
   Q_PROPERTY(QString statusString READ statusString NOTIFY statusChanged STORED false)
   Q_PROPERTY(QQmlListProperty<OperationBase> operations READ operations NOTIFY operationsChanged)
   Q_PROPERTY(QDateTime expiration READ expiration WRITE setExpiration NOTIFY expirationChanged)

   Status m_status = Unbroadcasted;
   graphene::chain::signed_transaction m_transaction;
};
