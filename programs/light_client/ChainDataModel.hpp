#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <fc/network/http/websocket.hpp>
#include <graphene/app/api.hpp>

#include "BoostMultiIndex.hpp"
#include "Asset.hpp"
#include "Account.hpp"

#include <QDateTime>
#include <QObject>

using graphene::chain::by_id;

namespace fc {
class thread;
}

struct by_symbol_name;
typedef multi_index_container<
   Asset*,
   indexed_by<
      hashed_unique< tag<by_id>, const_mem_fun<GrapheneObject, ObjectId, &GrapheneObject::id > >,
      ordered_unique< tag<by_symbol_name>, const_mem_fun<Asset, QString, &Asset::symbol> >
   >
> asset_multi_index_type;

struct by_account_name;
typedef multi_index_container<
   Account*,
   indexed_by<
      hashed_unique< tag<by_id>, const_mem_fun<GrapheneObject, ObjectId, &GrapheneObject::id > >,
      ordered_unique< tag<by_account_name>, const_mem_fun<Account, QString, &Account::name> >
   >
> account_multi_index_type;

class Transaction;
class ChainDataModel : public QObject {
   Q_OBJECT
   Q_PROPERTY(QDateTime chainTime READ chainTime NOTIFY blockReceived)

   void processUpdatedObject(const fc::variant& update);

   void getAssetImpl(QString assetIdentifier, Asset* const * assetInContainer);
   void getAccountImpl(QString accountIdentifier, Account* const * accountInContainer);

public:
   Q_INVOKABLE Account* getAccount(ObjectId id);
   Q_INVOKABLE Account* getAccount(QString name);
   Q_INVOKABLE Asset* getAsset(ObjectId id);
   Q_INVOKABLE Asset* getAsset(QString symbol);

   QDateTime chainTime() const;

   ChainDataModel(){}
   ChainDataModel(fc::thread& t, QObject* parent = nullptr);

   void setDatabaseAPI(fc::api<graphene::app::database_api> dbapi);
   void setNetworkAPI(fc::api<graphene::app::network_broadcast_api> napi);

   const graphene::chain::global_property_object& global_properties() const { return m_global_properties; }
   const graphene::chain::dynamic_global_property_object& dynamic_global_properties() const { return m_dynamic_global_properties; }
   const graphene::chain::chain_property_object& chain_properties() const { return m_chain_properties; }

public Q_SLOTS:
   void broadcast(Transaction* transaction);

Q_SIGNALS:
   void queueExecute(const std::function<void()>&);
   void exceptionThrown(QString message);
   void blockReceived();

private:
   fc::thread* m_rpc_thread = nullptr;
   std::string m_api_url;
   fc::api<graphene::app::database_api> m_db_api;
   fc::api<graphene::app::network_broadcast_api> m_net_api;

   graphene::chain::global_property_object m_global_properties;
   graphene::chain::dynamic_global_property_object m_dynamic_global_properties;
   graphene::chain::chain_property_object m_chain_properties;

   ObjectId m_account_query_num = -1;
   account_multi_index_type m_accounts;
   asset_multi_index_type m_assets;
};

