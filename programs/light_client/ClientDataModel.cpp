#include "ClientDataModel.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

ChainDataModel::ChainDataModel( fc::thread& t, QObject* parent )
:QObject(parent),m_thread(&t){}

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



GrapheneApplication::GrapheneApplication( QObject* parent )
:QObject( parent ),m_thread("app")
{
   m_model = new ChainDataModel( m_thread, this );
}
GrapheneApplication::~GrapheneApplication()
{
}

bool GrapheneApplication::start( QString datadir, QString apiurl )
{
   if( !m_thread.is_current() )
   {
      m_done = m_thread.async( [=](){ return start( datadir, apiurl ); }  );
      return true;
   }
   try {
      auto con  = m_client.connect( apiurl.toStdString() );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);


   } catch ( const fc::exception& e )
   {

      return false;
   }
   return true;
}
