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
#include "BlockChain.hpp"

#include <graphene/app/application.hpp>
#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>

#include <fc/thread/thread.hpp>

#include <boost/program_options.hpp>

#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>

BlockChain::BlockChain()
   : chainThread(new fc::thread("chainThread")),
     grapheneApp(new graphene::app::application),
     webUsername(QStringLiteral("webui")),
     webPassword(QString::fromStdString(fc::sha256::hash(fc::ecc::private_key::generate())))
{}

BlockChain::~BlockChain() {
   startFuture.cancel_and_wait(__FUNCTION__);
   chainThread->async([this] {
      grapheneApp->shutdown_plugins();
      delete grapheneApp;
   }).wait();
   delete chainThread;
}

void BlockChain::start()
{
   startFuture = chainThread->async([this] {
      try {
         QSettings settings;
         rpcEndpoint = settings.value( "rpc-endpoint", "127.0.0.1:8090" ).value<QString>();
         auto seed_node    = settings.value( "seed-node", "104.236.51.238:2005" ).value<QString>().toStdString();
         grapheneApp->register_plugin<graphene::account_history::account_history_plugin>();
         grapheneApp->register_plugin<graphene::market_history::market_history_plugin>();

         QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
         QDir(dataDir).mkpath(".");
         idump((dataDir.toStdString()));
         boost::program_options::variables_map map;
         map.insert({"rpc-endpoint",boost::program_options::variable_value(rpcEndpoint.toStdString(), false)});
         map.insert({"seed-node",boost::program_options::variable_value(std::vector<std::string>{(seed_node)}, false)});
         grapheneApp->initialize(dataDir.toStdString(), map);
         grapheneApp->initialize_plugins(map);
         grapheneApp->startup();
         grapheneApp->startup_plugins();

         graphene::app::api_access_info webPermissions;
         auto passHash = fc::sha256::hash(webPassword.toStdString());
         webPermissions.password_hash_b64 = fc::base64_encode(passHash.data(), passHash.data_size());
         webPermissions.password_salt_b64 = fc::base64_encode("");
         webPermissions.allowed_apis = {"database_api", "network_broadcast_api", "network_node_api", "history_api"};
         grapheneApp->set_api_access_info(webUsername.toStdString(), std::move(webPermissions) );
      } catch (const fc::exception& e) {
         elog("Crap went wrong: ${e}", ("e", e.to_detail_string()));
      }
      QMetaObject::invokeMethod(this, "started");
   });
}
