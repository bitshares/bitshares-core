#include "ClientDataModel.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

using namespace graphene::app;

ChainDataModel::ChainDataModel( fc::thread& t, QObject* parent )
:QObject(parent),m_thread(&t){}


Asset* ChainDataModel::getAsset(qint64 id)
{
   auto& by_id_idx = m_assets.get<::by_id>();
   auto itr = by_id_idx.find(id);
   if( itr == by_id_idx.end() )
   {
      auto tmp = new Asset;
      QQmlEngine::setObjectOwnership(tmp, QQmlEngine::CppOwnership);
      tmp->id = id; --m_account_query_num;
      tmp->symbol = QString::number( --m_account_query_num);
      auto result = m_assets.insert( tmp );
      assert( result.second );

      /** execute in app thread */
      m_thread->async( [this,id](){
         try {
           ilog( "look up symbol.." );
           auto result = m_db_api->get_assets( {asset_id_type(id)} );
           wdump((result));

           /** execute in main */
           Q_EMIT queueExecute( [this,result,id](){
              wlog( "process result" );
              auto& by_id_idx = this->m_assets.get<::by_id>();
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
                    [=]( Asset* a ){
                       a->setProperty("symbol", QString::fromStdString(result.front()->symbol) );
                       a->setProperty("precision", result.front()->precision );
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

Asset* ChainDataModel::getAsset(QString symbol)
{
   auto& by_symbol_idx = m_assets.get<by_symbol_name>();
   auto itr = by_symbol_idx.find(symbol);
   if( itr == by_symbol_idx.end() )
   {
      auto tmp = new Asset;
      QQmlEngine::setObjectOwnership(tmp, QQmlEngine::CppOwnership);
      tmp->id = --m_account_query_num;
      tmp->symbol = symbol;
      auto result = m_assets.insert( tmp );
      assert( result.second );

      /** execute in app thread */
      m_thread->async( [this,symbol](){
         try {
           ilog( "look up symbol.." );
           auto result = m_db_api->lookup_asset_symbols( {symbol.toStdString()} );
           /** execute in main */
           Q_EMIT queueExecute( [this,result,symbol](){
              wlog( "process result" );
              auto& by_symbol_idx = this->m_assets.get<by_symbol_name>();
              auto itr = by_symbol_idx.find(symbol);
              assert( itr != by_symbol_idx.end() );

              if( result.size() == 0 || !result.front() )
              {
                  elog( "delete later" );
                  (*itr)->deleteLater();
                  by_symbol_idx.erase( itr );
              }
              else
              {
                 by_symbol_idx.modify( itr,
                    [=]( Asset* a ){
                       a->setProperty("id", ObjectId(result.front()->id.instance()));
                       a->setProperty("precision", result.front()->precision );
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

Account* ChainDataModel::getAccount(ObjectId id)
{
   auto& by_id_idx = m_accounts.get<::by_id>();
   auto itr = by_id_idx.find(id);
   if( itr == by_id_idx.end() )
   {
      auto tmp = new Account;
      QQmlEngine::setObjectOwnership(tmp, QQmlEngine::CppOwnership);
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
      QQmlEngine::setObjectOwnership(tmp, QQmlEngine::CppOwnership);
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
                       a->setProperty("id", ObjectId(result.front()->id.instance()));
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
      m_connectionClosed = con->closed.connect([this]{queueExecute([this]{setIsConnected(false);});});
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
