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
   Q_PROPERTY(QString rpcEndpoint MEMBER rpcEndpoint CONSTANT)

   fc::thread* chainThread;
   graphene::app::application* grapheneApp;
   fc::future<void> startFuture;
   QString webUsername;
   QString webPassword;
   QString rpcEndpoint;

public:
   BlockChain();
   virtual ~BlockChain();

public Q_SLOTS:
   void start();

Q_SIGNALS:
   void started();
};
