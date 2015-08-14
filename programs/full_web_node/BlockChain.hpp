#pragma once

#include <fc/thread/future.hpp>

#include <QObject>

class QTimer;
class QThread;
namespace fc { class thread; }
namespace graphene { namespace app { class application; } }
class BlockChain : public QObject {
   Q_OBJECT

   fc::thread* chainThread;
   QTimer* fcTaskScheduler;
   graphene::app::application* grapheneApp;
   fc::future<void> startFuture;

public:
   BlockChain();
   virtual ~BlockChain();

public Q_SLOTS:
   void start();

Q_SIGNALS:
   void started();
};
