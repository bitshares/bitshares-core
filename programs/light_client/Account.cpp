#include "Balance.hpp"
#include "ChainDataModel.hpp"

#include <graphene/chain/account_object.hpp>

QQmlListProperty<Balance> Account::balances()
{
   auto count = [](QQmlListProperty<Balance>* list) {
      return reinterpret_cast<Account*>(list->data)->m_balances.size();
   };
   auto at = [](QQmlListProperty<Balance>* list, int index) {
      return reinterpret_cast<Account*>(list->data)->m_balances[index];
   };

   return QQmlListProperty<Balance>(this, this, count, at);
}

void Account::update(const graphene::chain::account_balance_object& balance)
{
   auto balanceItr = std::find_if(m_balances.begin(), m_balances.end(),
                                  [&balance](Balance* b) { return b->type()->id() == balance.asset_type.instance.value; });

   if (balanceItr != m_balances.end()) {
      ilog("Updating ${a}'s balance: ${b}", ("a", m_name.toStdString())("b", balance));
      (*balanceItr)->update(balance);
      Q_EMIT balancesChanged();
   } else {
      ilog("Adding to ${a}'s new balance: ${b}", ("a", m_name.toStdString())("b", balance));
      Balance* newBalance = new Balance;
      newBalance->setParent(this);
      auto model = qobject_cast<ChainDataModel*>(parent());
      newBalance->setProperty("type", QVariant::fromValue(model->getAsset(balance.asset_type.instance.value)));
      newBalance->setProperty("amount", QVariant::fromValue(balance.balance.value));
      m_balances.append(newBalance);
      Q_EMIT balancesChanged();
   }
}
