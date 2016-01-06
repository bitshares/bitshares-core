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
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include "GrapheneObject.hpp"

namespace graphene { namespace chain {
class account_balance_object;
}}

class Asset;
class Balance : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(Asset* type MEMBER m_type READ type NOTIFY typeChanged)
   Q_PROPERTY(qint64 amount MEMBER amount NOTIFY amountChanged)

   Asset* m_type;
   qint64 amount;

public:
   // This ultimately needs to be replaced with a string equivalent
   Q_INVOKABLE qreal amountReal() const;

   Asset* type()const {
      return m_type;
   }

   void update(const graphene::chain::account_balance_object& update);

Q_SIGNALS:
   void typeChanged();
   void amountChanged();
};
