/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
