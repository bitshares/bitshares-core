/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
