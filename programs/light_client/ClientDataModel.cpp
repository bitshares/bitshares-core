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
   acct->setProperty("accountName", "LOADING");
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
                [=]( Account* a ){ a->setProperty("accountName", name ); }
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
         edump((e.to_detail_string()));
         Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
      }
   });

   return acct;
}

Account* ChainDataModel::getAccount(QString name)
{
   {
      auto itr = m_accounts.get<by_account_name>().begin();
      while( itr != m_accounts.get<by_account_name>().end() )
      {
         edump(( (*itr)->getAccountName().toStdString()));
         if((*itr)->name() == name.toStdString() )
            wlog( "THEY ARE THE SAME" );
         ++itr;
      }
   }
   auto& by_name_index = m_accounts.get<by_account_name>();
   {
      for( auto itr = by_name_index.begin(); itr != by_name_index.end(); ++itr )
         wdump(( (*itr)->getAccountName().toStdString()));
   }
   auto itr = by_name_index.find(name.toStdString());
   idump((itr != by_name_index.end()));
   if( itr != by_name_index.end() )
   {
      return *itr;
   }

   auto acct = new Account(this);
   acct->setProperty("id", --m_account_query_num );
   acct->setProperty("accountName", name);
   auto insert_result = m_accounts.insert( acct );
   edump( (insert_result.second) );
   edump( (int64_t(*insert_result.first))(int64_t(acct)) );
   auto itr2 = by_name_index.find(name.toStdString());
   assert( itr2 != by_name_index.end() );

   /** execute in app thread */
   m_thread->async( [=](){
      try {
        ilog( "look up names.." );
        auto result = m_db_api->lookup_account_names( {name.toStdString()} );
        idump((result));
        if( result.size() && result.front().valid() )
        {
          /** execute in main */
          Q_EMIT queueExecute( [=](){
             this->m_accounts.modify( insert_result.first,
                [=]( Account* a ){
                   ilog( "setting id ${i}", ("i",result.front()->id.instance()) );
                   idump((int64_t(a)));
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
             this->m_accounts.erase( insert_result.first );
          });
        }
      }
      catch ( const fc::exception& e )
      {
         edump((e.to_detail_string()));
         Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
      }
   });

   idump((int64_t(acct)));
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
      m_client = std::make_shared<fc::http::websocket_client>();
      ilog( "connecting...${s}", ("s",apiurl.toStdString()) );
      auto con  = m_client->connect( apiurl.toStdString() );
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      auto remote_api = apic->get_remote_api< login_api >(1);
      auto db_api = apic->get_remote_api< database_api >(0);
      if( !remote_api->login( user.toStdString(), pass.toStdString() ) )
      {
         elog( "login failed" );
         Q_EMIT loginFailed();
         return;
      }

      ilog( "connecting..." );
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
