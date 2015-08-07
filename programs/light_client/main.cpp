#include <QApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

#include "GrapheneApplication.hpp"
#include "ChainDataModel.hpp"
#include "Transaction.hpp"
#include "Operations.hpp"
#include "Balance.hpp"
#include "Wallet.hpp"

class Crypto {
   Q_GADGET

public:
   Q_INVOKABLE QString sha256(QByteArray data) {
      return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
   }
};
QML_DECLARE_TYPE(Crypto)

int main(int argc, char *argv[])
{
#ifndef NDEBUG
   QQmlDebuggingEnabler enabler;
#endif
   fc::thread::current().set_name("main");
   QApplication app(argc, argv);
   app.setApplicationName("Graphene Client");
   app.setOrganizationDomain("cryptonomex.org");
   app.setOrganizationName("Cryptonomex, Inc.");

   qRegisterMetaType<std::function<void()>>();
   qRegisterMetaType<ObjectId>();
   qRegisterMetaType<QList<OperationBase*>>();
   qRegisterMetaType<Transaction::Status>();

   qmlRegisterType<Asset>("Graphene.Client", 0, 1, "Asset");
   qmlRegisterType<Balance>("Graphene.Client", 0, 1, "Balance");
   qmlRegisterType<Account>("Graphene.Client", 0, 1, "Account");
   qmlRegisterType<ChainDataModel>("Graphene.Client", 0, 1, "DataModel");
   qmlRegisterType<Wallet>("Graphene.Client", 0, 1, "Wallet");
   qmlRegisterType<GrapheneApplication>("Graphene.Client", 0, 1, "GrapheneApplication");
   qmlRegisterType<Transaction>("Graphene.Client", 0, 1, "Transaction");

   qmlRegisterUncreatableType<OperationBase>("Graphene.Client", 0, 1, "OperationBase",
                                             "OperationBase is an abstract base class; cannot be created");
   qmlRegisterType<TransferOperation>("Graphene.Client", 0, 1, "TransferOperation");

   qmlRegisterUncreatableType<OperationBuilder>("Graphene.Client", 0, 1, "OperationBuilder",
                                                QStringLiteral("OperationBuilder cannot be created from QML"));

   QQmlApplicationEngine engine;
   QVariant crypto;
   crypto.setValue(Crypto());
   engine.rootContext()->setContextProperty("Crypto", crypto);
#ifdef NDEBUG
   engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
#else
   engine.load(QUrl(QStringLiteral("qml/main.qml")));
   QFileSystemWatcher watcher;
   qDebug() << watcher.addPath("qml/");
   QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &engine, [&](QString path) {
      qDebug() << "Changed file" << path;
      engine.clearComponentCache();
   });
#endif

   return app.exec();
}

#include "main.moc"
