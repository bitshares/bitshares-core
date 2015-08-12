#include "BlockChain.hpp"

#include <graphene/app/application.hpp>

#include <fc/thread/thread.hpp>

#include <QThread>
#include <QTimer>
#include <QDebug>

BlockChain::BlockChain()
   : chainThread(fc::thread::current()),
     fcTaskScheduler(new QTimer(this)),
     grapheneApp(new graphene::app::application)
{
   fcTaskScheduler->setInterval(100);
   fcTaskScheduler->setSingleShot(false);
   connect(fcTaskScheduler, &QTimer::timeout, [] {fc::yield();});
   fcTaskScheduler->start();
}
