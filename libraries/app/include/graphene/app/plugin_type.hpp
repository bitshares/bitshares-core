#pragma once
#include <graphene/app/plugin.hpp>

#include <graphene/account_history/account_history_plugin.hpp>
#include <graphene/market_history/market_history_plugin.hpp>
#include <graphene/witness/witness.hpp>
#include <graphene/debug_witness/debug_witness.hpp>
#include <graphene/elasticsearch/elasticsearch_plugin.hpp>
#include <graphene/delayed_node/delayed_node_plugin.hpp>
#include <graphene/snapshot/snapshot.hpp>
#include <graphene/es_objects/es_objects.hpp>
#include <graphene/grouped_orders/grouped_orders_plugin.hpp>
#include <graphene/template_plugin/template_plugin.hpp>

namespace graphene { namespace app{
      using plugin_witness                = graphene::witness_plugin::witness_plugin;
      using plugin_debug_witness          = graphene::debug_witness_plugin::debug_witness_plugin;
      using plugin_account_history        = graphene::account_history::account_history_plugin;
      using plugin_elasticsearch          = graphene::elasticsearch::elasticsearch_plugin;
      using plugin_market_history         = graphene::market_history::market_history_plugin;
      using plugin_delayed_node           = graphene::delayed_node::delayed_node_plugin;
      using plugin_snapshot               = graphene::snapshot_plugin::snapshot_plugin;
      using plugin_es_objects             = graphene::es_objects::es_objects_plugin;
      using plugin_grouped_orders         = graphene::grouped_orders::grouped_orders_plugin;
      using plugin_template               = graphene::template_plugin::template_plugin;
      std::string get_plugin_name(const abstract_plugin &plg);
}}

