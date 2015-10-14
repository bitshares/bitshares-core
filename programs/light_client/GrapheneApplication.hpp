/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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

