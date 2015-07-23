#include "Balance.hpp"
#include "ChainDataModel.hpp"
#include "Wallet.hpp"

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

double Account::getActiveControl( Wallet* w )const
{
   if( m_account.active.num_auths() == 0 ) return 0;
   if( m_account.active.weight_threshold == 0 ) return 0;

   uint64_t weight = 0;
   for( auto& key : m_account.active.key_auths )
   {
      if( w->hasPrivateKey( toQString(key.first) ) ) weight += key.second;
   }
   for( auto& acnt :  m_account.active.account_auths )
   {
      // TODO: lookup Account, check to see if we have full control of it, and
      // add its weight if we do.  Be sure to limit recursion depth
   }

   return double(weight) / double( m_account.active.weight_threshold );
}

double Account::getOwnerControl( Wallet* w )const
{
   if( m_account.owner.num_auths() == 0 ) return 0;
   if( m_account.owner.weight_threshold == 0 ) return 0;
   uint64_t weight = 0;
   for( auto& key : m_account.owner.key_auths )
   {
      if( w->hasPrivateKey( toQString(key.first) ) ) weight += key.second;
   }
   for( auto& acnt :  m_account.owner.account_auths )
   {
      // TODO: lookup Account, check to see if we have full *ACTIVE* control of it, and
      // add its weight if we do.  Be sure to limit recursion depth
   }

   return double(weight) / double( m_account.owner.weight_threshold );
}

void Account::update(const graphene::chain::account_balance_object& balance)
{
   auto balanceItr = std::find_if(m_balances.begin(), m_balances.end(),
                                  [&balance](Balance* b) { return b->type()->id() == balance.asset_type.instance.value; });

   if (balanceItr != m_balances.end()) {
      ilog("Updating ${a}'s balance: ${b}", ("a", name().toStdString())("b", balance));
      (*balanceItr)->update(balance);
      Q_EMIT balancesChanged();
   } else {
      ilog("Adding to ${a}'s new balance: ${b}", ("a", name().toStdString())("b", balance));
      Balance* newBalance = new Balance;
      newBalance->setParent(this);
      auto model = qobject_cast<ChainDataModel*>(parent());
      newBalance->setProperty("type", QVariant::fromValue(model->getAsset(balance.asset_type.instance.value)));
      newBalance->setProperty("amount", QVariant::fromValue(balance.balance.value));
      m_balances.append(newBalance);
      Q_EMIT balancesChanged();
   }
}
