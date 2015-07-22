#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include "GrapheneObject.hpp"

#include <QQmlListProperty>

namespace graphene { namespace chain {
class account_balance_object;
}}

class Balance;
class Account : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(QString name MEMBER m_name READ name NOTIFY nameChanged)
   Q_PROPERTY(QQmlListProperty<Balance> balances READ balances NOTIFY balancesChanged)

   QString m_name;
   QList<Balance*> m_balances;

public:
   Account(ObjectId id = -1, QString name = QString(), QObject* parent = nullptr)
      : GrapheneObject(id, parent), m_name(name)
   {}

   QString name()const { return m_name; }
   QQmlListProperty<Balance> balances();

   void setBalances(QList<Balance*> balances) {
      if (balances != m_balances) {
         m_balances = balances;
         Q_EMIT balancesChanged();
      }
   }

   void update(const graphene::chain::account_balance_object& balance);

Q_SIGNALS:
   void nameChanged();
   void balancesChanged();
};
