#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include <fc/network/http/websocket.hpp>
#include <fc/thread/thread.hpp>
#include <graphene/app/api.hpp>

#include <QObject>
#include <QQmlListProperty>

using boost::multi_index_container;
using namespace boost::multi_index;

Q_DECLARE_METATYPE(std::function<void()>)


class Asset : public QObject {
   Q_OBJECT

   Q_PROPERTY(QString symbol MEMBER symbol)
   Q_PROPERTY(qint64 id MEMBER id)
   Q_PROPERTY(quint8 precision MEMBER precision)

   QString symbol;
   qint64 id;
   quint8 precision;
};

class Balance : public QObject {
   Q_OBJECT

   Q_PROPERTY(Asset* type MEMBER type)
   Q_PROPERTY(qint64 amount MEMBER amount)
   Q_PROPERTY(qint64 id MEMBER id)

   Asset* type;
   qint64 amount;
   qint64 id;
};

class Account : public QObject {
   Q_OBJECT

   Q_PROPERTY(QString name MEMBER name NOTIFY nameChanged)
   Q_PROPERTY(qint64 id MEMBER id NOTIFY idChanged)
   Q_PROPERTY(QQmlListProperty<Balance> balances READ balances)

   QList<Balance*> m_balances;

public:
 //  Account(QObject* parent = nullptr)
 //     : QObject(parent){}

   const QString& getName()const { return name; }
   qint64        getId()const   { return id;   }

   QQmlListProperty<Balance> balances();

   QString name;
   qint64 id;

signals:
   void nameChanged();
   void idChanged();
};

struct by_id;
struct by_account_name;
/**
 * @ingroup object_index
 */
typedef multi_index_container<
   Account*,
   indexed_by<
      hashed_unique< tag<by_id>,  const_mem_fun<Account, qint64, &Account::getId > >,
      ordered_unique< tag<by_account_name>, const_mem_fun<Account, const QString&, &Account::getName> >
   >
> account_multi_index_type;




class ChainDataModel : public QObject {
   Q_OBJECT

public:
   Q_INVOKABLE Account* getAccount(qint64 id);
   Q_INVOKABLE Account* getAccount(QString name);

   ChainDataModel(){}
   ChainDataModel( fc::thread& t, QObject* parent = nullptr );

   void setDatabaseAPI( fc::api<graphene::app::database_api> dbapi ){ m_db_api = dbapi; }

Q_SIGNALS:
   void queueExecute( const std::function<void()>& );
   void exceptionThrown( QString message );

private:
   fc::thread*                           m_thread = nullptr;
   std::string                           m_api_url;
   fc::api<graphene::app::database_api>  m_db_api;

   qint64                                m_account_query_num = -1;
   account_multi_index_type              m_accounts;
};





class GrapheneApplication : public QObject {
   Q_OBJECT

   Q_PROPERTY(ChainDataModel* model READ model CONSTANT)
   Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)


   fc::thread                  m_thread;
   ChainDataModel*             m_model       = nullptr;
   bool                        m_isConnected = false;

   std::shared_ptr<fc::http::websocket_client>  m_client;
   fc::future<void>                        m_done;

   void setIsConnected( bool v );

   Q_SLOT void execute( const std::function<void()>& )const;
public:
   GrapheneApplication( QObject* parent = nullptr );
   ~GrapheneApplication();

   ChainDataModel* model() const
   {
      return m_model;
   }

   Q_INVOKABLE void start(QString apiUrl,
                           QString user,
                           QString pass );

   bool isConnected() const
   {
      return m_isConnected;
   }

Q_SIGNALS:
   void exceptionThrown( QString message );
   void loginFailed();
   void isConnectedChanged(bool isConnected);
   void queueExecute( const std::function<void()>& );
};
