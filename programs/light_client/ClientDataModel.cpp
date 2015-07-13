#include "ClientDataModel.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

using namespace graphene::app;

ChainDataModel::ChainDataModel( fc::thread& t, QObject* parent )
:QObject(parent),m_thread(&t){}

Account* ChainDataModel::getAccount(qint64 id) 
{
   auto itr = m_accounts.find( id );
   if( itr != m_accounts.end() )
      return *itr;

   auto acct = new Account(this);
   acct->setProperty("id", id);
   acct->setProperty("name", "LOADING");
   auto insert_result = m_accounts.insert( acct );

   /** execute in app thread */
   m_thread->async( [=](){ 
      try {
        auto result = m_db_api->get_accounts( {account_id_type(id)} );
        if( result.size() && result.front().valid() )
        {
          QString name = QString::fromStdString( result.front()->name );
          /** execute in main */
          Q_EMIT queueExecute( [=](){
             this->m_accounts.modify( insert_result.first, 
                [=]( Account* a ){ a->setProperty("name", name ); }
             );
          });
        }
        else
        {
          /** execute in main */
          Q_EMIT queueExecute( [=](){
             acct->deleteLater();
             m_accounts.erase( insert_result.first );
          });
        }
      } 
      catch ( const fc::exception& e )
      {
         Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
      }
   });

   return acct;
}

Account* ChainDataModel::getAccount(QString name) 
{
   auto itr = m_accounts.get<::by_name>().find(name);
   if( itr != m_accounts.get<::by_name>().end() )
   {
      return *itr;
   }


   auto acct = new Account(this);
   acct->setProperty("id", --m_account_query_num );
   acct->setProperty("name", name);
   auto insert_result = m_accounts.insert( acct );

   /** execute in app thread */
   m_thread->async( [=](){ 
      try {
        auto result = m_db_api->lookup_account_names( {name.toStdString()} );
        if( result.size() && result.front().valid() )
        {
          /** execute in main */
          Q_EMIT queueExecute( [=](){
             this->m_accounts.modify( insert_result.first, 
                [=]( Account* a ){ 
                   a->setProperty("id", result.front()->id.instance() ); 
                }
             );
          });
        }
        else
        {
          /** execute in main */
          Q_EMIT queueExecute( [=](){
             acct->deleteLater();
             m_accounts.erase( insert_result.first );
          });
        }
      } 
      catch ( const fc::exception& e )
      {
         Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
      }
   });

   return nullptr;
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

   connect( m_model, &ChainDataModel::queueExecute,
            this, &GrapheneApplication::execute );

   connect( m_model, &ChainDataModel::exceptionThrown,
            this, &GrapheneApplication::exceptionThrown );
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

void GrapheneApplication::start( QString apiurl, QString user, QString pass )
{
   if( !m_thread.is_current() )
   {
      m_done = m_thread.async( [=](){ return start( apiurl, user, pass ); }  );
      return;
   }
   try {
      auto con  = m_client.connect( apiurl.toStdString() );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      auto remote_api = apic->get_remote_api< login_api >(1);
      auto db_api = apic->get_remote_api< database_api >(0);

      if( !remote_api->login( user.toStdString(), pass.toStdString() ) )
      {
         Q_EMIT loginFailed();
         return;
      }

      queueExecute( [=](){
         m_model->setDatabaseAPI( db_api );
      });

      queueExecute( [=](){setIsConnected( true );} );
   } catch ( const fc::exception& e )
   {
      Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
   }
}
Q_SLOT void GrapheneApplication::execute( const std::function<void()>& func )const
{
   func();
}
