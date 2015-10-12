/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "Balance.hpp"
#include "ChainDataModel.hpp"
#include "Wallet.hpp"

#include <graphene/chain/account_object.hpp>

void Account::setAccountObject(const graphene::chain::account_object& obj)
{
   auto oldName = m_account.name;
   auto oldMemoKey = memoKey();

   m_account = obj;
   if (oldName != m_account.name)
      Q_EMIT nameChanged();
   if (oldMemoKey != memoKey())
      Q_EMIT memoKeyChanged();

   if (!m_loaded) {
      m_loaded = true;
      Q_EMIT loaded();
      qDebug() << name() << "loaded.";
   }
}

QString Account::memoKey() const
{
   return toQString(m_account.options.memo_key);
}

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

double Account::getActiveControl(Wallet* w, int depth)const
{
   if (depth >= GRAPHENE_MAX_SIG_CHECK_DEPTH) return 0;
   if (m_account.active.num_auths() == 0) return 0;
   if (m_account.active.weight_threshold == 0) return 0;

   uint64_t weight = 0;
   for (auto& key : m_account.active.key_auths)
   {
      if (w->hasPrivateKey(toQString(key.first))) weight += key.second;
   }

   ChainDataModel* model = qobject_cast<ChainDataModel*>(parent());
   for (auto& acnt : m_account.active.account_auths)
   {
      Account* account = model->getAccount(acnt.first.instance.value);
      if (!account->m_loaded) {
         QEventLoop el;
         connect(account, &Account::loaded, &el, &QEventLoop::quit);
         QTimer::singleShot(1000, &el, SLOT(quit()));
         el.exec();
         if (!account->m_loaded)
            // We don't have this account loaded yet... Oh well, move along
            continue;
      }
      if (account->getActiveControl(w, depth + 1) >= 1.0)
         weight += acnt.second;
   }

   return double(weight) / double(m_account.active.weight_threshold);
}

double Account::getOwnerControl(Wallet* w)const
{
   if (m_account.owner.num_auths() == 0) return 0;
   if (m_account.owner.weight_threshold == 0) return 0;
   uint64_t weight = 0;
   for (auto& key : m_account.owner.key_auths)
   {
      if (w->hasPrivateKey(toQString(key.first))) weight += key.second;
   }

   ChainDataModel* model = qobject_cast<ChainDataModel*>(parent());
   for (auto& acnt : m_account.owner.account_auths)
   {
      Account* account = model->getAccount(acnt.first.instance.value);
      if (!account->m_loaded)
         // We don't have this account loaded yet... Oh well, move along
         continue;
      if (account->getActiveControl(w) >= 1.0)
         weight += acnt.second;
   }

   return double(weight) / double(m_account.owner.weight_threshold);
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
