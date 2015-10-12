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
#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include "GrapheneObject.hpp"

#include <QQmlListProperty>
#include <graphene/app/full_account.hpp>

namespace graphene { namespace chain {
class account_balance_object;
}}

using graphene::chain::account_object;
using graphene::chain::account_balance_object;

class Balance;
class Wallet;

class Account : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(QString name READ name NOTIFY nameChanged)
   Q_PROPERTY(QQmlListProperty<Balance> balances READ balances NOTIFY balancesChanged)
   Q_PROPERTY(QString memoKey READ memoKey NOTIFY memoKeyChanged)
   Q_PROPERTY(bool isLoaded MEMBER m_loaded NOTIFY loaded)

   account_object  m_account;
   QList<Balance*> m_balances;
   bool m_loaded = false;

public:
   Account(ObjectId id = -1, QString name = QString(), QObject* parent = nullptr)
      : GrapheneObject(id, parent)
   {
      m_account.name = name.toStdString();
   }
   void setAccountObject(const account_object& obj);
   const account_object& accountObject()const {
      return m_account;
   }

   QString name()const { return QString::fromStdString(m_account.name); }
   QString memoKey()const;
   QQmlListProperty<Balance> balances();

   void setBalances(QList<Balance*> balances) {
      if (balances != m_balances) {
         m_balances = balances;
         Q_EMIT balancesChanged();
      }
   }

   void update(const account_balance_object& balance);

   /**
    * Anything greater than 1.0 means full authority.
    * Anything between (0 and 1.0) means partial authority
    * 0 means no authority.
    *
    * @return the percent of direct control the wallet has over the account.
    */
   Q_INVOKABLE double getOwnerControl(Wallet* w)const;
   Q_INVOKABLE double getActiveControl(Wallet* w , int depth = 0)const;

Q_SIGNALS:
   void nameChanged();
   void balancesChanged();
   void memoKeyChanged();
   void loaded();
};
