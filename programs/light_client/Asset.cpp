#include "Asset.hpp"

#include <graphene/chain/asset_object.hpp>

#include <QVariant>

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
