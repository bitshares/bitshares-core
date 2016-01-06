/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
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
