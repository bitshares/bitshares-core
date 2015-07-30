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
