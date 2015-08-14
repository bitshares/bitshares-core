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
     fcTaskScheduler(new QTimer(this)),
     grapheneApp(new graphene::app::application)
{
   fcTaskScheduler->setInterval(100);
   fcTaskScheduler->setSingleShot(false);
   connect(fcTaskScheduler, &QTimer::timeout, [] {fc::yield();});
   fcTaskScheduler->start();
}

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
         map.insert({"rpc-endpoint",boost::program_options::variable_value(std::string("127.0.0.1:8090"), false)});
         map.insert({"seed-node",boost::program_options::variable_value(std::vector<std::string>{("104.200.28.117:61705")}, false)});
         grapheneApp->initialize(dataDir.toStdString(), map);
         grapheneApp->initialize_plugins(map);
         grapheneApp->startup();
         grapheneApp->startup_plugins();
      } catch (const fc::exception& e) {
         elog("Crap went wrong: ${e}", ("e", e.to_detail_string()));
      }
      QMetaObject::invokeMethod(this, "started");
   });
}
