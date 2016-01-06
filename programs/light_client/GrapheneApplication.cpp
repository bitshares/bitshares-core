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
#include "GrapheneApplication.hpp"
#include "ChainDataModel.hpp"
#include "Wallet.hpp"
#include "Operations.hpp"
#include "Transaction.hpp"

#include <graphene/app/api.hpp>

#include <fc/rpc/websocket_api.hpp>

#include <QStandardPaths>

using graphene::app::login_api;
using graphene::app::database_api;

GrapheneApplication::GrapheneApplication(QObject* parent)
:QObject(parent),m_thread("app")
{
   connect(this, &GrapheneApplication::queueExecute,
           this, &GrapheneApplication::execute);

   m_model = new ChainDataModel(m_thread, this);
   m_operationBuilder = new OperationBuilder(*m_model, this);
   m_wallet = new Wallet(this);

   connect(m_model, &ChainDataModel::queueExecute,
           this, &GrapheneApplication::execute);

   connect(m_model, &ChainDataModel::exceptionThrown,
           this, &GrapheneApplication::exceptionThrown);
}

GrapheneApplication::~GrapheneApplication()
{
}

void GrapheneApplication::setIsConnected(bool v)
{
   if (v != m_isConnected)
   {
      m_isConnected = v;
      Q_EMIT isConnectedChanged(m_isConnected);
   }
}

void GrapheneApplication::start(QString apiurl, QString user, QString pass)
{
   if (!m_thread.is_current())
   {
      m_done = m_thread.async([=](){ return start(apiurl, user, pass); });
      return;
   }
   try {
      m_client = std::make_shared<fc::http::websocket_client>();
      ilog("connecting...${s}", ("s",apiurl.toStdString()));
      auto con = m_client->connect(apiurl.toStdString());
      m_connectionClosed = con->closed.connect([this]{queueExecute([this]{setIsConnected(false);});});
      auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);
      auto remote_api = apic->get_remote_api<login_api>(1);
      auto db_api = apic->get_remote_api<database_api>(0);
      if (!remote_api->login(user.toStdString(), pass.toStdString()))
      {
         elog("login failed");
         Q_EMIT loginFailed();
         return;
      }
      auto net_api = remote_api->network_broadcast();

      ilog("connecting...");
      queueExecute([=](){
         m_model->setDatabaseAPI(db_api);
         m_model->setNetworkAPI(net_api);
      });

      queueExecute([=](){ setIsConnected(true); });
   } catch (const fc::exception& e)
   {
      Q_EMIT exceptionThrown(QString::fromStdString(e.to_string()));
   }
}

QString GrapheneApplication::defaultDataPath()
{
   return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

Transaction* GrapheneApplication::createTransaction() const
{
   return new Transaction;
}

void GrapheneApplication::signTransaction(Transaction* transaction) const
{
   if (transaction == nullptr) return;

   auto getActiveAuth = [this](graphene::chain::account_id_type id) {
      return &model()->getAccount(id.instance.value)->accountObject().active;
   };
   auto getOwnerAuth = [this](graphene::chain::account_id_type id) {
      return &model()->getAccount(id.instance.value)->accountObject().owner;
   };

   auto& chainId = model()->chain_properties().chain_id;
   auto& trx = transaction->internalTransaction();
   trx.set_reference_block(model()->dynamic_global_properties().head_block_id);
   flat_set<public_key_type> pubKeys = wallet()->getAvailablePrivateKeys();
   auto requiredKeys = trx.get_required_signatures(chainId, pubKeys, getActiveAuth, getOwnerAuth);
   trx.signatures = wallet()->signDigest(trx.digest(), requiredKeys);
   idump((trx));
}


Q_SLOT void GrapheneApplication::execute(const std::function<void()>& func)const
{
   func();
}


