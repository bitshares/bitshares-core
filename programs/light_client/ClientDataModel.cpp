#include "ClientDataModel.hpp"


Account* ClientDataModel::getAccount(quint64 id) const
{
   auto acct = new Account;
   acct->setProperty("id", id);
   acct->setProperty("name", "joe");
   return acct;
}

Account*ClientDataModel::getAccount(QString name) const
{
   auto acct = new Account;
   acct->setProperty("id", 800);
   acct->setProperty("name", name);
   return acct;
}

QQmlListProperty<Balance> Account::balances()
{
   return QQmlListProperty<Balance>(this, m_balances);
}
