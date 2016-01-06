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

#include "GrapheneObject.hpp"

#include "graphene/chain/protocol/asset.hpp"

class Asset : public GrapheneObject {
   Q_OBJECT

   Q_PROPERTY(QString symbol MEMBER m_symbol READ symbol NOTIFY symbolChanged)
   Q_PROPERTY(quint32 precision MEMBER m_precision NOTIFY precisionChanged)

   QString m_symbol;
   quint32 m_precision;

   graphene::chain::price coreExchangeRate;

public:
   Asset(ObjectId id = -1, QString symbol = QString(), quint32 precision = 0, QObject* parent = nullptr)
      : GrapheneObject(id, parent), m_symbol(symbol), m_precision(precision)
   {}

   QString symbol() const {
      return m_symbol;
   }

   quint64 precisionPower() const {
      quint64 power = 1;
      for (int i = 0; i < m_precision; ++i)
         power *= 10;
      return power;
   }
   /// Given an amount like 123401, return "1234.01"
   Q_INVOKABLE QString formatAmount(qint64 amount) const;

   void update(const graphene::chain::asset_object& asset);

Q_SIGNALS:
   void symbolChanged();
   void precisionChanged();
};
