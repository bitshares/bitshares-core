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
