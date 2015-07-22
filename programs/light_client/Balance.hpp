#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include "GrapheneObject.hpp"

namespace graphene { namespace chain {
class account_balance_object;
}}

class Asset;
class Balance : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(Asset* type MEMBER m_type READ type NOTIFY typeChanged)
   Q_PROPERTY(qint64 amount MEMBER amount NOTIFY amountChanged)

   Asset* m_type;
   qint64 amount;

public:
   // This ultimately needs to be replaced with a string equivalent
   Q_INVOKABLE qreal amountReal() const;

   Asset* type()const {
      return m_type;
   }

   void update(const graphene::chain::account_balance_object& update);

Q_SIGNALS:
   void typeChanged();
   void amountChanged();
};
