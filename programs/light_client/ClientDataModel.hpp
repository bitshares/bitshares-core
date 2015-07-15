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

#include <QtQml>
#include <QObject>
#include <QQmlListProperty>

using boost::multi_index_container;
using namespace boost::multi_index;

using ObjectId = qint64;
Q_DECLARE_METATYPE(ObjectId)

Q_DECLARE_METATYPE(std::function<void()>)

class GrapheneObject : public QObject
{
   Q_OBJECT
   Q_PROPERTY(ObjectId id MEMBER m_id READ id NOTIFY idChanged)

   ObjectId m_id;

public:
   GrapheneObject(ObjectId id = -1, QObject* parent = nullptr)
      : QObject(parent), m_id(id)
   {}

   ObjectId id() const {
      return m_id;
   }

Q_SIGNALS:
   void idChanged();
};
class Crypto {
   Q_GADGET

public:
   Q_INVOKABLE QString sha256(QByteArray data) {
      return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
   }
};
QML_DECLARE_TYPE(Crypto)


class Asset : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(QString symbol MEMBER m_symbol READ symbol NOTIFY symbolChanged)
   Q_PROPERTY(quint32 precision MEMBER m_precision NOTIFY precisionChanged)

   QString m_symbol;
   quint32 m_precision;

public:
   Asset(ObjectId id = -1, QString symbol = QString(), quint32 precision = 0, QObject* parent = nullptr)
      : GrapheneObject(id, parent), m_symbol(symbol), m_precision(precision)
   {}

   QString symbol() const {
      return m_symbol;
   }

   quint64 precisionPower() const {
      quint64 power = 1;
      for (int i = 0; i < m_precision; ++i)
         power *= 10;
      return power;
   }

Q_SIGNALS:
   void symbolChanged();
   void precisionChanged();
};

struct by_id;
struct by_symbol_name;
typedef multi_index_container<
   Asset*,
   indexed_by<
      hashed_unique< tag<by_id>, const_mem_fun<GrapheneObject, ObjectId, &GrapheneObject::id > >,
      ordered_unique< tag<by_symbol_name>, const_mem_fun<Asset, QString, &Asset::symbol> >
   >
> asset_multi_index_type;

class Balance : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(Asset* type MEMBER type NOTIFY typeChanged)
   Q_PROPERTY(qint64 amount MEMBER amount NOTIFY amountChanged)

   Asset* type;
   qint64 amount;

public:
   // This ultimately needs to be replaced with a string equivalent
   Q_INVOKABLE qreal amountReal() const {
      return amount / qreal(type->precisionPower());
   }

Q_SIGNALS:
   void typeChanged();
   void amountChanged();
};

class Account : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(QString name MEMBER m_name READ name NOTIFY nameChanged)
   Q_PROPERTY(QQmlListProperty<Balance> balances READ balances NOTIFY balancesChanged)

   QString m_name;
   QList<Balance*> m_balances;

public:
   Account(ObjectId id = -1, QString name = QString(), QObject* parent = nullptr)
      : GrapheneObject(id, parent), m_name(name)
   {}

   QString name()const { return m_name; }
   QQmlListProperty<Balance> balances();

Q_SIGNALS:
   void nameChanged();
   void balancesChanged();
};

struct by_account_name;
typedef multi_index_container<
   Account*,
   indexed_by<
      hashed_unique< tag<by_id>, const_mem_fun<GrapheneObject, ObjectId, &GrapheneObject::id > >,
      ordered_unique< tag<by_account_name>, const_mem_fun<Account, QString, &Account::name> >
   >
> account_multi_index_type;

class ChainDataModel : public QObject {
   Q_OBJECT

public:
   Q_INVOKABLE Account* getAccount(ObjectId id);
   Q_INVOKABLE Account* getAccount(QString name);
   Q_INVOKABLE Asset*   getAsset(ObjectId id);
   Q_INVOKABLE Asset*   getAsset(QString symbol);

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

   ObjectId                                m_account_query_num = -1;
   account_multi_index_type              m_accounts;
   asset_multi_index_type                m_assets;
};

class GrapheneApplication : public QObject {
   Q_OBJECT

   Q_PROPERTY(ChainDataModel* model READ model CONSTANT)
   Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)


   fc::thread                  m_thread;
   ChainDataModel*             m_model       = nullptr;
   bool                        m_isConnected = false;

   boost::signals2::scoped_connection m_connectionClosed;

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
