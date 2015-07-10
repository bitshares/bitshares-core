#include <QApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

#include "ClientDataModel.hpp"

int main(int argc, char *argv[])
{
   QApplication app(argc, argv);

   qmlRegisterType<Asset>("Graphene.Client", 0, 1, "Asset");
   qmlRegisterType<Balance>("Graphene.Client", 0, 1, "Balance");
   qmlRegisterType<Account>("Graphene.Client", 0, 1, "Account");
   qmlRegisterType<ClientDataModel>("Graphene.Client", 0, 1, "DataModel");

   QQmlApplicationEngine engine;
   engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

   return app.exec();
}

