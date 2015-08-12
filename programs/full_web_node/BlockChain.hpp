#pragma once

#include <QObject>

class QTimer;
class QThread;
namespace fc { class thread; }
namespace graphene { namespace app { class application; } }
class BlockChain : public QObject {
   Q_OBJECT

   fc::thread& chainThread;
   QTimer* fcTaskScheduler;
   graphene::app::application* grapheneApp;

public:
   BlockChain();
   virtual ~BlockChain(){}
};
