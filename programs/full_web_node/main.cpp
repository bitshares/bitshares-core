#include "BlockChain.hpp"

#include <fc/thread/thread.hpp>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtWebEngine>
#include <QtQml>

int main(int argc, char *argv[])
{
   fc::thread::current().set_name("main");
   QGuiApplication app(argc, argv);
   app.setApplicationName("Graphene Client");
   app.setApplicationDisplayName(app.applicationName());
   app.setOrganizationDomain("cryptonomex.org");
   app.setOrganizationName("Cryptonomex, Inc.");

   QtWebEngine::initialize();
   qmlRegisterType<BlockChain>("Graphene.FullNode", 1, 0, "BlockChain");
   QQmlApplicationEngine engine;
   engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

   return app.exec();
}
