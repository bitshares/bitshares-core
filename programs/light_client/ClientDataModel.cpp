#include "ClientDataModel.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

using namespace graphene::app;

ChainDataModel::ChainDataModel( fc::thread& t, QObject* parent )
:QObject(parent),m_thread(&t){}

Account* ChainDataModel::getAccount(qint64 id)
{
   auto& by_id_idx = m_accounts.get<::by_id>();
   auto itr = by_id_idx.find(id);
   if( itr == by_id_idx.end() )
   {
      auto tmp = new Account;
      tmp->id = id; --m_account_query_num;
      tmp->name = QString::number( --m_account_query_num);
      auto result = m_accounts.insert( tmp );
      assert( result.second );

      /** execute in app thread */
      m_thread->async( [this,id](){
         try {
           ilog( "look up names.." );
           auto result = m_db_api->get_accounts( {account_id_type(id)} );
           /** execute in main */
           Q_EMIT queueExecute( [this,result,id](){
              wlog( "process result" );
              auto& by_id_idx = this->m_accounts.get<::by_id>();
              auto itr = by_id_idx.find(id);
              assert( itr != by_id_idx.end() );

              if( result.size() == 0 || !result.front() )
              {
                  elog( "delete later" );
                  (*itr)->deleteLater();
                  by_id_idx.erase( itr );
              }
              else
              {
                 by_id_idx.modify( itr,
                    [=]( Account* a ){
                       a->setProperty("name", QString::fromStdString(result.front()->name) );
                    }
                 );
              }
           });
         } 
         catch ( const fc::exception& e )
         {
            Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
         }
       });
      return *result.first;
   }
   return *itr;
}

Account* ChainDataModel::getAccount(QString name)
{
   auto& by_name_idx = m_accounts.get<by_account_name>();
   auto itr = by_name_idx.find(name);
   if( itr == by_name_idx.end() )
   {
      auto tmp = new Account;
      tmp->id = --m_account_query_num;
      tmp->name = name;
      auto result = m_accounts.insert( tmp );
      assert( result.second );

      /** execute in app thread */
      m_thread->async( [this,name](){
         try {
           ilog( "look up names.." );
           auto result = m_db_api->lookup_account_names( {name.toStdString()} );
           /** execute in main */
           Q_EMIT queueExecute( [this,result,name](){
              wlog( "process result" );
              auto& by_name_idx = this->m_accounts.get<by_account_name>();
              auto itr = by_name_idx.find(name);
              assert( itr != by_name_idx.end() );

              if( result.size() == 0 || !result.front() )
              {
                  elog( "delete later" );
                  (*itr)->deleteLater();
                  by_name_idx.erase( itr );
              }
              else
              {
                 by_name_idx.modify( itr,
                    [=]( Account* a ){
                       a->setProperty("id", result.front()->id.instance() );
                    }
                 );
              }
           });
         } 
         catch ( const fc::exception& e )
         {
            Q_EMIT exceptionThrown( QString::fromStdString(e.to_string()) );
         }
       });
      return *result.first;
   }
   return *itr;
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
