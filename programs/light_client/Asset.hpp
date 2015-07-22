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

   void update(const graphene::chain::asset_object& asset);

Q_SIGNALS:
   void symbolChanged();
   void precisionChanged();
};
