#include "Transaction.hpp"
#include "Operations.hpp"

#include <fc/reflect/typename.hpp>

struct OperationConverter {
   using result_type = OperationBase*;

   OperationBase* operator()(const graphene::chain::transfer_operation& op) const {
      auto ret = new TransferOperation(op);
      QObject::connect(ret, &QObject::destroyed, []{qDebug("Cleaned up operation");});
      return ret;
   }

   template<typename Op>
   OperationBase* operator()(const Op&) const {
      elog("NYI: OperationConverter for ${type}", ("type", fc::get_typename<Op>::name()));
      abort();
   }
};

QString Transaction::statusString() const
{
   return QMetaEnum::fromType<Status>().valueToKey(status());
}

QQmlListProperty<OperationBase> Transaction::operations()
{
   auto append = [](QQmlListProperty<OperationBase>* list, OperationBase* op) {
      static_cast<Transaction*>(list->data)->appendOperation(op);
   };
   auto count = [](QQmlListProperty<OperationBase>* list) {
      return static_cast<Transaction*>(list->data)->operationCount();
   };
   auto at = [](QQmlListProperty<OperationBase>* list, int index) {
      return static_cast<Transaction*>(list->data)->operationAt(index);
   };
   auto clear = [](QQmlListProperty<OperationBase>* list) {
      static_cast<Transaction*>(list->data)->clearOperations();
   };

   return QQmlListProperty<OperationBase>(this, this, append, count, at, clear);
}

OperationBase* Transaction::operationAt(int index) const {
   return m_transaction.operations[index].visit(OperationConverter());
}

void Transaction::appendOperation(OperationBase* op)
{
   if (op == nullptr)
   {
      qWarning("Unable to append null operation to transaction");
      return;
   }
   op->setParent(this);
   m_transaction.operations.push_back(op->genericOperation());
   Q_EMIT operationsChanged();
}
