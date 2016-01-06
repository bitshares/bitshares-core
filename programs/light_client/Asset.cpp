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
#include "Asset.hpp"

#include <graphene/chain/asset_object.hpp>

#include <QVariant>

QString Asset::formatAmount(qint64 amount) const
{
   graphene::chain::asset_object ao;
   ao.precision = m_precision;
   return QString::fromStdString(ao.amount_to_string(amount));
}

void Asset::update(const graphene::chain::asset_object& asset)
{
   if (asset.id.instance() != id())
      setProperty("id", QVariant::fromValue(asset.id.instance()));
   if (asset.symbol != m_symbol.toStdString()) {
      m_symbol = QString::fromStdString(asset.symbol);
      Q_EMIT symbolChanged();
   }
   if (asset.precision != m_precision) {
      m_precision = asset.precision;
      Q_EMIT precisionChanged();
   }

   if (asset.options.core_exchange_rate != coreExchangeRate)
      coreExchangeRate = asset.options.core_exchange_rate;
}
