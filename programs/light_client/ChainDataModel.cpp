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
#include "ChainDataModel.hpp"
#include "Balance.hpp"
#include "Operations.hpp"
#include "Transaction.hpp"

#include <graphene/app/api.hpp>
#include <graphene/chain/protocol/protocol.hpp>

#include <fc/rpc/websocket_api.hpp>

#include <QMetaObject>

using namespace graphene::app;

template<typename T>
QString idToString(T id) {
   return QString("%1.%2.%3").arg(T::space_id).arg(T::type_id).arg(ObjectId(id.instance));
}
QString idToString(graphene::db::object_id_type id) {
   return QString("%1.%2.%3").arg(id.space(), id.type(), ObjectId(id.instance()));
}

ChainDataModel::ChainDataModel(fc::thread& t, QObject* parent)
   :QObject(parent),m_rpc_thread(&t){}

void ChainDataModel::setDatabaseAPI(fc::api<database_api> dbapi) {
   m_db_api = dbapi;
   m_rpc_thread->async([this] {
      m_global_properties = m_db_api->get_global_properties();
      m_db_api->subscribe_to_objects([this](const variant& v) {
         m_global_properties = v.as<global_property_object>();
      }, {m_global_properties.id});

      m_dynamic_global_properties = m_db_api->get_dynamic_global_properties();
      m_db_api->subscribe_to_objects([this](const variant& d) {
         m_dynamic_global_properties = d.as<dynamic_global_property_object>();
      }, {m_dynamic_global_properties.id});

      m_chain_properties = m_db_api->get_chain_properties();
   });
}

void ChainDataModel::setNetworkAPI(fc::api<network_broadcast_api> napi)
{
   m_net_api = napi;
}

void ChainDataModel::broadcast(Transaction* transaction)
{
   try {
      m_net_api->broadcast_transaction_with_callback([transaction](const fc::variant&) {
         transaction->setStatus(Transaction::Complete);
      }, transaction->internalTransaction());
      transaction->setStatus(Transaction::Pending);
   } catch (const fc::exception& e) {
      transaction->setStatus(Transaction::Failed);
      Q_EMIT exceptionThrown(QString::fromStdString(e.to_string()));
   }
}

Asset* ChainDataModel::getAsset(ObjectId id)
{
   auto& by_id_idx = m_assets.get<by_id>();
   auto itr = by_id_idx.find(id);
   if (itr == by_id_idx.end())
   {
      auto result = m_assets.insert(new Asset(id, QString::number(--m_account_query_num), 0, this));
      assert(result.second);

      // Run in RPC thread
      m_rpc_thread->async([this,id,result]{ getAssetImpl(idToString(asset_id_type(id)), &*result.first); });
      return *result.first;
   }
   return *itr;
}

Asset* ChainDataModel::getAsset(QString symbol)
{
   auto& by_symbol_idx = m_assets.get<by_symbol_name>();
   auto itr = by_symbol_idx.find(symbol);
   if (itr == by_symbol_idx.end())
   {
      auto result = m_assets.insert(new Asset(--m_account_query_num, symbol, 0, this));
      assert(result.second);

      // Run in RPC thread
      m_rpc_thread->async([this,symbol,result](){ getAssetImpl(symbol, &*result.first); });
      return *result.first;
   }
   return *itr;
}

QDateTime ChainDataModel::chainTime() const {
   return QDateTime::fromTime_t(m_dynamic_global_properties.time.sec_since_epoch());
}

void ChainDataModel::processUpdatedObject(const fc::variant& update)
{
   if (update.is_null())
      return;
   if (&fc::thread::current() == m_rpc_thread)
   {
      ilog("Proxying object update to app thread.");
      Q_EMIT queueExecute([this,update]{processUpdatedObject(update);});
      return;
   }

   idump((update));
   try {
   auto id = update.as<variant_object>()["id"].as<object_id_type>();
   if (id.space() == protocol_ids) {
      switch (id.type()) {
         default:
            wlog("Update procedure for ${update} is not yet implemented.", ("update", update));
            break;
      }
   } else if (id.space() == implementation_ids) {
      switch (id.type()) {
         case impl_account_balance_object_type: {
            account_balance_object balance = update.as<account_balance_object>();
            auto owner = m_accounts.find(balance.owner.instance.value);
            if (owner != m_accounts.end())
               (*owner)->update(balance);
            else
               elog("Got unexpected balance update:\n${u}\nfor an account I don't have.",
                    ("u", update));
            break;
      }

      default:
         wlog("Update procedure for ${update} is not yet implemented.", ("update", update));
         break;
      }
   } else
      wlog("Update procedure for ${update} is not yet implemented.", ("update", update));
   } catch (const fc::exception& e) {
      elog("Caught exception while updating object: ${e}", ("e", e.to_detail_string()));
   }
}

void ChainDataModel::getAssetImpl(QString assetIdentifier, Asset* const * assetInContainer)
{
   try {
      ilog("Fetching asset ${asset}", ("asset", assetIdentifier.toStdString()));
      auto result = m_db_api->lookup_asset_symbols({assetIdentifier.toStdString()});

      // Run in main thread
      Q_EMIT queueExecute([this,result,assetInContainer](){
         ilog("Processing result ${r}", ("r", result));
         auto itr = m_assets.iterator_to(*assetInContainer);

         if (result.size() == 0 || !result.front()) {
            (*itr)->deleteLater();
            m_assets.erase(itr);
         } else {
            m_assets.modify(itr,
                            [=](Asset* a){
               a->setProperty("symbol", QString::fromStdString(result.front()->symbol));
               a->setProperty("id", ObjectId(result.front()->id.instance()));
               a->setProperty("precision", result.front()->precision);
            });
         }
      });
   }
   catch ( const fc::exception& e )
   {
      Q_EMIT exceptionThrown(QString::fromStdString(e.to_string()));
   }
}

void ChainDataModel::getAccountImpl(QString accountIdentifier, Account* const * accountInContainer)
{
   try {
      ilog("Fetching account ${acct}", ("acct", accountIdentifier.toStdString()));
      auto result = m_db_api->get_full_accounts([this](const fc::variant& v) {
         vector<variant> updates = v.as<vector<variant>>();
         for (const variant& update : updates) {
            if (update.is_object())
               processUpdatedObject(update);
            else
               elog("Handling object deletions is not yet implemented: ${update}", ("update", update));
         }
         // TODO: replace true on the next line with a smarter decision as to whether we need status updates or not
      }, {accountIdentifier.toStdString()}, true);
      fc::optional<full_account> accountPackage;

      if (result.count(accountIdentifier.toStdString())) {
         accountPackage = result.at(accountIdentifier.toStdString());

         // Fetch all necessary assets
         QList<asset_id_type> assetsToFetch;
         QList<Asset* const *> assetPlaceholders;
         assetsToFetch.reserve(accountPackage->balances.size());
         // Get list of asset IDs the account has a balance in
         std::transform(accountPackage->balances.begin(), accountPackage->balances.end(), std::back_inserter(assetsToFetch),
                        [](const account_balance_object& b) { return b.asset_type; });
         auto function = [this,&assetsToFetch,&assetPlaceholders] {
            auto itr = assetsToFetch.begin();
            const auto& assets_by_id = m_assets.get<by_id>();
            // Filter out assets I already have, create placeholders for the ones I don't.
            while (itr != assetsToFetch.end()) {
               if (assets_by_id.count(itr->instance))
                  itr = assetsToFetch.erase(itr);
               else {
                  assetPlaceholders.push_back(&*m_assets.insert(new Asset(itr->instance, QString(), 0, this)).first);
                  ++itr;
               }
            }
         };
         QMetaObject::invokeMethod(parent(), "execute", Qt::BlockingQueuedConnection,
                                   Q_ARG(const std::function<void()>&, function));
         assert(assetsToFetch.size() == assetPlaceholders.size());

         // Blocking call to fetch and complete initialization for all the assets
         for (int i = 0; i < assetsToFetch.size(); ++i)
            getAssetImpl(idToString(assetsToFetch[i]), assetPlaceholders[i]);
      }

      // Run in main thread
      Q_EMIT queueExecute([this,accountPackage,accountInContainer](){
         ilog("Processing result ${r}", ("r", accountPackage));
         auto itr = m_accounts.iterator_to(*accountInContainer);

         if (!accountPackage.valid()) {
            (*itr)->deleteLater();
            m_accounts.erase(itr);
         } else {
            m_accounts.modify(itr, [this,&accountPackage](Account* a){
               a->setProperty("id", ObjectId(accountPackage->account.id.instance()));
               a->setAccountObject(accountPackage->account);

               // Set balances
               QList<Balance*> balances;
               std::transform(accountPackage->balances.begin(), accountPackage->balances.end(), std::back_inserter(balances),
                              [this](const account_balance_object& b) {
                  Balance* bal = new Balance;
                  bal->setParent(this);
                  bal->setProperty("amount", QVariant::fromValue(b.balance.value));
                  bal->setProperty("type", QVariant::fromValue(getAsset(ObjectId(b.asset_type.instance))));
                  return bal;
               });
               a->setBalances(balances);
            });
         }
      });
   }
   catch (const fc::exception& e)
   {
      Q_EMIT exceptionThrown(QString::fromStdString(e.to_string()));
   }
}

Account* ChainDataModel::getAccount(ObjectId id)
{
   auto& by_id_idx = m_accounts.get<by_id>();
   auto itr = by_id_idx.find(id);
   if( itr == by_id_idx.end() )
   {
      auto tmp = new Account(id, tr("Account #%1").arg(--m_account_query_num), this);
      auto result = m_accounts.insert(tmp);
      assert(result.second);

      // Run in RPC thread
      m_rpc_thread->async([this, id, result]{getAccountImpl(idToString(account_id_type(id)), &*result.first);});
      return *result.first;
   }
   return *itr;
}

Account* ChainDataModel::getAccount(QString name)
{
   auto& by_name_idx = m_accounts.get<by_account_name>();
   auto itr = by_name_idx.find(name);
   if( itr == by_name_idx.end() )
   {
      auto tmp = new Account(--m_account_query_num, name, this);
      auto result = m_accounts.insert(tmp);
      assert(result.second);

      // Run in RPC thread
      m_rpc_thread->async([this, name, result]{getAccountImpl(name, &*result.first);});
      return *result.first;
   }
   return *itr;
}
