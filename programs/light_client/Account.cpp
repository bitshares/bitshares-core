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
