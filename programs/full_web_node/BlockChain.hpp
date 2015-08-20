#pragma once

#include <fc/thread/future.hpp>

#include <QObject>

class QTimer;
class QThread;
namespace fc { class thread; }
namespace graphene { namespace app { class application; } }
class BlockChain : public QObject {
   Q_OBJECT
   Q_PROPERTY(QString webUsername MEMBER webUsername CONSTANT)
   Q_PROPERTY(QString webPassword MEMBER webPassword CONSTANT)

   fc::thread* chainThread;
   graphene::app::application* grapheneApp;
   fc::future<void> startFuture;
   QString webUsername;
   QString webPassword;

public:
   BlockChain();
   virtual ~BlockChain();

public Q_SLOTS:
   void start();

Q_SIGNALS:
   void started();
};
