/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
