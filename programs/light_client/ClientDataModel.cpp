#include "ClientDataModel.hpp"


Account* ChainDataModel::getAccount(quint64 id) const
{
   auto acct = new Account;
   acct->setProperty("id", id);
   acct->setProperty("name", "joe");
   return acct;
}

Account*ChainDataModel::getAccount(QString name) const
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



void GrapheneApplication::initialize( QString datadir, QString apiurl )
{
}
