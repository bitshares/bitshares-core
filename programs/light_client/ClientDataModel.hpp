#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include <fc/network/http/websocket.hpp>
#include <fc/thread/thread.hpp>

#include <QObject>
#include <QQmlListProperty>




class Asset : public QObject {
   Q_OBJECT

   Q_PROPERTY(QString symbol MEMBER symbol)
   Q_PROPERTY(quint64 id MEMBER id)
   Q_PROPERTY(quint8 precision MEMBER precision)

   QString symbol;
   quint64 id;
   quint8 precision;
};

class Balance : public QObject {
   Q_OBJECT

   Q_PROPERTY(Asset* type MEMBER type)
   Q_PROPERTY(qint64 amount MEMBER amount)
   Q_PROPERTY(quint64 id MEMBER id)

   Asset* type;
   qint64 amount;
   quint64 id;
};

class Account : public QObject {
   Q_OBJECT

   Q_PROPERTY(QString name MEMBER name)
   Q_PROPERTY(quint64 id MEMBER id)
   Q_PROPERTY(QQmlListProperty<Balance> balances READ balances)

   QString name;
   quint64 id;
   QList<Balance*> m_balances;

public:
   Account(QObject* parent = nullptr)
      : QObject(parent){}

   QQmlListProperty<Balance> balances();
};

class ChainDataModel : public QObject {
   Q_OBJECT

public:
   Q_INVOKABLE Account* getAccount(quint64 id)const;
   Q_INVOKABLE Account* getAccount(QString name)const;

   ChainDataModel(){}
   ChainDataModel( fc::thread& t, QObject* parent = nullptr );

private:
   fc::thread*                 m_thread = nullptr;
   std::string                 m_api_url;
};

class GrapheneApplication : public QObject {
   Q_OBJECT

   Q_PROPERTY(ChainDataModel* model READ model CONSTANT)
   Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)


   fc::thread                  m_thread;
   ChainDataModel*             m_model       = nullptr;
   bool                        m_isConnected = false;

   fc::http::websocket_client  m_client;
   fc::future<bool>            m_done;
public:
   GrapheneApplication( QObject* parent = nullptr );
   ~GrapheneApplication();

   ChainDataModel* model() const
   {
      return m_model;
   }

   Q_INVOKABLE bool start(QString dataDirectory, QString apiUrl);

   bool isConnected() const
   {
      return m_isConnected;
   }

signals:
   void isConnectedChanged(bool isConnected);
};
