#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <graphene/chain/protocol/transaction.hpp>

#include <QObject>
#include <QQmlListProperty>

class OperationBase;
class Transaction : public QObject {
   Q_OBJECT

public:
   enum Status { Unbroadcasted, Pending, Complete, Failed };
   Q_ENUM(Status);

   Status status() const { return m_status; }
   QQmlListProperty<OperationBase> operations();

   OperationBase* operationAt(int index) const;
   int operationCount() const {
      return m_transaction.operations.size();
   }

public slots:
   void setStatus(Status status)
   {
      if (status == m_status)
         return;

      m_status = status;
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

signals:
   void statusChanged(Status status);
   void operationsChanged();

private:
   Q_PROPERTY(Status status READ status WRITE setStatus NOTIFY statusChanged)
   Q_PROPERTY(QQmlListProperty<OperationBase> operations READ operations NOTIFY operationsChanged)

   Status m_status;
   graphene::chain::transaction m_transaction;
};
