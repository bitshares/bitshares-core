/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
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
