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
