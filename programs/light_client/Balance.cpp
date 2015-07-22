#include "Balance.hpp"
#include "Asset.hpp"

#include <graphene/chain/account_object.hpp>

qreal Balance::amountReal() const {
   return amount / qreal(m_type->precisionPower());
}

void Balance::update(const graphene::chain::account_balance_object& update)
{
   if (update.balance != amount) {
      amount = update.balance.value;
      emit amountChanged();
   }
}
