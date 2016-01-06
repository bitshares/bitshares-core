/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <fc/thread/thread.hpp>

#include <boost/signals2.hpp>

#include <QObject>


namespace fc { namespace http {
class websocket_client;
}}

class ChainDataModel;
class OperationBuilder;
class OperationBase;
class Transaction;
class Wallet;
class GrapheneApplication : public QObject {
   Q_OBJECT

   Q_PROPERTY(ChainDataModel* model READ model CONSTANT)
   Q_PROPERTY(OperationBuilder* operationBuilder READ operationBuilder CONSTANT)
   Q_PROPERTY(Wallet* wallet READ wallet CONSTANT)
   Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)


   fc::thread        m_thread;
   ChainDataModel*   m_model = nullptr;
   Wallet*           m_wallet = nullptr;
   OperationBuilder* m_operationBuilder = nullptr;
   bool              m_isConnected = false;

   boost::signals2::scoped_connection m_connectionClosed;

   std::shared_ptr<fc::http::websocket_client> m_client;
   fc::future<void> m_done;

   void setIsConnected(bool v);

protected Q_SLOTS:
   void execute(const std::function<void()>&)const;

public:
   GrapheneApplication(QObject* parent = nullptr);
   ~GrapheneApplication();

   Wallet* wallet()const { return m_wallet; }

   ChainDataModel* model() const {
      return m_model;
   }
   OperationBuilder* operationBuilder() const {
      return m_operationBuilder;
   }

   Q_INVOKABLE void start(QString apiUrl,
                          QString user,
                          QString pass);

   bool isConnected() const
   {
      return m_isConnected;
   }

   Q_INVOKABLE static QString defaultDataPath();

   /// Convenience method to get a Transaction in QML. Caller takes ownership of the new Transaction.
   Q_INVOKABLE Transaction* createTransaction() const;
   Q_INVOKABLE void signTransaction(Transaction* transaction) const;

Q_SIGNALS:
   void exceptionThrown(QString message);
   void loginFailed();
   void isConnectedChanged(bool isConnected);
   void queueExecute(const std::function<void()>&);
};

