#include "ClientDataModel.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

using namespace graphene::app;

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
   connect( this, &GrapheneApplication::queueExecute,
            this, &GrapheneApplication::execute );

   m_model = new ChainDataModel( m_thread, this );

   start( "", "", "", "" );
}

GrapheneApplication::~GrapheneApplication()
{
}

void GrapheneApplication::setIsConnected( bool v )
{
   if( v != m_isConnected )
   {
      m_isConnected = v;
      Q_EMIT isConnectedChanged( m_isConnected );
   }
}

void GrapheneApplication::start( QString datadir, QString apiurl, QString user, QString pass )
{
   if( !m_thread.is_current() )
   {
      m_done = m_thread.async( [=](){ return start( datadir, apiurl, user, pass ); }  );
      return;
   }
   try {
      auto con  = m_client.connect( apiurl.toStdString() );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      auto remote_api = apic->get_remote_api< login_api >(1);

      if( !remote_api->login( user.toStdString(), pass.toStdString() ) )
      {
         // TODO: emit error
         return;
      }

      queueExecute( [=](){setIsConnected( true );} );
   } catch ( const fc::exception& e )
   {

   }
}
Q_SLOT void GrapheneApplication::execute( const std::function<void()>& func )const
{
   func();
}
