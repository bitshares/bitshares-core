#pragma once

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
};

class GrapheneApplication : public QObject {
   Q_OBJECT

   Q_PROPERTY(ChainDataModel* model READ model CONSTANT)
   Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)

   ChainDataModel* m_model;
   bool m_isConnected;

public:
   ChainDataModel* model() const
   {
      return m_model;
   }

   Q_INVOKABLE void initialize(QString dataDirectory, QString apiUrl);

   bool isConnected() const
   {
      return m_isConnected;
   }

signals:
   void isConnectedChanged(bool isConnected);
};
