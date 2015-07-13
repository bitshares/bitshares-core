#include <QApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

#include "ClientDataModel.hpp"


int main(int argc, char *argv[])
{
   fc::thread::current().set_name( "main" );
   QApplication app(argc, argv);

   qRegisterMetaType<std::function<void()>>();

   qmlRegisterType<Asset>("Graphene.Client", 0, 1, "Asset");
   qmlRegisterType<Balance>("Graphene.Client", 0, 1, "Balance");
   qmlRegisterType<Account>("Graphene.Client", 0, 1, "Account");
   qmlRegisterType<ChainDataModel>("Graphene.Client", 0, 1, "DataModel");
   qmlRegisterType<GrapheneApplication>("Graphene.Client", 0, 1, "GrapheneApplication");

   QQmlApplicationEngine engine;
#ifdef NDEBUG
   engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
#else
   engine.load(QUrl(QStringLiteral("qml/main.qml")));
#endif

   return app.exec();
}
