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
         grapheneApp->register_plugin<graphene::account_history::account_history_plugin>();
         grapheneApp->register_plugin<graphene::market_history::market_history_plugin>();

         QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
         QDir(dataDir).mkpath(".");
         boost::program_options::variables_map map;
         map.insert({"rpc-endpoint",boost::program_options::variable_value(std::string("127.0.0.1:8091"), false)});
         map.insert({"seed-node",boost::program_options::variable_value(std::vector<std::string>{("104.200.28.117:61705")}, false)});
         grapheneApp->initialize(dataDir.toStdString(), map);
         grapheneApp->initialize_plugins(map);
         grapheneApp->startup();
         grapheneApp->startup_plugins();

         graphene::app::api_access_info webPermissions;
         auto passHash = fc::sha256::hash(webPassword.toStdString());
         webPermissions.password_hash_b64 = fc::base64_encode(passHash.data(), passHash.data_size());
         webPermissions.password_salt_b64 = fc::base64_encode("");
         webPermissions.allowed_apis = {"database_api", "network_broadcast_api", "network_node_api", "history_api"};
         grapheneApp->set_api_access_info(webUsername.toStdString(), std::move(webPermissions));
      } catch (const fc::exception& e) {
         elog("Crap went wrong: ${e}", ("e", e.to_detail_string()));
      }
      QMetaObject::invokeMethod(this, "started");
   });
}
